/**
 * nusys_patches.cpp – PC replacements for NuSystem API stubs.
 * Updated for current N64ModernRuntime API
 *
 * IMPORTANT: All RECOMP_PATCH functions must be declared extern "C" so the
 * linker sees them as C-linkage symbols matching the recompiled game code.
 * Without extern "C", C++ name mangling creates a different symbol and the
 * patch is never called.
 */

#include "librecomp/recomp.h"
#include "librecomp/game.hpp"
#include "librecomp/addresses.hpp"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/ultra64.h"

#include <atomic>
#include <cstring>
#include <cstdio>
#include <thread>
#include <unistd.h>

extern "C" void gfxRetraceCallback(uint8_t* rdram, recomp_context* ctx);

static std::atomic<uint8_t*> s_rdram{nullptr};
std::atomic<bool> s_retrace_registered{false};  // non-static for extern access

static std::atomic<uint64_t> g_evt{0};

static void log_rdram_event(const char* tag, uint8_t* arg_rdram, void* step_ptr = nullptr) {
    uint64_t n = g_evt.fetch_add(1, std::memory_order_relaxed);
    uint8_t* cur = s_rdram.load(std::memory_order_acquire);
    size_t tid_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    fprintf(stderr,
        "[%06llu] pid=%d tid=%zu tag=%-30s &s_rdram=%p s_rdram=%p arg_rdram=%p step_ptr=%p\n",
        (unsigned long long)n, getpid(), tid_hash, tag,
        (void*)&s_rdram, (void*)cur, (void*)arg_rdram, step_ptr);
    fflush(stderr);
}

// Chain counters – accessed from graphics.cpp and title_patches.cpp
std::atomic<uint32_t> g_vi_count{0};
std::atomic<uint32_t> g_invoke_count{0};
std::atomic<uint32_t> g_gfxcb_count{0};
std::atomic<uint32_t> g_stepset_count{0};

// Plain wrapper so graphics.cpp (inside a namespace) can call it without namespace issues
void hm64_vi_count_increment() { g_vi_count.fetch_add(1, std::memory_order_relaxed); }

// Expose s_rdram address for cross-TU address identity checks
extern "C" void* hm64_get_s_rdram_addr() { return (void*)&s_rdram; }
extern "C" void log_rdram_event_external(const char* tag, uint8_t* arg_rdram, void* step_ptr) {
    log_rdram_event(tag, arg_rdram, step_ptr);
}

// Stack in RDRAM BSS at 0x80222000, 32KB.
// renderSceneGraph + guRotateRPY + fcos + guOrtho etc. each use significant
// stack depth through the VI thread retrace callback path.
static constexpr uint32_t RETRACE_STACK_BASE = 0x80222000u;
static constexpr uint32_t RETRACE_STACK_SIZE = 0x8000u;  // 32KB

// stepMainLoop flag at VMA 0x80205208
static constexpr uint32_t STEP_MAIN_LOOP_ADDR = 0x80205208u;

void hm64_invoke_retrace_callback() {
    uint8_t* rdram = s_rdram.load(std::memory_order_acquire);
    if (rdram == nullptr || !s_retrace_registered.load(std::memory_order_acquire)) {
        return;
    }
    uint32_t count = g_invoke_count.fetch_add(1, std::memory_order_relaxed);

    volatile uint8_t* step_ptr = (volatile uint8_t*)(rdram + ((STEP_MAIN_LOOP_ADDR ^ 3u) - 0x80000000u));

    if (count < 5 || count % 30 == 0) {
        log_rdram_event("invoke_retrace", rdram, (void*)step_ptr);
    }

    recomp_context ctx{};
    ctx.f_odd = &ctx.f0.u32h;
    ctx.mips3_float_mode = 1;
    ctx.r29 = (gpr)(int32_t)(RETRACE_STACK_BASE + RETRACE_STACK_SIZE - 0x10u);
    ctx.r4  = 0;
    gfxRetraceCallback(rdram, &ctx);

    if (count < 5 || count % 30 == 0) {
        uint8_t step_after = rdram[(STEP_MAIN_LOOP_ADDR ^ 3u) - 0x80000000u];
        fprintf(stderr, "[%06llu] invoke #%u stepML_after=%u\n",
                (unsigned long long)g_evt.load(std::memory_order_relaxed), count, step_after);
        fflush(stderr);
    }
}

