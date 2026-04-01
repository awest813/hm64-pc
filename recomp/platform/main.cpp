/**
 * hm64_pc - PC port entry point
 * Updated for current N64ModernRuntime API
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <filesystem>
#include <string>
#include <memory>
#include <cstddef>
#include <chrono>
#include <thread>

#include <SDL2/SDL.h>

#include <ultramodern/ultramodern.hpp>
#include <ultramodern/renderer_context.hpp>
#include <ultramodern/rsp.hpp>
#include <ultramodern/input.hpp>
#include <ultramodern/events.hpp>
#include <ultramodern/error_handling.hpp>
#include <ultramodern/threads.hpp>
#include <librecomp/game.hpp>
#include <librecomp/recomp.h>
#include <librecomp/overlays.hpp>
#include <xxHash/xxh3.h>

extern "C" recomp_func_t* get_function(int32_t addr);

#include "audio.h"
#include "graphics.h"
#include "input.h"
#include <librecomp/rsp.hpp>

extern "C" {
    void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
    void register_game_overlays();
}

// Silent RSP audio ucode: called in place of real RSP audio microcode.
// Just returns Broke (success) without touching any audio output buffers.
// The game will hear silence, but nothing will crash.
static RspExitReason silent_audio_ucode(uint8_t* rdram, uint32_t ucode_addr) {
    (void)rdram; (void)ucode_addr;
    return RspExitReason::Broke;
}

static RspUcodeFunc* hm64_get_rsp_microcode(const OSTask* task) {
    if (task->t.type == M_AUDTASK) {
        printf("[rsp] Audio task – returning silent ucode\n");
        fflush(stdout);
        return silent_audio_ucode;
    }
    fprintf(stderr, "[rsp] Unknown RSP task type %u – returning nullptr\n", task->t.type);
    return nullptr;
}

// ---------------------------------------------------------------------------
// load_elf_data_section – Load the .code segment's data content from the
// decomp ELF file into RDRAM.
//
// Background:
//   The 1MB DMA (do_rom_read) loads the original ROM binary into RDRAM.  For
//   code (functions), this matches the ELF VMA addresses because N64Recomp
//   generated recompiled functions at the same VMAs.  However, for statically
//   initialized DATA (global arrays, struct tables, etc.), the content in the
//   original ROM binary does NOT match what the decomp ELF puts at those
//   VMAs.  The difference arises because the decomp ELF was built from
//   partially-reconstructed C sources that don't exactly reproduce the
//   original compiler output, leaving a layout gap in the data section.
//
//   The fix: after the ROM DMA, overwrite the code segment's data region in
//   RDRAM with the correct bytes from the ELF file.  The ELF's data section
//   bytes were produced by compiling the decomp sources and represent the
//   values the recompiled code expects to see at those VMAs.
//
// ELF LOAD segment (from readelf -l hm64.elf):
//   Offset=0x015c00  VirtAddr=0x80025c00  FileSiz=0xf96b0
//
// We load only the DATA portion (after the text/ucode) to avoid overwriting
// the function code that the ROM DMA loaded correctly.  The text portion of
// the code segment is fine because the recompiled C code is generated at the
// same VMAs.
//
// Data section starts at _codeSegmentDataStart VMA = 0x8010CAD0.
// We load from there to the end of the ELF LOAD segment content.
// ---------------------------------------------------------------------------
static void load_elf_data_section(uint8_t* rdram, const std::string& elf_path) {
    printf("[init] load_elf_data_section: looking for ELF at path: %s\n", elf_path.c_str());
    fflush(stdout);
    
    // ELF LOAD segment parameters
    constexpr uint32_t ELF_LOAD_FILE_OFFSET = 0x015c00u;
    constexpr uint32_t ELF_LOAD_VADDR       = 0x80025c00u;
    constexpr uint32_t ELF_LOAD_FILESZ      = 0x0f96b0u;

    // We only need to overwrite the data portion (after text/ucode).
    // _codeSegmentDataStart = 0x8010CAD0 (where ucode text ends and data begins).
    constexpr uint32_t DATA_VADDR_START = 0x8010CAD0u;

    uint32_t data_offset_in_segment = DATA_VADDR_START - ELF_LOAD_VADDR;
    uint32_t elf_data_file_offset   = ELF_LOAD_FILE_OFFSET + data_offset_in_segment;
    uint32_t data_size              = ELF_LOAD_FILESZ - data_offset_in_segment;

    std::ifstream elf_file(elf_path, std::ios::binary);
    if (!elf_file.good()) {
        fprintf(stderr, "[init] WARNING: Could not open ELF file '%s' – data section not patched.\n",
                elf_path.c_str());
        fprintf(stderr, "[init]   Static data (rdpstateinit_dl, spawnPointToMap, etc.) will be\n");
        fprintf(stderr, "[init]   wrong and the game will likely crash during map loading.\n");
        return;
    }

    elf_file.seekg(elf_data_file_offset);
    std::vector<uint8_t> data(data_size);
    elf_file.read(reinterpret_cast<char*>(data.data()), data_size);
    if (elf_file.fail()) {
        fprintf(stderr, "[init] WARNING: Failed to read ELF data section.\n");
        return;
    }

    // Write into RDRAM using MEM_B for correct byte-lane ordering.
    // MEM_B(i, vaddr) = rdram[(vaddr + i) ^ 3 - 0x80000000]
    gpr base_vaddr = (gpr)(int32_t)DATA_VADDR_START;
    for (uint32_t i = 0; i < data_size; i++) {
        MEM_B(i, base_vaddr) = (int8_t)data[i];
    }

    printf("[init] Loaded ELF data section: VMA 0x%08X .. 0x%08X (%u bytes) from '%s'\n",
           DATA_VADDR_START, DATA_VADDR_START + data_size, data_size, elf_path.c_str());
    fflush(stdout);
}

// Forward declarations for patched functions that need indirect-call registration
extern "C" void setMainLoopCallbackFunctionIndex(uint8_t*, recomp_context*);
extern "C" void setMapAudioAndLighting(uint8_t*, recomp_context*);
extern "C" void setLevelLighting(uint8_t*, recomp_context*);
extern "C" void levelLoadCallback(uint8_t*, recomp_context*);

static void hm64_on_init(uint8_t* rdram, recomp_context* ctx) {
    // Zero out audio-library global state so audio fn-ptrs start NULL.
    constexpr uint32_t AU_STATE_VADDR = 0x80117C00u;
    constexpr uint32_t AU_STATE_SIZE  = 0x200u;
    memset(rdram + (AU_STATE_VADDR - 0x80000000u), 0, AU_STATE_SIZE);
    printf("[init] zeroed audio state 0x%08X-0x%08X\n",
           AU_STATE_VADDR, AU_STATE_VADDR + AU_STATE_SIZE);

    // Patch the code segment data section from the decomp ELF.
    load_elf_data_section(rdram, "hm64.elf");

    // Register patched functions in the overlay map so LOOKUP_FUNC indirect calls
    // (e.g. from mainLoopCallbacksTable) route to our versions, not the recompiled originals.
    recomp::overlays::add_loaded_function((int32_t)0x800261CCu, setMainLoopCallbackFunctionIndex);
    recomp::overlays::add_loaded_function((int32_t)0x8005AB6Cu, setMapAudioAndLighting);
    recomp::overlays::add_loaded_function((int32_t)0x8005ABD4u, setLevelLighting);
    recomp::overlays::add_loaded_function((int32_t)0x8005B4A4u, levelLoadCallback);

    // Immediately verify func_map has our versions
    void* got_smaal = (void*)get_function((int32_t)0x8005AB6Cu);
    void* got_sml   = (void*)get_function((int32_t)0x8005ABD4u);
    void* got_scmf  = (void*)get_function((int32_t)0x800261CCu);
    printf("[init] registered patched callback-chain functions in overlay map\n");
    printf("[init] func_map[setMapAudioAndLighting] = %p  (patch=%p match=%d)\n",
           got_smaal, (void*)setMapAudioAndLighting, got_smaal == (void*)setMapAudioAndLighting);
    printf("[init] func_map[setLevelLighting]        = %p  (patch=%p match=%d)\n",
           got_sml, (void*)setLevelLighting, got_sml == (void*)setLevelLighting);
    printf("[init] func_map[setMainLoopCallbackFn]   = %p  (patch=%p match=%d)\n",
           got_scmf, (void*)setMainLoopCallbackFunctionIndex, got_scmf == (void*)setMainLoopCallbackFunctionIndex);

    fflush(stdout);
    (void)ctx;
}

namespace {
static std::unique_ptr<hm64::graphics::RendererContext> s_renderer;
}

static ultramodern::renderer::callbacks_t make_renderer_callbacks() {
    ultramodern::renderer::callbacks_t cb{};
    cb.create_render_context = [](uint8_t* rdram, hm64::graphics::WindowHandle window_handle, bool developer_mode)
        -> std::unique_ptr<ultramodern::renderer::RendererContext>
    {
        return hm64::graphics::create_render_context(rdram, window_handle, developer_mode);
    };
    return cb;
}

static ultramodern::audio_callbacks_t make_audio_callbacks() {
    ultramodern::audio_callbacks_t cb{};
    cb.queue_samples = hm64::audio::queue_samples;
    cb.get_frames_remaining = hm64::audio::get_frames_remaining;
    cb.set_frequency = hm64::audio::set_frequency;
    return cb;
}

static ultramodern::input::callbacks_t make_input_callbacks() {
    ultramodern::input::callbacks_t cb{};
    cb.poll_input = hm64::input::poll;
    cb.get_input = [](int controller_num, uint16_t* buttons, float* x, float* y) -> bool {
        return hm64::input::get_input(controller_num, buttons, x, y);
    };
    cb.set_rumble = [](int controller_num, bool rumble) {
        hm64::input::set_rumble(controller_num, rumble);
    };
    cb.get_connected_device_info = nullptr;
    return cb;
}

static recomp::rsp::callbacks_t make_rsp_callbacks() {
    recomp::rsp::callbacks_t cb{};
    cb.get_rsp_microcode = hm64_get_rsp_microcode;
    return cb;
}

static ultramodern::gfx_callbacks_t make_gfx_callbacks() {
    ultramodern::gfx_callbacks_t cb{};
    cb.create_gfx = nullptr;
    cb.create_window = [](void* gfx_data) -> hm64::graphics::WindowHandle {
        return hm64::graphics::create_window();
    };
    cb.update_gfx = [](void* gfx_data) {
        hm64::graphics::update_gfx();
    };
    return cb;
}

static ultramodern::events::callbacks_t make_events_callbacks() {
    ultramodern::events::callbacks_t cb{};
    cb.vi_callback = []() {
        hm64::graphics::on_vi_interrupt();
    };
    cb.gfx_init_callback = nullptr;
    return cb;
}

static ultramodern::error_handling::callbacks_t make_error_callbacks() {
    ultramodern::error_handling::callbacks_t cb{};
    cb.message_box = [](const char* msg) {
        fprintf(stderr, "[hm64_pc] Error: %s\n", msg);
    };
    return cb;
}

static ultramodern::threads::callbacks_t make_threads_callbacks() {
    ultramodern::threads::callbacks_t cb{};
    cb.get_game_thread_name = nullptr;
    return cb;
}

// HM64 US ROM xxhash3_64 hash
static constexpr uint64_t HM64_US_ROM_HASH = 0x68b2c3755c527305ULL;
// HM64 boot address: where IPL3 copies the first 1MB of ROM (from ROM offset 0x1000).
// This is read from the N64 ROM header at offset 0x8, and is used as the DMA base for
// load_overlays(). It is NOT the PC entry point (which is 0x80025D90).
static constexpr gpr HM64_ENTRYPOINT = (gpr)(int32_t)0x80025C00;
static const std::u8string HM64_GAME_ID = u8"hm64_us";

int main(int argc, char* argv[]) {
    printf("[hm64_pc] Starting...\n");
    fflush(stdout);

    std::string rom_path = "baserom.us.z64";
    if (argc >= 2) {
        rom_path = argv[1];
    }
    printf("[hm64_pc] ROM path: %s\n", rom_path.c_str());
    fflush(stdout);

    // Register the config path (current directory)
    recomp::register_config_path(".");

    // Register the HM64 US game entry
    recomp::GameEntry hm64_entry{};
    hm64_entry.rom_hash = HM64_US_ROM_HASH;
    hm64_entry.internal_name = "HARVESTMOON64";
    hm64_entry.game_id = HM64_GAME_ID;
    hm64_entry.mod_game_id = "";
    hm64_entry.save_type = recomp::SaveType::Sram;
    hm64_entry.is_enabled = true;
    hm64_entry.entrypoint_address = HM64_ENTRYPOINT;
    hm64_entry.entrypoint = recomp_entrypoint;
    hm64_entry.on_init_callback = hm64_on_init;

    if (!recomp::register_game(hm64_entry)) {
        fprintf(stderr, "[hm64_pc] Failed to register game\n");
        return EXIT_FAILURE;
    }

    std::u8string game_id = HM64_GAME_ID;
    auto rom_result = recomp::select_rom(rom_path, game_id);
    if (rom_result != recomp::RomValidationError::Good) {
        fprintf(stderr, "[hm64_pc] Failed to load ROM: %s\n", rom_path.c_str());
        switch (rom_result) {
            case recomp::RomValidationError::FailedToOpen:
                fprintf(stderr, "  Could not open file. Make sure it exists and is readable.\n"); break;
            case recomp::RomValidationError::IncorrectRom:
                fprintf(stderr, "  Wrong ROM. Expected Harvest Moon 64 (USA).\n"); break;
            case recomp::RomValidationError::IncorrectVersion:
                fprintf(stderr, "  Wrong version. Expected the US (v1.0) release.\n"); break;
            case recomp::RomValidationError::NotARom:
                fprintf(stderr, "  File is not a valid N64 ROM.\n"); break;
            default:
                fprintf(stderr, "  Unknown error.\n"); break;
        }
        return EXIT_FAILURE;
    }

    printf("[hm64_pc] ROM: %s\n", rom_path.c_str());
    printf("[hm64_pc] About to load ROM manually...\n");
    fflush(stdout);

    // Actually load the ROM now to avoid thread issues with load_stored_rom
    {
        printf("[hm64_pc] Opening ROM file...\n");
        fflush(stdout);
        std::ifstream file(rom_path, std::ios::binary);
        if (!file.good()) {
            fprintf(stderr, "[hm64_pc] Failed to open ROM file: %s\n", rom_path.c_str());
            return EXIT_FAILURE;
        }
        printf("[hm64_pc] ROM file opened successfully\n");
        fflush(stdout);
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        printf("[hm64_pc] ROM size: %zu bytes\n", size);
        fflush(stdout);
        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> rom_data(size);
        printf("[hm64_pc] Vector allocated, reading file...\n");
        fflush(stdout);
        file.read(reinterpret_cast<char*>(rom_data.data()), size);
        printf("[hm64_pc] File read complete\n");
        fflush(stdout);
        
        // Calculate hash to verify
        uint64_t hash = XXH3_64bits(rom_data.data(), rom_data.size());
        printf("[hm64_pc] ROM hash: 0x%016llx\n", (unsigned long long)hash);
        fflush(stdout);
        
        recomp::set_rom_contents(std::move(rom_data));
        printf("[hm64_pc] ROM contents set\n");
        printf("[hm64_pc] ROM loaded (%zu bytes)\n", size);
        fflush(stdout);
    }

    {
        auto save_path = std::filesystem::absolute("hm64.sav");
        printf("[hm64_pc] Save file: %s\n", save_path.string().c_str());
    }

    printf("[hm64_pc] --- Controls ---\n");
    printf("[hm64_pc]   Arrow keys=D-pad  WASD=Analog\n");
    printf("[hm64_pc]   Z=Z  X=B  C=A  Enter=Start  Shift=R  Q=L\n");
    printf("[hm64_pc]   F11=fullscreen  Esc=quit\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
        fprintf(stderr, "[hm64_pc] SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    hm64::graphics::init();

    recomp::Configuration config{};
    config.project_version = {0, 1, 0, ""};
    config.window_handle = hm64::graphics::get_window_handle();
    config.rsp_callbacks = make_rsp_callbacks();
    config.renderer_callbacks = make_renderer_callbacks();
    config.audio_callbacks = make_audio_callbacks();
    config.input_callbacks = make_input_callbacks();
    config.gfx_callbacks = make_gfx_callbacks();
    config.events_callbacks = make_events_callbacks();
    config.error_handling_callbacks = make_error_callbacks();
    config.threads_callbacks = make_threads_callbacks();

    register_game_overlays();

    // start_game must be called *after* the ultramodern threads are running
    // (so VI/gfx threads have initialised their state). We launch it from a
    // background thread that waits briefly to avoid a race with the VI thread.
    std::thread game_starter([game_id]() {
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(200ms);
        recomp::start_game(game_id);
    });
    game_starter.detach();

    // This call blocks until the window is closed or the game exits.
    recomp::start(config);

    hm64::graphics::deinit();
    SDL_Quit();

    return EXIT_SUCCESS;
}