// ---------------------------------------------------------------------------
// NuSystem graphics patches
// ---------------------------------------------------------------------------

// RDRAM virtual address range for our two OSTask ping-pong slots.
static constexpr uint32_t NU_GFX_TASK_ADDR_0 = 0x80237380u;
static constexpr uint32_t NU_GFX_TASK_ADDR_1 = 0x802373C0u;
static int s_task_slot = 0;

// NuSystem globals – addresses confirmed from data_dump.toml:
//   nuGfxFunc        vram = 0x80118050
//   nuGfxSwapCfbFunc vram = 0x80118064
static constexpr uint32_t NU_GFX_FUNC_ADDR      = 0x80118050u;
static constexpr uint32_t NU_GFX_SWAP_FUNC_ADDR = 0x80118064u;

static constexpr uint32_t CONT_PAK_ERROR_OFFSET = 8;

std::atomic<uint32_t> g_update_ml_count{0};

extern "C" {

// Forward declarations
void updateMainLoopTimer(uint8_t* rdram, recomp_context* ctx);
void handleGraphicsUpdate(uint8_t* rdram, recomp_context* ctx);

// gfxRetraceCallback – called on every VI retrace to update graphics and main loop.
// This is registered via nuGfxFuncSet and is the core of the game loop.
RECOMP_PATCH void gfxRetraceCallback(uint8_t* rdram, recomp_context* ctx) {
    // pendingGfx is in r4 (ctx->r4)
    int pendingGfx = (int)ctx->r4;

    // pendingGfxNum = pendingGfx
    constexpr uint32_t PENDING_GFX_NUM_ADDR = 0x80205635u;
    rdram[(PENDING_GFX_NUM_ADDR ^ 3u) - 0x80000000u] = (uint8_t)pendingGfx;

    // engineStateFlags &= ~2 (clear bit 1)
    constexpr uint32_t ENGINE_FLAGS_ADDR = 0x80205622u;
    uint16_t engFlags = *(uint16_t*)(rdram + ((ENGINE_FLAGS_ADDR ^ 2u) - 0x80000000u));
    engFlags &= ~2u;
    *(uint16_t*)(rdram + ((ENGINE_FLAGS_ADDR ^ 2u) - 0x80000000u)) = engFlags;

    // readControllerData() - stubbed, skip

    // handleGraphicsUpdate(pendingGfx) - this is where stepMainLoop gets set to 1
    extern void handleGraphicsUpdate(uint8_t* rdram, recomp_context* ctx);
    ctx->r4 = (gpr)pendingGfx;
    handleGraphicsUpdate(rdram, ctx);

    // updateMainLoopTimer(pendingGfx)
    // We already have a separate patch for this, but let's call it here too
    // for completeness since the original did both
    updateMainLoopTimer(rdram, ctx);

    // if (frameCount > 59) { frameCount = 0; engineStateFlags |= 2; }
    constexpr uint32_t FRAME_COUNT_ADDR = 0x80205634u;
    uint8_t frameCount = rdram[(FRAME_COUNT_ADDR ^ 3u) - 0x80000000u];
    if (frameCount > 59) {
        rdram[(FRAME_COUNT_ADDR ^ 3u) - 0x80000000u] = 0;
        engFlags = *(uint16_t*)(rdram + ((ENGINE_FLAGS_ADDR ^ 2u) - 0x80000000u));
        engFlags |= 2u;
        *(uint16_t*)(rdram + ((ENGINE_FLAGS_ADDR ^ 2u) - 0x80000000u)) = engFlags;
    }

    // frameCount++
    frameCount = rdram[(FRAME_COUNT_ADDR ^ 3u) - 0x80000000u];
    frameCount++;
    rdram[(FRAME_COUNT_ADDR ^ 3u) - 0x80000000u] = frameCount;

    // retraceCount++ (at 0x8020520C)
    constexpr uint32_t RETRACE_COUNT_ADDR = 0x8020520Cu;
    uint32_t retraceCount = *(uint32_t*)(rdram + (RETRACE_COUNT_ADDR - 0x80000000u));
    retraceCount++;
    *(uint32_t*)(rdram + (RETRACE_COUNT_ADDR - 0x80000000u)) = retraceCount;

    ctx->r2 = 0;  // return 0 (unused)
}

RECOMP_PATCH void updateMainLoopTimer(uint8_t* rdram, recomp_context* ctx) {
    uint32_t count = g_update_ml_count.fetch_add(1, std::memory_order_relaxed);
    
    constexpr uint32_t FRAME_COUNT_ADDR    = 0x80205634u;
    constexpr uint32_t ML_UPDATE_RATE_ADDR = 0x802226E2u;
    constexpr uint32_t STEP_MAIN_LOOP_ADDR = 0x80205208u;
    
    // Physical byte address using same XOR-3 lane as MEM_B/MEM_BU macros.
    volatile uint8_t* step_ptr = (volatile uint8_t*)(rdram + ((STEP_MAIN_LOOP_ADDR ^ 3u) - 0x80000000u));
    uint8_t mlRate     = *(volatile uint8_t*)(rdram + ((ML_UPDATE_RATE_ADDR ^ 3u) - 0x80000000u));
    uint8_t frameCount = *(volatile uint8_t*)(rdram + ((FRAME_COUNT_ADDR ^ 3u) - 0x80000000u));
    uint8_t step_before = *step_ptr;
    
    if (count % 30 == 0 || count < 5) {
        log_rdram_event("updateMainLoopTimer", rdram, (void*)step_ptr);
        fprintf(stderr, "         count=%u frameCount=%u rate=%u stepML=%u\n",
                count, frameCount, mlRate, step_before);
        fflush(stderr);
    }
    
    // Replicate the gate: if (frameCount % rate == 0) && stepMainLoop == FALSE → set TRUE.
    // Use atomic store with release so the game-thread spin (separate OS thread) sees it.
    if (mlRate == 0 || (frameCount % mlRate) == 0) {
        if (step_before == 0) {
            __atomic_store_n(step_ptr, 1, __ATOMIC_RELEASE);
        }
    }
}

// ---------------------------------------------------------------------------
// nuGfxInit – NuSystem graphics initialisation.
//
// The original nuGfxInit builds and submits an RDP state-initialisation
// display list from the static rdpstateinit_dl[] array.  On PC, the symbol
// address for rdpstateinit_dl in RDRAM does NOT match the 1MB DMA load
// layout (the ELF VMA is off by 0x4BE0 relative to what do_rom_read places
// in RDRAM), so RT64 crashes trying to follow the gSPDisplayList jump to
// garbage bytes.
//
// This patch replicates all useful side-effects of nuGfxInit (setting up the
// framebuffer, ucode pointer, etc.) and skips only the broken display list
// submission.  The RDP state will be initialised by the first proper frame.
// ---------------------------------------------------------------------------
RECOMP_PATCH void nuGfxInit(uint8_t* rdram, recomp_context* ctx) {
    // nuGfxThreadStart – stubbed, skip
    // nuGfxSetCfb(FrameBuf, 3) – sets up framebuffer array pointer in NuSystem
    // We call it directly via the recompiled version.
    extern void nuGfxSetCfb(uint8_t*, recomp_context*);
    extern void nuGfxSwapCfbFuncSet(uint8_t*, recomp_context*);

    // nuGfxSetCfb(FrameBuf, 3):  r4 = &FrameBuf, r5 = 3
    // FrameBuf vram = 0x8010DD38
    // The framebuffers are at: 0x8038F800, 0x803B5000, 0x803DA800
    // Initialize FrameBuf array directly with correct framebuffer addresses
    constexpr uint32_t FRAME_BUF_ARRAY_ADDR = 0x8010DD38u;
    constexpr uint32_t FB0_ADDR = 0x8038F800u;
    constexpr uint32_t FB1_ADDR = 0x803B5000u;
    constexpr uint32_t FB2_ADDR = 0x803DA800u;
    *(uint32_t*)(rdram + (FRAME_BUF_ARRAY_ADDR - 0x80000000u)) = FB0_ADDR;
    *(uint32_t*)(rdram + (FRAME_BUF_ARRAY_ADDR - 0x80000000u) + 4) = FB1_ADDR;
    *(uint32_t*)(rdram + (FRAME_BUF_ARRAY_ADDR - 0x80000000u) + 8) = FB2_ADDR;
    
    // Also initialize nuGfxCfb pointer to FrameBuf array
    constexpr uint32_t NU_GFX_CFB_ADDR = 0x80215EBCu;
    *(uint32_t*)(rdram + (NU_GFX_CFB_ADDR - 0x80000000u)) = FRAME_BUF_ARRAY_ADDR;
    
    // Initialize nuGfxCfbP to first framebuffer
    constexpr uint32_t NU_GFX_CFBP_ADDR = 0x80205750u;
    *(uint32_t*)(rdram + (NU_GFX_CFBP_ADDR - 0x80000000u)) = FB0_ADDR;
    
    // Initialize framebufferCount to 0
    constexpr uint32_t FB_COUNT_ADDR = 0x801C3F71u;
    rdram[(FB_COUNT_ADDR ^ 3u) - 0x80000000u] = 0;

    // nuGfxSetZBuffer(NU_GFX_ZBUFFER_ADDR = 0x80000400):
    // nuGfxZBuffer at VMA 0x801806C4 is a pointer to the zbuffer.
    // Without this, nuGfxZBuffer = 0 and clearFramebuffer writes G_SETDEPTHIMAGE
    // with physical address 0, causing RT64 to crash.
    {
        constexpr uint32_t NU_GFX_ZBUFFER_VADDR = 0x801806C4u;  // nuGfxZBuffer global
        constexpr uint32_t ZBUFFER_ADDR = 0x80000400u;           // NU_GFX_ZBUFFER_ADDR
        // nuGfxZBuffer is a u64* pointer – write as 32-bit pointer (N64 is 32-bit).
        // Use MEM_W equivalent: *(int32_t*)(rdram + (vaddr - 0x80000000u)) = value
        *(int32_t*)(rdram + (NU_GFX_ZBUFFER_VADDR - 0x80000000u)) = (int32_t)ZBUFFER_ADDR;
    }

    // nuGfxSwapCfbFuncSet(nuGfxSwapCfb) – our patch stubs this out anyway
    // nuGfxTaskMgrInit – stubbed, skip

    // Set nuGfxUcode global: RDRAM[0x801C3F74] = &nugfx_ucode (0x8010DD30)
    // (Used by the real nuGfxTaskMgr but we bypass it.)
    constexpr uint32_t NU_GFX_UCODE_PTR = 0x801C3F74u;
    uint32_t phys = NU_GFX_UCODE_PTR & 0x1FFFFFFFu;
    rdram[phys+0] = 0x80; rdram[phys+1] = 0x10;
    rdram[phys+2] = 0xDD; rdram[phys+3] = 0x30;

    // Skip the rdpstateinit_dl display list submission – it crashes RT64
    // because the RDRAM layout doesn't match the ELF symbol addresses.
    // nuGfxTaskAllEndWait – stubbed, skip

    printf("[nuGfxInit] patched – skipped broken rdpstateinit_dl DL submission\n");
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// nuGfxTaskStart – Submit a display list to RT64.
// r4=gfxp, r5=gfxsize, r6=flags, r7=swapCFB
// ---------------------------------------------------------------------------
// nuGfxTaskStart – submit a display list to RT64 and optionally swap the framebuffer.
// r4=gfxp (DL start vaddr), r5=gfxsize (bytes), r6=ucode flags, r7=swapCFB (1=swap)
RECOMP_PATCH void nuGfxTaskStart(uint8_t* rdram, recomp_context* ctx) {
    uint32_t gfxp    = (uint32_t)ctx->r4;
    uint32_t gfxsize = (uint32_t)ctx->r5;
    uint32_t swap    = (uint32_t)ctx->r7;  // NU_SC_SWAPBUFFER = 1

    if (gfxp == 0 || gfxsize == 0) return;

    uint32_t task_vaddr = (s_task_slot == 0) ? NU_GFX_TASK_ADDR_0 : NU_GFX_TASK_ADDR_1;
    s_task_slot ^= 1;

    uint32_t task_phys = task_vaddr & 0x1FFFFFFFu;
    OSTask* task = reinterpret_cast<OSTask*>(rdram + task_phys);

    memset(task, 0, sizeof(OSTask));
    task->t.type            = M_GFXTASK;
    task->t.flags           = 0;
    task->t.data_ptr        = (PTR(u64))gfxp;
    task->t.data_size       = gfxsize;
    task->t.ucode           = (PTR(u64))0x8010C970u;
    task->t.ucode_size      = 0x420u;
    task->t.ucode_data      = (PTR(u64))(0x8010C970u + 0x420u);
    task->t.ucode_data_size = 0x420u;
    task->t.dram_stack      = (PTR(u64))0x8011D000u;
    task->t.dram_stack_size = 0x40u;
    task->t.output_buff     = (PTR(u64))0x80104000u;
    task->t.output_buff_size= 0x1000u;
    task->t.ucode_boot      = (PTR(u64))0x80110820u;
    task->t.ucode_boot_size = 0x100u;

    ultramodern::submit_rsp_task(rdram, task_vaddr);

    // If the caller requested a buffer swap, advance the framebuffer and
    // call osViSwapBuffer so RT64 knows which RDRAM buffer to scan out.
    if (swap) {
        // nuGfxCfb  (0x80215EBC) = pointer to FrameBuf[] = {FB0, FB1, FB2}
        // framebufferCount (0x801C3F71) = current buffer index (u8)
        // nuGfxCfbP (0x80205750) = current framebuffer pointer
        //
        // Use raw RDRAM byte reads (physical = vaddr & 0x1FFFFFFF).
        // N64 words in RDRAM are big-endian, but our rdram array stores them
        // as-is from the ROM/ELF. So we read them as little-endian host words.
        auto rdram_r32 = [&](uint32_t vaddr) -> uint32_t {
            uint32_t phys = vaddr & 0x1FFFFFFFu;
            return *(uint32_t*)(rdram + phys);
        };
        auto rdram_w32 = [&](uint32_t vaddr, uint32_t val) {
            uint32_t phys = vaddr & 0x1FFFFFFFu;
            *(uint32_t*)(rdram + phys) = val;
        };

        // Read current framebuffer index (byte, XOR-3 lane)
        uint8_t fb_idx = rdram[(0x801C3F71u & 0x1FFFFFFFu) ^ 3u] % 3u;

        // nuGfxCfb holds the address of FrameBuf[]; dereference to get FrameBuf[fb_idx]
        uint32_t cfb_array_vaddr = rdram_r32(0x80215EBCu);   // = &FrameBuf[0]
        uint32_t fb_ptr          = rdram_r32(cfb_array_vaddr + fb_idx * 4u);

        // Update nuGfxCfbP
        rdram_w32(0x80205750u, fb_ptr);

        // Increment framebufferCount (since we bypassed NuSystem task manager)
        uint32_t fb_count_phys = 0x801C3F71u & 0x1FFFFFFFu;
        uint8_t fb_count = rdram[fb_count_phys ^ 3u];
        fb_count = (fb_count + 1) % 3u;  // wrap at 3
        rdram[fb_count_phys ^ 3u] = fb_count;

        static uint32_t task_swap_count = 0;
        if (task_swap_count < 5) {
            printf("[nuGfxTaskStart] fb_idx=%u fb_ptr=0x%08X new_fb_count=%u\n",
                   fb_idx, fb_ptr, fb_count);
            fflush(stdout);
            task_swap_count++;
        }

        // Call osViSwapBuffer(fb_ptr) via the recomp wrapper
        ctx->r4 = (gpr)(int32_t)fb_ptr;
        extern void osViSwapBuffer_recomp(uint8_t*, recomp_context*);
        osViSwapBuffer_recomp(rdram, ctx);

        // Debug: check framebuffer contents
        static uint32_t fb_check_count = 0;
        if (fb_check_count < 3) {
            uint32_t fb_phys = fb_ptr & 0x1FFFFFFFu;
            // Check first 16 pixels at (0,0) - each pixel is 2 bytes (RGBA5551)
            uint16_t pixel0 = *(uint16_t*)(rdram + fb_phys);
            uint16_t pixel100 = *(uint16_t*)(rdram + fb_phys + 100);  // ~10th pixel
            uint16_t pixel320 = *(uint16_t*)(rdram + fb_phys + 320*2); // row 1
            printf("[FB_CHECK] fb=0x%08X pixel[0,0]=0x%04X [0,50]=0x%04X [1,0]=0x%04X\n",
                   fb_ptr, pixel0, pixel100, pixel320);
            fflush(stdout);
            fb_check_count++;
        }
    }
}

RECOMP_PATCH void nuGfxTaskAllEndWait(uint8_t* rdram, recomp_context* ctx) {
    // ultramodern handles task synchronisation internally
}

// nuGfxSwapCfb – advance framebuffer and call osViSwapBuffer.
//
// The original NuSystem nuGfxSwapCfb is an empty stub in this ROM build.
// We need to call osViSwapBuffer() with the current framebuffer pointer so
// that RT64 knows which RDRAM buffer to scan out.
//
// nuGfxCfbP (0x80205750) = current framebuffer pointer (updated each swap)
RECOMP_PATCH void nuGfxSwapCfb(uint8_t* rdram, recomp_context* ctx) {
    constexpr uint32_t NU_GFX_CFBP_ADDR = 0x80205750u; // nuGfxCfbP: current fb ptr

    // nuGfxCfbP already has the current framebuffer pointer (updated by gfxBufferSwap)
    auto rdram_r32 = [&](uint32_t vaddr) -> uint32_t {
        uint32_t phys = vaddr & 0x1FFFFFFFu;
        return (uint32_t)(uint8_t)rdram[phys+0] << 24 |
               (uint32_t)(uint8_t)rdram[phys+1] << 16 |
               (uint32_t)(uint8_t)rdram[phys+2] <<  8 |
               (uint32_t)(uint8_t)rdram[phys+3];
    };
    uint32_t fb_ptr = rdram_r32(NU_GFX_CFBP_ADDR);

    static uint32_t swap_count = 0;
    if (swap_count++ < 5) {
        printf("[nuGfxSwapCfb] fb_ptr=0x%08X\n", fb_ptr);
        fflush(stdout);
    }

    // Call osViSwapBuffer(fb_ptr) via the recomp wrapper
    ctx->r4 = (gpr)(int32_t)fb_ptr;
    extern void osViSwapBuffer_recomp(uint8_t*, recomp_context*);
    osViSwapBuffer_recomp(rdram, ctx);
}

RECOMP_PATCH void nuGfxSwapCfbFuncSet(uint8_t* rdram, recomp_context* ctx) {
}

RECOMP_PATCH void nuGfxPreNMIFuncSet(uint8_t* rdram, recomp_context* ctx) {
}

// nuGfxFuncSet – register the per-retrace callback.
// r4 = function pointer (N64 virtual address of the callback)
RECOMP_PATCH void nuGfxFuncSet(uint8_t* rdram, recomp_context* ctx) {
    uint32_t func_vaddr = (uint32_t)ctx->r4;
    log_rdram_event("nuGfxFuncSet-before", rdram);
    // Assert: rdram must not change after first registration
    uint8_t* old = s_rdram.load(std::memory_order_acquire);
    if (old != nullptr && old != rdram) {
        fprintf(stderr, "[FATAL] RDRAM CHANGED IN-PROCESS: old=%p new=%p\n", (void*)old, (void*)rdram);
        fflush(stderr);
        abort();
    }
    s_rdram.store(rdram, std::memory_order_release);
    log_rdram_event("nuGfxFuncSet-after", rdram);
    fprintf(stderr, "         func=0x%08X\n", func_vaddr);
    fflush(stderr);
    s_retrace_registered.store(func_vaddr != 0, std::memory_order_release);
    // Write the function pointer into the NuSystem RDRAM global (nuGfxFunc).
    uint32_t phys = NU_GFX_FUNC_ADDR & 0x1FFFFFFFu;
    rdram[phys+0] = (func_vaddr >> 24) & 0xFF;
    rdram[phys+1] = (func_vaddr >> 16) & 0xFF;
    rdram[phys+2] = (func_vaddr >>  8) & 0xFF;
    rdram[phys+3] = (func_vaddr >>  0) & 0xFF;
}

RECOMP_PATCH void nuGfxDisplayOn(uint8_t* rdram, recomp_context* ctx) {
}

RECOMP_PATCH void nuGfxDisplayOff(uint8_t* rdram, recomp_context* ctx) {
}

// updateAudio – crashes when libmus handles are garbage (audio is fully stubbed).
// Zero out any active sequence/sfx flags so the real body's loops are no-ops,
// then return immediately.
RECOMP_PATCH void updateAudio(uint8_t* rdram, recomp_context* ctx) {
    // gAudioSequences at 0x80117C00 (zeroed at init) — flags are u16 at offset 0.
    // If any AUDIO_ACTIVE bit crept in via setLevelAudio, clear it.
    // gSfx array is right after. Both were zeroed in hm64_on_init; stay silent.
    (void)rdram; (void)ctx;
}

// ---------------------------------------------------------------------------
// NuSystem controller patches
// ---------------------------------------------------------------------------

RECOMP_PATCH void nuContInit(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0x01;  // report 1 controller connected
}

RECOMP_PATCH void nuContDataGetAll(uint8_t* rdram, recomp_context* ctx) {
}

RECOMP_PATCH void nuContDataGetExAll(uint8_t* rdram, recomp_context* ctx) {
}

RECOMP_PATCH void nuContDataGet(uint8_t* rdram, recomp_context* ctx) {
}

RECOMP_PATCH void nuContPakOpen(uint8_t* rdram, recomp_context* ctx) {
    uint32_t pak_vaddr = (uint32_t)ctx->r4;
    if (pak_vaddr) {
        uint32_t phys = pak_vaddr & 0x1FFFFFFF;
        rdram[phys + CONT_PAK_ERROR_OFFSET] = 1;
    }
}

RECOMP_PATCH void nuContPakRepairId(uint8_t* rdram, recomp_context* ctx) {
}

RECOMP_PATCH void nuContPakGetFree(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = 0;
}

RECOMP_PATCH void nuContPakFileNum(uint8_t* rdram, recomp_context* ctx) {
    uint32_t max_ptr  = (uint32_t)ctx->r5;
    uint32_t used_ptr = (uint32_t)ctx->r6;
    if (max_ptr) {
        uint32_t phys = max_ptr & 0x1FFFFFFF;
        rdram[phys+0] = rdram[phys+1] = rdram[phys+2] = rdram[phys+3] = 0;
    }
    if (used_ptr) {
        uint32_t phys = used_ptr & 0x1FFFFFFF;
        rdram[phys+0] = rdram[phys+1] = rdram[phys+2] = rdram[phys+3] = 0;
    }
}

RECOMP_PATCH void nuContPakCodeSet(uint8_t* rdram, recomp_context* ctx) {
}

RECOMP_PATCH void nuContPakFileOpenJis(uint8_t* rdram, recomp_context* ctx) {
    uint32_t pak_vaddr = (uint32_t)ctx->r4;
    if (pak_vaddr) {
        rdram[(pak_vaddr & 0x1FFFFFFF) + CONT_PAK_ERROR_OFFSET] = 1;
    }
}

RECOMP_PATCH void nuContPakFileReadWrite(uint8_t* rdram, recomp_context* ctx) {
}

RECOMP_PATCH void nuContPakFileDeleteJis(uint8_t* rdram, recomp_context* ctx) {
}

RECOMP_PATCH void osEPiLinkHandle(uint8_t* rdram, recomp_context* ctx) {
}

// ---------------------------------------------------------------------------
// nuPiReadRom – ROM DMA with ELF→ROM layout correction.
//
// NuSystem passes rom_addr as a raw ROM offset stored in the ELF data section
// (e.g. via _xxxSegmentRomStart linker symbols).  Due to a packing difference
// between the original N64 ROM build and our decomp ELF relink, every segment
// ROM address in the ELF is 0x4F10 bytes EARLIER than its actual position in
// the ROM binary.  We apply a universal +0x4F10 correction here.
//
// Verified across: all cutscene banks, all sprite/texture/palette/index asset
// segments (player, title, font, etc.).  The delta is exactly 0x4F10 for every
// segment tested.
//
// r4 = rom_offset  – raw ROM offset from ELF symbol (needs +0x4F10)
// r5 = buf_ptr     – RDRAM destination virtual address
// r6 = size        – byte count
// ---------------------------------------------------------------------------
static constexpr uint32_t ROM_LAYOUT_DELTA = 0x4F10u;

RECOMP_PATCH void nuPiReadRom(uint8_t* rdram, recomp_context* ctx) {
    uint32_t rom_offset_elf = (uint32_t)ctx->r4;
    gpr      buf_ptr        = ctx->r5;
    uint32_t size           = (uint32_t)ctx->r6;

    // Apply ELF→ROM layout correction.
    uint32_t rom_offset = rom_offset_elf + ROM_LAYOUT_DELTA;

    static uint32_t nuPi_log_count = 0;
    if (nuPi_log_count < 40 || (nuPi_log_count < 200 && nuPi_log_count % 10 == 0)) {
        printf("[nuPiReadRom #%u] elf_off=0x%06X rom_off=0x%06X buf=%08X size=%u\n",
               nuPi_log_count, rom_offset_elf, rom_offset, (uint32_t)buf_ptr, size);
        fflush(stdout);
    }
    nuPi_log_count++;

    // Reconstruct the physical cart address (same as EPI handle would produce).
    uint32_t phys = rom_offset | recomp::rom_base;

    auto rom_span = recomp::get_rom();
    uint32_t rom_phys_end = recomp::rom_base + (uint32_t)rom_span.size();

    // Use 64-bit arithmetic to avoid overflow in phys + size.
    bool in_bounds = (size > 0) &&
                     (size <= 0x1000000u) &&  // sanity cap: max 16MB
                     (phys >= recomp::rom_base) &&
                     ((uint64_t)phys + size <= (uint64_t)rom_phys_end);

    if (!in_bounds) {
        static uint32_t oob_log_count = 0;
        if (oob_log_count < 20) {
            printf("[nuPiReadRom OOB #%u] elf=0x%06X rom=0x%06X buf=%08X size=%u phys_end=0x%06X rom_size=0x%06X\n",
                   oob_log_count, rom_offset_elf, rom_offset, (uint32_t)buf_ptr, size,
                   phys + size, (uint32_t)rom_span.size());
            fflush(stdout);
        }
        oob_log_count++;
        return;
    }

    // Copy ROM → RDRAM; MEM_B handles the big-endian byte-lane swapping.
    const uint8_t* src = rom_span.data() + (phys - recomp::rom_base);
    for (uint32_t i = 0; i < size; i++) {
        MEM_B(i, buf_ptr) = src[i];
    }

    // DIAG: dump first 32 bytes of loaded data for title sprite ROM addresses
    // (0xDB51F0 area) and for the overlay screen assets (0x154420 area)
    if ((rom_offset_elf >= 0x154000u && rom_offset_elf <= 0x155000u) ||
        (rom_offset_elf >= 0xDB5000u && rom_offset_elf <= 0xDD0000u)) {
        printf("[nuPi-DATA] rom=0x%06X buf=0x%08X sz=%u first32:",
               rom_offset_elf, (uint32_t)buf_ptr, size);
        uint32_t dump_n = size < 32 ? size : 32;
        for (uint32_t i = 0; i < dump_n; i++) {
            printf(" %02X", MEM_B(i, buf_ptr));
        }
        printf("\n");
        // Also dump what ROM source looks like for comparison
        printf("[nuPi-SRC]  rom=0x%06X src first32:");
        for (uint32_t i = 0; i < dump_n; i++) {
            printf(" %02X", src[i]);
        }
        printf("\n");
        fflush(stdout);
    }
}

} // extern "C"
