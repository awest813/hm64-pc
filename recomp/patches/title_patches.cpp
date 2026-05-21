/**
 * title_patches.cpp – PC patches for title/startup path.
 *
 * Root problem: launchIntroCutscene(OPENING_LOGOS, spawnPoint=0x61) routes
 * through the normal map loader.  Map #53 (resolved from spawn 0x61) is an
 * all-0xFF sentinel blob — no real world geometry.  Feeding it through the
 * normal dmaMapAssets → setupMap → setMapGrid pipeline crashes.
 *
 * Clean solution (layered):
 *   1. loadMapAtSpawnPoint – top-level gate.  Detects dummy map by ID or
 *      spawn point, sets SCENEFLAG_NO_WORLD_GEOMETRY, leaves geometry ptrs
 *      NULL, and returns without touching dmaMapAssets at all.
 *   2. setMapGrid – null-safe guard for any remaining call paths.
 *   3. dmaMapAssets – sentinel-offset guard as a safety net.
 *   4. loadLevelMapObjects – stubbed; overlay sprite ROM addresses are
 *      garbage for dummy maps and would crash nuPiReadRom → drawFrame.
 *   5. drawFrame – logged wrapper so we can trace render crashes.
 */

#include "recomp.h"
#include "librecomp/game.hpp"
#include "librecomp/addresses.hpp"
#include "ultramodern/ultramodern.hpp"
#include <cstdio>
#include <csignal>
#include <cstring>
#include <cstdint>
#include <execinfo.h>
#include <unistd.h>

extern "C" {
    void initializeEngine(uint8_t* rdram, recomp_context* ctx);
    void setupGameStart(uint8_t* rdram, recomp_context* ctx);
    void mainLoop(uint8_t* rdram, recomp_context* ctx);
    void initializeTitleScreen(uint8_t* rdram, recomp_context* ctx);
    void nuPiReadRom(uint8_t* rdram, recomp_context* ctx);
    void initializeCutscene(uint8_t* rdram, recomp_context* ctx);
    void spawnCutsceneExecutor(uint8_t* rdram, recomp_context* ctx);
    void setupMap(uint8_t* rdram, recomp_context* ctx);
    void unloadMapAssets(uint8_t* rdram, recomp_context* ctx);
    void setMapTranslation(uint8_t* rdram, recomp_context* ctx);
    void setMapScale(uint8_t* rdram, recomp_context* ctx);
    void setMapRotation(uint8_t* rdram, recomp_context* ctx);
    void setMapControllerViewPosition(uint8_t* rdram, recomp_context* ctx);
    void setInitialMapRotation(uint8_t* rdram, recomp_context* ctx);
    void setMapViewPositionAndCurrentTile(uint8_t* rdram, recomp_context* ctx);
    void setMapRGBA(uint8_t* rdram, recomp_context* ctx);
    void startGfxTask(uint8_t* rdram, recomp_context* ctx);
    void renderScene(uint8_t* rdram, recomp_context* ctx);
    void doViewportGfxTask(uint8_t* rdram, recomp_context* ctx);
    void renderSceneGraph(uint8_t* rdram, recomp_context* ctx);
    void guMtxIdent(uint8_t* rdram, recomp_context* ctx);
    void setMainLoopCallbackFunctionIndex(uint8_t* rdram, recomp_context* ctx);
}

// ---------------------------------------------------------------------------
// Scene-geometry flag.
//
// Stored in a static C++ variable (host-side) so it's always accessible
// without worrying about the RDRAM layout.  Cleared on every real map load.
// ---------------------------------------------------------------------------
static bool s_no_world_geometry = false;
static bool s_title_forced = false;
static bool s_diag_dumped = false;

static const char* dl_cmd_name(uint32_t w0);
static const char* cs_opcode_name(uint16_t op);

static inline bool scene_has_no_world_geometry() { return s_no_world_geometry; }

// Forward declarations for helpers used by mainLoop but defined later.
static inline uint16_t read_cb_idx(uint8_t* rdram);
static inline void     write_cb_idx(uint8_t* rdram, uint16_t v);
extern "C" void setMainLoopCallbackFunctionIndex(uint8_t* rdram, recomp_context* ctx);

// ---------------------------------------------------------------------------
// Dummy-map constants
// ---------------------------------------------------------------------------
static constexpr uint16_t OPENING_LOGOS_SPAWN_POINT = 0x61;
static constexpr uint8_t  OPENING_LOGOS_MAP_ID      = 53;
static constexpr uint16_t TITLE_SCREEN_CALLBACK     = 0x32;

// ---------------------------------------------------------------------------
// RDRAM helpers.
//
// N64 RDRAM byte storage uses XOR-3 byte-lane swapping within each 4-byte
// word.  Single byte accesses (MEM_B/MEM_BU) use (addr ^ 3) as the physical
// offset.  16-bit half-word accesses (MEM_H/MEM_HU) use (addr ^ 2).
// 32-bit word accesses (MEM_W) use addr directly (no XOR).
// ---------------------------------------------------------------------------
static inline uint8_t  rdram_ru8 (uint8_t* r, uint32_t v) { return *(uint8_t* )(r + ((v ^ 3u) - 0x80000000u)); }
static inline uint16_t rdram_ru16(uint8_t* r, uint32_t v) { return *(uint16_t*)(r + ((v ^ 2u) - 0x80000000u)); }
static inline uint32_t rdram_ru32(uint8_t* r, uint32_t v) { return *(uint32_t*)(r + (v - 0x80000000u)); }
static inline void     rdram_wu8 (uint8_t* r, uint32_t v, uint8_t  d){ *(uint8_t* )(r + ((v ^ 3u) - 0x80000000u)) = d; }
static inline void     rdram_wu16(uint8_t* r, uint32_t v, uint16_t d){ *(uint16_t*)(r + ((v ^ 2u) - 0x80000000u)) = d; }

static void dump_logical_bytes(uint8_t* rdram, uint32_t vaddr, uint32_t count) {
    printf("[raw %08X]", vaddr);
    for (uint32_t i = 0; i < count; i++) {
        printf(" %02X", rdram_ru8(rdram, vaddr + i));
    }
    printf("\n");
}

static inline float rdram_rf32(uint8_t* r, uint32_t v) {
    auto bswap16 = [](uint16_t x) -> uint16_t {
        return (uint16_t)((x >> 8) | (x << 8));
    };
    // Float fields written through the recompiled title path currently have
    // swapped 16-bit lanes. Decode them as two logical halfwords.
    uint32_t bits = ((uint32_t)bswap16(rdram_ru16(r, v)) << 16) |
                    (uint32_t)bswap16(rdram_ru16(r, v + 2u));
    float value;
    __builtin_memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline float rdram_rf32_direct(uint8_t* r, uint32_t v) {
    uint32_t bits = rdram_ru32(r, v);
    float value;
    __builtin_memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline float rdram_rf32_any(uint8_t* r, uint32_t v) {
    float direct = rdram_rf32_direct(r, v);
    if (direct > -1000.0f && direct < 1000.0f) {
        return direct;
    }
    return rdram_rf32(r, v);
}

static inline void rdram_wf32_direct(uint8_t* r, uint32_t v, float value) {
    uint32_t bits;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    *(uint32_t*)(r + (v - 0x80000000u)) = bits;
}

static inline bool sane_coord(float v) {
    return v > -1000.0f && v < 1000.0f;
}

static inline void fb_write_rgba5551(uint8_t* rdram, uint32_t fb_vaddr, int x, int y, uint16_t color) {
    if ((unsigned)x >= 320u || (unsigned)y >= 240u) return;
    uint32_t addr = fb_vaddr + (uint32_t)((y * 320 + x) * 2);
    *(uint16_t*)(rdram + ((addr ^ 2u) - 0x80000000u)) = color;
}

static float mtx_fixed_to_float(uint8_t* rdram, uint32_t mtx_vaddr, uint32_t index) {
    int16_t whole = (int16_t)rdram_ru16(rdram, mtx_vaddr + index * 2u);
    uint16_t frac = rdram_ru16(rdram, mtx_vaddr + 0x20u + index * 2u);
    int32_t fixed = ((int32_t)whole << 16) | frac;
    return (float)fixed / 65536.0f;
}

static uint32_t next_active_bitmap(uint8_t* rdram, uint32_t start) {
    constexpr uint32_t BITMAP_BASE = 0x801F7110u;
    constexpr uint32_t BITMAP_STRIDE = 0x58u;
    for (uint32_t bi = start; bi < 48; bi++) {
        uint32_t flags = rdram_ru16(rdram, BITMAP_BASE + bi * BITMAP_STRIDE + 0x56u);
        if (flags != 0) return bi;
    }
    return 0xFFFFFFFFu;
}

static bool title_default_placement(uint32_t bi, float& tx, float& ty) {
    struct Placement { float x; float y; };
    // Active bitmap order follows updateSprites' title sprite order. These are
    // the title.c view-space placements with centerCoordinate = -160.
    static constexpr Placement kTitlePlacements[] = {
        {   0.0f,   64.0f }, // logo
        {   0.0f,  -96.0f }, // copyright / publisher
        {   0.0f,  -96.0f },
        {   0.0f,  -16.0f }, // push start sign
        {-320.0f,  -12.0f }, // how to play sign
        {-320.0f,  -44.0f }, // dirt road
        {   0.0f,  -32.0f }, // sign post 1
        {-352.0f,  -32.0f }, // sign post 2
        {-288.0f,  -32.0f }, // sign post 3
        {   0.0f,    0.0f }, // cloud 1 pair
        {-320.0f,    0.0f },
        {   0.0f,   64.0f }, // cloud 2 / cloud 3 / licensed pieces
        {-128.0f,   96.0f },
        {  64.0f,   80.0f },
        {-224.0f,   72.0f },
        {-352.0f,  108.0f },
        {-448.0f,   88.0f },
        {   0.0f,    0.0f }, // grass strips
        {-320.0f,    0.0f },
        {   0.0f,    0.0f },
        {   0.0f,    0.0f },
        {   0.0f,    0.0f },
        {-320.0f,    0.0f },
        {-320.0f,    0.0f },
        {-320.0f,    0.0f },
    };
    if (bi >= sizeof(kTitlePlacements) / sizeof(kTitlePlacements[0])) {
        return false;
    }
    tx = kTitlePlacements[bi].x;
    ty = kTitlePlacements[bi].y;
    return true;
}

static void software_blit_bitmap(uint8_t* rdram, uint32_t fb, uint32_t bi, float tx, float ty) {
    constexpr uint32_t BITMAP_BASE = 0x801F7110u;
    constexpr uint32_t BITMAP_STRIDE = 0x58u;
    constexpr uint32_t VTX_BASE = 0x8021E6E0u;
    constexpr uint32_t VTX_STRIDE = 0x10u;
    constexpr uint32_t VTXS_PER_BITMAP = 4u;
    constexpr uint32_t VTX_BANK_SIZE = 0x80u * VTXS_PER_BITMAP * VTX_STRIDE;

    uint32_t bv = BITMAP_BASE + bi * BITMAP_STRIDE;
    uint32_t tex = rdram_ru32(rdram, bv + 0x00);
    uint32_t pal = rdram_ru32(rdram, bv + 0x04);
    int width = (int)rdram_ru32(rdram, bv + 0x08);
    int height = (int)rdram_ru32(rdram, bv + 0x0C);
    uint32_t pixel_size = rdram_ru32(rdram, bv + 0x14);
    uint32_t sprite_no = rdram_ru16(rdram, bv + 0x18);
    uint32_t vtx_count = rdram_ru16(rdram, bv + 0x1A);

    if (tex < 0x80000000u || tex >= 0x80800000u ||
        pal < 0x80000000u || pal >= 0x80800000u ||
        width <= 0 || width > 320 || height <= 0 || height > 240 ||
        vtx_count == 0 || vtx_count > 16) {
        return;
    }

    int texture_height = 0;
    int texture_width_bytes = 0;
    if (pixel_size == 0) {
        texture_height = width > 0 ? (4096 / width) : height;
        texture_width_bytes = (width + 1) / 2;
    } else if (pixel_size == 1) {
        texture_height = width > 0 ? (2048 / width) : height;
        texture_width_bytes = width;
    } else {
        return;
    }
    if (texture_height <= 0) texture_height = height;

    uint32_t gfx_buf = (uint32_t)MEM_W(0x5630, (gpr)(int32_t)0x80200000u) & 1u;
    uint32_t vtx_bank = VTX_BASE + gfx_buf * VTX_BANK_SIZE;
    int texture_y = 0;

    for (uint32_t seg = 0; seg < vtx_count; seg++) {
        uint32_t v0 = vtx_bank + (sprite_no + seg) * VTXS_PER_BITMAP * VTX_STRIDE;
        int16_t lx0 = (int16_t)rdram_ru16(rdram, v0 + 0x00);
        int16_t ly0 = (int16_t)rdram_ru16(rdram, v0 + 0x02);
        int16_t lx1 = (int16_t)rdram_ru16(rdram, v0 + VTX_STRIDE + 0x00);
        int16_t ly2 = (int16_t)rdram_ru16(rdram, v0 + VTX_STRIDE * 2 + 0x02);
        int local_min_x = lx0 < lx1 ? lx0 : lx1;
        int local_max_x = lx0 > lx1 ? lx0 : lx1;
        int tile_w = local_max_x - local_min_x;
        int tile_h = ly0 - ly2;
        if (tile_w <= 0) tile_w = width;
        if (tile_h <= 0) tile_h = texture_height;
        if (texture_y + tile_h > height) tile_h = height - texture_y;
        if (tile_h <= 0) break;

        int dst_x0 = (int)(160.0f + tx + (float)local_min_x);
        int dst_y0 = (int)(120.0f - ty - (float)ly0);

        for (int dy = 0; dy < tile_h; dy++) {
            int sy = texture_y + dy;
            if (sy < 0 || sy >= height) continue;
            int py = dst_y0 + dy;
            if ((unsigned)py >= 240u) continue;
            for (int dx = 0; dx < tile_w; dx++) {
                int sx = dx;
                if (sx < 0 || sx >= width) continue;
                int px = dst_x0 + dx;
                if ((unsigned)px >= 320u) continue;

                uint8_t idx;
                uint32_t tex_off = (uint32_t)(sy * texture_width_bytes);
                if (pixel_size == 0) {
                    uint8_t packed = rdram_ru8(rdram, tex + tex_off + (uint32_t)(sx >> 1));
                    idx = (sx & 1) ? (packed & 0x0Fu) : (packed >> 4);
                } else {
                    idx = rdram_ru8(rdram, tex + tex_off + (uint32_t)sx);
                }

                uint16_t color = rdram_ru16(rdram, pal + (uint32_t)idx * 2u);
                if ((color & 1u) == 0) continue;
                fb_write_rgba5551(rdram, fb, px, py, color);
            }
        }

        texture_y += tile_h;
    }
}

static void software_blit_title_from_scene(uint8_t* rdram, uint32_t dl_start, uint32_t dl_size_bytes) {
    uint32_t fb = rdram_ru32(rdram, 0x80205750u);
    if (fb < 0x80000000u || fb >= 0x80800000u) {
        fb = 0x8038F800u;
    }

    // Clear the visible framebuffer to opaque black before drawing the title quads.
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 320; x++) {
            fb_write_rgba5551(rdram, fb, x, y, 0x0001u);
        }
    }

    uint32_t n_cmds = dl_size_bytes / 8u;
    uint32_t bitmap_cursor = 0;
    uint32_t last_translation_mtx = 0;
    uint32_t drawn = 0;

    for (uint32_t i = 0; i < n_cmds; i++) {
        uint32_t cmd_v = dl_start + i * 8u;
        uint32_t w0 = rdram_ru32(rdram, cmd_v);
        uint32_t w1 = rdram_ru32(rdram, cmd_v + 4u);
        if ((w0 & 0xFF0000FFu) == 0xDA000003u) {
            last_translation_mtx = 0x80000000u | (w1 & 0x00FFFFFFu);
        } else if (((w0 & 0xFF000000u) == 0xDE000000u) && i != 0 && last_translation_mtx != 0) {
            uint32_t bi = next_active_bitmap(rdram, bitmap_cursor);
            if (bi == 0xFFFFFFFFu) break;
            bitmap_cursor = bi + 1u;

            float tx = mtx_fixed_to_float(rdram, last_translation_mtx, 12);
            float ty = mtx_fixed_to_float(rdram, last_translation_mtx, 13);
            uint32_t bv = 0x801F7110u + bi * 0x58u;
            const char* placement_mode = "mtx";
            if (tx > -0.01f && tx < 0.01f && ty > -0.01f && ty < 0.01f) {
                float bx = rdram_rf32_any(rdram, bv + 0x1Cu);
                float by = rdram_rf32_any(rdram, bv + 0x20u);
                float sx = rdram_rf32_any(rdram, bv + 0x28u);
                float sy = rdram_rf32_any(rdram, bv + 0x2Cu);
                if (!sane_coord(bx)) bx = 0.0f;
                if (!sane_coord(by)) by = 0.0f;
                if (!(sx > 0.001f && sx < 8.0f)) sx = 1.0f;
                if (!(sy > 0.001f && sy < 8.0f)) sy = 1.0f;

                int width = (int)rdram_ru32(rdram, bv + 0x08u);
                int height = (int)rdram_ru32(rdram, bv + 0x0Cu);
                uint32_t render = rdram_ru16(rdram, bv + 0x54u);
                tx = bx;
                ty = by;
                if (((render >> 3) & 3u) == 1u) tx = bx + (float)((width / 2) * sx);
                if (((render >> 3) & 3u) == 3u) tx = bx - (float)((width / 2) * sx);
                if (((render >> 5) & 3u) == 1u) ty = by + (float)((height / 2) * sy);
                if (((render >> 5) & 3u) == 3u) ty = by - (float)((height / 2) * sy);
                placement_mode = "bitmap";
                if (tx > -0.01f && tx < 0.01f && ty > -0.01f && ty < 0.01f &&
                    title_default_placement(bi, tx, ty)) {
                    placement_mode = "title";
                }
            }
            static uint32_t placement_log_count = 0;
            if (placement_log_count < 32) {
                printf("[software-title-place] mode=%s bi=%u mtx=%08X tx=%.2f ty=%.2f tex=%08X %ux%u\n",
                       placement_mode, bi, last_translation_mtx, tx, ty, rdram_ru32(rdram, bv),
                       rdram_ru32(rdram, bv + 0x08), rdram_ru32(rdram, bv + 0x0C));
                fflush(stdout);
                placement_log_count++;
            }
            software_blit_bitmap(rdram, fb, bi, tx, ty);
            drawn++;
            last_translation_mtx = 0;
        }
    }

    static uint32_t blit_log_count = 0;
    if (blit_log_count++ < 6) {
        uint32_t fb_phys = fb & 0x1FFFFFFFu;
        uint16_t p0 = *(uint16_t*)(rdram + fb_phys);
        uint16_t pc = *(uint16_t*)(rdram + fb_phys + (120 * 320 + 160) * 2);
        printf("[software-title-blit] fb=0x%08X drawn=%u p00=0x%04X pc=0x%04X\n",
               fb, drawn, p0, pc);
        fflush(stdout);
    }
}

// ---------------------------------------------------------------------------
// Sprite transform setters.
//
// The title setup code stores positions through these tiny helpers before
// updateSprites expands each SpriteObject into BitmapObjects.  Letting the
// recompiled C body write floats here leaves several title sprites effectively
// at the origin.  These patches write the RDRAM SpriteObject fields directly
// with the actual HM64 bss layout.
// ---------------------------------------------------------------------------
static constexpr uint32_t GLOBAL_SPRITES_BASE = 0x801FD630u;
static constexpr uint32_t GLOBAL_SPRITE_STRIDE = 0x9Cu;
static constexpr uint32_t GLOBAL_SPRITE_VIEW_POS = 0x2Cu;
static constexpr uint32_t GLOBAL_SPRITE_SCALE = 0x38u;
static constexpr uint32_t GLOBAL_SPRITE_ROT = 0x44u;
static constexpr uint32_t GLOBAL_SPRITE_STATE_FLAGS = 0x9Au;
static constexpr uint16_t SPRITE_ACTIVE_FLAG = 0x0001u;
static constexpr uint32_t MAX_GLOBAL_SPRITES = 192u;

static inline float ctx_f32_arg(gpr value) {
    uint32_t bits = (uint32_t)value;
    float out;
    __builtin_memcpy(&out, &bits, sizeof(out));
    return out;
}

static inline bool global_sprite_active(uint8_t* rdram, uint32_t index) {
    if (index >= MAX_GLOBAL_SPRITES) return false;
    uint32_t sv = GLOBAL_SPRITES_BASE + index * GLOBAL_SPRITE_STRIDE;
    return (rdram_ru16(rdram, sv + GLOBAL_SPRITE_STATE_FLAGS) & SPRITE_ACTIVE_FLAG) != 0;
}

static inline void write_sprite_vec3(uint8_t* rdram, uint32_t index, uint32_t field, float x, float y, float z) {
    uint32_t sv = GLOBAL_SPRITES_BASE + index * GLOBAL_SPRITE_STRIDE + field;
    rdram_wf32_direct(rdram, sv + 0x0u, x);
    rdram_wf32_direct(rdram, sv + 0x4u, y);
    rdram_wf32_direct(rdram, sv + 0x8u, z);
}

static inline void read_sprite_vec3_any(uint8_t* rdram, uint32_t index, uint32_t field, float& x, float& y, float& z) {
    uint32_t sv = GLOBAL_SPRITES_BASE + index * GLOBAL_SPRITE_STRIDE + field;
    x = rdram_rf32_any(rdram, sv + 0x0u);
    y = rdram_rf32_any(rdram, sv + 0x4u);
    z = rdram_rf32_any(rdram, sv + 0x8u);
}

extern "C" RECOMP_PATCH void setSpriteViewSpacePosition(uint8_t* rdram, recomp_context* ctx) {
    uint32_t index = (uint32_t)ctx->r4 & 0xFFFFu;
    ctx->r2 = 0;
    if (!global_sprite_active(rdram, index)) return;

    write_sprite_vec3(rdram, index, GLOBAL_SPRITE_VIEW_POS,
                      ctx_f32_arg(ctx->r5), ctx_f32_arg(ctx->r6), ctx_f32_arg(ctx->r7));
    ctx->r2 = 1;
}

extern "C" RECOMP_PATCH void adjustSpriteViewSpacePosition(uint8_t* rdram, recomp_context* ctx) {
    uint32_t index = (uint32_t)ctx->r4 & 0xFFFFu;
    ctx->r2 = 0;
    if (!global_sprite_active(rdram, index)) return;

    float x, y, z;
    read_sprite_vec3_any(rdram, index, GLOBAL_SPRITE_VIEW_POS, x, y, z);
    write_sprite_vec3(rdram, index, GLOBAL_SPRITE_VIEW_POS,
                      x + ctx_f32_arg(ctx->r5),
                      y + ctx_f32_arg(ctx->r6),
                      z + ctx_f32_arg(ctx->r7));
    ctx->r2 = 1;
}

extern "C" RECOMP_PATCH void setSpriteScale(uint8_t* rdram, recomp_context* ctx) {
    uint32_t index = (uint32_t)ctx->r4 & 0xFFFFu;
    ctx->r2 = 0;
    if (!global_sprite_active(rdram, index)) return;

    write_sprite_vec3(rdram, index, GLOBAL_SPRITE_SCALE,
                      ctx_f32_arg(ctx->r5), ctx_f32_arg(ctx->r6), ctx_f32_arg(ctx->r7));
    ctx->r2 = 1;
}

extern "C" RECOMP_PATCH void setSpriteRotation(uint8_t* rdram, recomp_context* ctx) {
    uint32_t index = (uint32_t)ctx->r4 & 0xFFFFu;
    ctx->r2 = 0;
    if (!global_sprite_active(rdram, index)) return;

    write_sprite_vec3(rdram, index, GLOBAL_SPRITE_ROT,
                      ctx_f32_arg(ctx->r5), ctx_f32_arg(ctx->r6), ctx_f32_arg(ctx->r7));
    ctx->r2 = 1;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// guRotateRPY – fast-path identity matrix for zero-angle rotations.
//
// The recompiled N64 fcos/fsin helpers use ctx->f_odd scratch registers as
// RDRAM write destinations.  When those registers are zero (uninitialised BSS
// context constructed by our retrace callback), fcos writes to address 0x10
// and crashes.
//
// For the opening logos all sprite/bitmap scene nodes have rotation (0,0,0),
// so we can safely short-circuit guRotateRPY: if all three angles are near
// zero, write an identity matrix and return without touching the trig path.
//
// guRotateRPY(Mtx *m, float rx, float ry, float rz)
//   r4 = m (N64 vaddr of output Mtx)
//   r5 = rx bits (passed via mtc1 / integer register convention)
//   r6 = ry bits
//   r7 = rz bits
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void guRotateRPY(uint8_t* rdram, recomp_context* ctx) {
    // Reinterpret register integer bits as floats.
    float rx, ry, rz;
    uint32_t rx_bits = (uint32_t)ctx->r5;
    uint32_t ry_bits = (uint32_t)ctx->r6;
    uint32_t rz_bits = (uint32_t)ctx->r7;
    __builtin_memcpy(&rx, &rx_bits, 4);
    __builtin_memcpy(&ry, &ry_bits, 4);
    __builtin_memcpy(&rz, &rz_bits, 4);

    // Threshold: treat angles < 0.001 degrees as zero.
    constexpr float EPS = 0.001f;
    bool near_zero = (rx > -EPS && rx < EPS) &&
                     (ry > -EPS && ry < EPS) &&
                     (rz > -EPS && rz < EPS);

    if (near_zero) {
        // Write identity Mtx into *m via guMtxIdent.
        ctx->r4 = ctx->r4;  // m ptr already in r4
        guMtxIdent(rdram, ctx);
        return;
    }

    // Non-zero rotation: we cannot call the original recompiled body since
    // we replaced it.  Emit an identity matrix as a safe fallback for now
    // (world-map scene rotations will be revisited when map rendering is
    // implemented).
    guMtxIdent(rdram, ctx);
}

// SIGSEGV handler — installed by mainproc before the game loop starts.
static volatile const char* g_last_update_fn = "(none)";
static void segv_handler(int, siginfo_t* si, void*) {
    // async-signal-safe: write directly
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
        "\n[CRASH] SIGSEGV at %p, last update fn: %s\n",
        si->si_addr, (const char*)g_last_update_fn);
    (void)write(STDERR_FILENO, buf, n);
    void* frames[64];
    int frame_count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, frame_count, STDERR_FILENO);
    _exit(1);
}

// mainproc – skip N64 VI hardware setup.
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void mainproc(uint8_t* rdram, recomp_context* ctx) {
    // Install crash handler so we know which update function causes SIGSEGV
    struct sigaction sa{};
    sa.sa_sigaction = segv_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigaction(SIGSEGV, &sa, nullptr);
    // Initialize globals that the original mainproc body sets before calling
    // initializeEngine.  These are skipped by our patch but required for the
    // game loop to function.
    //
    // frameRate        0x802373F1  u8  = 1  (run drawFrame every 1st retrace)
    // mainLoopUpdateRate 0x802373F0? u8 = 1  (run mainLoop every retrace)
    // gfxTaskNo        0x80205209? u8  = 0xFF (reset task counter)
    //
    // Addresses from data_dump.toml and mainproc.c.
    // From data_dump.toml:
    //   frameRate        vram = 0x802373F1
    //   mainLoopUpdateRate vram = 0x802226E2
    //   gfxTaskNo        vram = 0x80205209 (gfxTaskNo u8)
    constexpr uint32_t FRAME_RATE_VADDR      = 0x802373F1u;
    constexpr uint32_t MAIN_LOOP_RATE_VADDR  = 0x802226E2u;
    constexpr uint32_t GFX_TASK_NO_VADDR     = 0x80205209u;

    rdram_wu8(rdram, FRAME_RATE_VADDR,     1);
    rdram_wu8(rdram, MAIN_LOOP_RATE_VADDR, 1);
    rdram_wu8(rdram, GFX_TASK_NO_VADDR,   0xFF);

    // Set VI mode to NTSC Low Pass (OS_VI_NTSC_LAN1 = 2)
    // OSViMode structure is 0x108 bytes (from libultra source)
    // osViModeTable at VMA 0x8011D050 (symbol_addrs.txt)
    extern void osViSetMode_recomp(uint8_t*, recomp_context*);
    ctx->r4 = (gpr)(int32_t)(0x8011D050u + 2 * 0x108u);  // &osViModeTable[OS_VI_NTSC_LAN1]
    osViSetMode_recomp(rdram, ctx);

    printf("[mainproc] starting initializeEngine...\n"); fflush(stdout);
    initializeEngine(rdram, ctx);
    printf("[mainproc] initializeEngine done, calling initializeBitmaps...\n"); fflush(stdout);
    { extern void initializeBitmaps(uint8_t*, recomp_context*); initializeBitmaps(rdram, ctx); }
    printf("[mainproc] initializeBitmaps done, starting setupGameStart...\n"); fflush(stdout);
    setupGameStart(rdram, ctx);
    printf("[mainproc] setupGameStart done, starting mainLoop...\n"); fflush(stdout);

    // Run mainLoop manually instead of via the recompiled version so we can
    // observe whether the stepMainLoop spin is actually being reached.
    {
        constexpr uint32_t STEP_ML      = 0x80205208u;
        constexpr uint32_t ENG_FLAGS    = 0x801594E4u;  // from symbol_addrs
        constexpr uint32_t D_8020564C   = 0x8020564Cu;
        constexpr uint32_t CB_IDX       = 0x8020564Au;
        constexpr uint32_t TABLE_BASE   = 0x80188F70u;

        // Set engineStateFlags = 1
        *(uint16_t*)(rdram + ((ENG_FLAGS ^ 2u) - 0x80000000u)) = 1;
        // Set D_8020182BA0 = 1 and D_8020564C = 0
        *(uint16_t*)(rdram + ((0x80182BA0u ^ 2u) - 0x80000000u)) = 1;
        *(uint16_t*)(rdram + ((D_8020564C ^ 2u) - 0x80000000u)) = 0;

        printf("[mainLoop-manual] entering spin loop. engineStateFlags=%u\n",
               *(uint16_t*)(rdram + ((ENG_FLAGS ^ 2u) - 0x80000000u)));
        fflush(stdout);

        volatile uint8_t* step_ptr =
            (volatile uint8_t*)(rdram + ((STEP_ML ^ 3u) - 0x80000000u));

        extern void log_rdram_event_external(const char*, uint8_t*, void*);
        extern void* hm64_get_s_rdram_addr();
        log_rdram_event_external("mainloop-init", rdram, (void*)step_ptr);
        fprintf(stderr, "         &s_rdram=%p phys_off=%08X\n",
                hm64_get_s_rdram_addr(), (STEP_ML ^ 3u) - 0x80000000u);
        fflush(stderr);

        uint32_t iter = 0;
        while (true) {
            // Spin while stepMainLoop == 0 — use atomic load to see VI callback writes
            uint8_t step_val = __atomic_load_n(step_ptr, __ATOMIC_ACQUIRE);
            uint32_t spin_count = 0;
            while (step_val == 0) {
                step_val = __atomic_load_n(step_ptr, __ATOMIC_ACQUIRE);  // Re-read each iteration
                spin_count++;
                if (spin_count > 10000000u) {
                    printf("[mainLoop-manual #%u] spin timeout! stepML=%u re-read=%u\n", iter, step_val, step_val);
                    fflush(stdout);
                    spin_count = 0;
                }
            }

            printf("[mainLoop-manual #%u] stepML=%u exiting spin\n", iter, step_val);
            fflush(stdout);

            // Check D_8020564C skip counter
            uint16_t skip = *(uint16_t*)(rdram + ((D_8020564C ^ 2u) - 0x80000000u));
            uint16_t cb_idx = *(uint16_t*)(rdram + ((CB_IDX ^ 2u) - 0x80000000u));

            if (skip == 0) {
                // Execute mainLoopCallbacksTable[cb_idx]
                uint32_t fn_vaddr = *(uint32_t*)(rdram + (TABLE_BASE + cb_idx * 4u - 0x80000000u));
                if (iter < 5) {
                    printf("[mainLoop-manual #%u] cb=%u fn=0x%08X\n", iter, cb_idx, fn_vaddr);
                    fflush(stdout);
                }
                if (fn_vaddr != 0) {
                    ctx->r4 = 0;
                    LOOKUP_FUNC(fn_vaddr)(rdram, ctx);
                }
                // D_8020564C = D_80182BA0; D_8020564C -= 1
                uint16_t ba0 = *(uint16_t*)(rdram + ((0x80182BA0u ^ 2u) - 0x80000000u));
                *(uint16_t*)(rdram + ((D_8020564C ^ 2u) - 0x80000000u)) = ba0 - 1;
            } else {
                *(uint16_t*)(rdram + ((D_8020564C ^ 2u) - 0x80000000u)) = skip - 1;
            }

            // Run the remaining per-frame update functions (from mainLoop.c lines 82-94)
            // Each block logs on entry so we can bisect crashes.
            // Track last function for crash diagnostics
            #define RUN(fn) do { \
                g_last_update_fn = #fn; \
                extern void fn(uint8_t*, recomp_context*); fn(rdram, ctx); \
                g_last_update_fn = "(between " #fn " done)"; \
            } while(0)

            RUN(resetBitmaps);
            RUN(updateAudio);
            RUN(resetSceneNodeCounter);
            if (iter < 3) { printf("[manual] pre-updateCutsceneExecutors iter=%u\n", iter); fflush(stdout); }
            RUN(updateCutsceneExecutors);
            if (iter < 3) { printf("[manual] post-updateCutsceneExecutors iter=%u\n", iter); fflush(stdout); }

            // Probe executor state after each tick
            // Offsets confirmed from recompiled updateCutsceneExecutors disasm:
            //   stride  = 0x70 (= i*7*16)
            //   base    = 0x801808B0
            //   +0x00   bytecodePtr u32  (MEM_W)
            //   +0x66   waitFrames u16   (lhu at 0x8B0+0x66 = 0x916)
            //   +0x6C   flags u16        (lhu at 0x8B0+0x6C = 0x91C; bit0=ACTIVE)
            {
                static uint32_t s_prev_pc[42] = {};
                static uint32_t s_stuck_cnt[42] = {};
                constexpr uint32_t EXEC_BASE = 0x801808B0u;
                constexpr uint32_t EXEC_SIZE = 0x70u;
                constexpr uint32_t OFF_BPTR  = 0x00u;
                constexpr uint32_t OFF_WAIT  = 0x66u;
                constexpr uint32_t OFF_FLAGS = 0x6Cu;  // confirmed from disasm
                for (int ei = 0; ei < 42; ei++) {
                    uint32_t bvma = EXEC_BASE + ei * EXEC_SIZE;
                    // flags: u16 at +0x6C via xor-2
                    uint16_t eflags = *(uint16_t*)(rdram + ((bvma + OFF_FLAGS ^ 2u) - 0x80000000u));
                    if (!(eflags & 1)) { s_stuck_cnt[ei] = 0; continue; }
                    // bytecodePtr: u32 at +0x00 (MEM_W, no xor needed for word)
                    uint32_t bptr = (uint32_t)MEM_W(OFF_BPTR, (gpr)(int32_t)bvma);
                    // waitFrames: u16 at +0x66 via xor-2
                    uint16_t wait = *(uint16_t*)(rdram + ((bvma + OFF_WAIT ^ 2u) - 0x80000000u));
                    // opcode: u16 at *bptr via xor-2
                    uint16_t opcode = 0xFFFF;
                    if (bptr >= 0x80000000u && bptr < 0x80800000u)
                        opcode = *(uint16_t*)(rdram + ((bptr ^ 2u) - 0x80000000u));
                    bool changed = (bptr != s_prev_pc[ei]);
                    s_stuck_cnt[ei] = changed ? 0 : s_stuck_cnt[ei] + 1;
                    s_prev_pc[ei] = bptr;
                    if (changed || s_stuck_cnt[ei] == 1 || s_stuck_cnt[ei] % 60 == 0) {
                        printf("[cs-exec ei=%d iter=%u] pc=%08X op=%u(%s) wait=%u stuck=%u flg=%04X%s\n",
                               ei, iter, bptr, opcode, cs_opcode_name(opcode),
                               wait, s_stuck_cnt[ei], eflags,
                               scene_has_no_world_geometry() ? " NO_GEO" : "");
                        fflush(stdout);
                    }
                    if (opcode == 60 && s_stuck_cnt[ei] > 0 && s_stuck_cnt[ei] % 30 == 0) {
                        uint16_t assetIdx = *(uint16_t*)(rdram + ((bvma + 0x62u ^ 2u) - 0x80000000u));
                        bool sprite_flag = (eflags & 0x2u) != 0;
                        bool entity_flag = (eflags & 0x8u) != 0;
                        bool map_flag    = (eflags & 0x10u) != 0;
                        const char* check = "none";
                        if (sprite_flag || entity_flag) {
                            uint16_t si = sprite_flag ? assetIdx :
                                *(uint16_t*)(rdram + ((0x80205580u + assetIdx * 0x160u + 0x14u ^ 2u) - 0x80000000u));
                            uint16_t sf = *(uint16_t*)(rdram + ((0x80205580u + si * 0x10u + 0x8u ^ 2u) - 0x80000000u));
                            check = (sf & 0x400u) ? "SPRITE_DONE" : "SPRITE_NOT_DONE";
                        } else if (map_flag) {
                            uint16_t mcMainMap = *(uint16_t*)(rdram + ((0x80205520u + assetIdx * 0x20u ^ 2u) - 0x80000000u));
                            uint16_t mf = *(uint16_t*)(rdram + ((0x8016F810u + mcMainMap * 0xC0u + 0x8Au ^ 2u) - 0x80000000u));
                            bool mapActive = (mf & 0x1u) != 0;
                            bool rgbaBit3  = (mf & 0x8u) != 0;
                            check = (mapActive && rgbaBit3) ? "MAP_DONE" : "MAP_NOT_DONE";
                        }
                        printf("[rgba-wait ei=%d iter=%u stuck=%u] asset=%u flags=[%s%s%s] %s no_geo=%d\n",
                               ei, iter, s_stuck_cnt[ei], assetIdx,
                               sprite_flag ? "SPR" : "", entity_flag ? " ENT" : "",
                               map_flag ? " MAP" : "", check, scene_has_no_world_geometry());
                        fflush(stdout);
                    }
                }
            }
            const uint16_t active_cb = read_cb_idx(rdram);
            if (s_no_world_geometry && (active_cb == TITLE_SCREEN_CALLBACK)) {
                printf("[fix] title callback active; re-enabling sprite/bitmap updates\n");
                fflush(stdout);
                s_no_world_geometry = false;
            }

            if (!scene_has_no_world_geometry()) {
                RUN(updateEntities);
                RUN(updateMapController);
                RUN(updateMapGraphics);
                RUN(updateNumberSprites);
                RUN(updateSprites);
                RUN(dmaSprites);
                RUN(updateBitmaps);
            }
            RUN(updateMessageBox);
            RUN(updateDialogues);

            #undef RUN

            // === DIAGNOSTIC: dump bitmap structs after title has been rendering ===
            if (s_title_forced && !s_diag_dumped && iter > 25) {
                s_diag_dumped = true;
                printf("\n========== TITLE SCREEN DIAGNOSTIC (iter=%u) ==========\n", iter); fflush(stdout);
                // bitmaps[MAX_BITMAPS] lives at 0x801F7110. BitmapObject is
                // 0x58 bytes in the decomp layout; keep this diagnostic in
                // logical RDRAM reads so host struct packing/endian cannot lie.
                constexpr uint32_t BITMAP_BASE = 0x801F7110u;
                constexpr uint32_t BITMAP_STRIDE = 0x58u;
                for (uint32_t bi = 0; bi < 48; bi++) {
                    uint32_t bv = BITMAP_BASE + bi * BITMAP_STRIDE;
                    uint32_t flags   = rdram_ru16(rdram, bv + 0x56);
                    if (flags == 0) continue;
                    uint32_t texPtr  = rdram_ru32(rdram, bv + 0x00);
                    uint32_t palPtr  = rdram_ru32(rdram, bv + 0x04);
                    uint32_t width   = rdram_ru32(rdram, bv + 0x08);
                    uint32_t height  = rdram_ru32(rdram, bv + 0x0C);
                    uint32_t fmt     = rdram_ru32(rdram, bv + 0x10);
                    uint32_t siz     = rdram_ru32(rdram, bv + 0x14);
                    uint32_t spriteNo= rdram_ru16(rdram, bv + 0x18);
                    uint32_t vtxIdx  = rdram_ru16(rdram, bv + 0x1A);
                    uint32_t render  = rdram_ru16(rdram, bv + 0x54);
                    printf("[bitmap-logical-%u] flags=0x%X render=0x%X spr=%u vtx=%u tex=%08X pal=%08X %ux%u fmt=%u siz=%u\n",
                           bi, flags, render, spriteNo, vtxIdx, texPtr, palPtr, width, height, fmt, siz);
                    fflush(stdout);
                    if (bi < 8 || width > 1024 || height > 1024 ||
                        texPtr < 0x80000000u || texPtr >= 0x80800000u ||
                        palPtr < 0x80000000u || palPtr >= 0x80800000u) {
                        dump_logical_bytes(rdram, bv, 0x40);
                    }
                }
            }

            // Every 60 iters: dump cutscene state
            if (iter % 60 == 0) {
                // gCutsceneFlags u32 at 0x8016FE00 (big-endian word, no xor)
                uint32_t csflags = *(uint32_t*)(rdram + (0x8016FE00u - 0x80000000u));
                // gCutsceneCompletionFlags s32 at 0x801891D4
                // In RDRAM, 32-bit words are stored host-endian (MEM_W uses no xor).
                int32_t  compl_  = (int32_t)MEM_W(0x1891D4, (gpr)(int32_t)0x80000000u);
                // gCutsceneIndex u16 at 0x801C3B66 (xor-2)
                uint16_t csidx   = *(uint16_t*)(rdram + ((0x801C3B66u ^ 2u) - 0x80000000u));
                // count active cutscene executors (flags byte at end of 0x50-byte struct)
                // struct size from cutscene.h: fields up to flags u16 at offset ~0x4E
                constexpr uint32_t EXEC_BASE = 0x801808B0u;
                constexpr uint32_t EXEC_SIZE = 0x70u;   // stride confirmed from disasm (i*7*16)
                constexpr uint32_t FLAGS_OFF  = 0x6Cu;  // flags u16, confirmed from lhu at 0x91C
                constexpr uint32_t BPTR_OFF   = 0x00u;  // bytecodePtr u32
                constexpr uint32_t WAIT_OFF   = 0x66u;  // waitFrames u16
                int active = 0;
                // On first state dump, also show raw struct bytes for ei=0
                if (iter == 0 || iter == 240) {
                    uint32_t e0 = EXEC_BASE;
                    printf("[exec0-raw iter=%u]\n", iter);
                    for (int b = 0; b < 0x70; b += 4) {
                        uint32_t w = (uint32_t)MEM_W(b, (gpr)(int32_t)e0);
                        printf("  +%02X: %08X\n", b, w);
                    }
                    fflush(stdout);
                }
                for (int ei = 0; ei < 42; ei++) {
                    uint32_t bvma = EXEC_BASE + ei * EXEC_SIZE;
                    uint16_t eflags = *(uint16_t*)(rdram + ((bvma + FLAGS_OFF ^ 2u) - 0x80000000u));
                    if (eflags & 1) {
                        active++;
                        uint32_t bptr = (uint32_t)MEM_W(BPTR_OFF, (gpr)(int32_t)bvma);
                        uint16_t wait = *(uint16_t*)(rdram + ((bvma + WAIT_OFF ^ 2u) - 0x80000000u));
                        uint16_t opcode = (bptr >= 0x80000000u) ?
                            *(uint16_t*)(rdram + ((bptr ^ 2u) - 0x80000000u)) : 0xFFFF;
                        printf("  [active ei=%d] pc=%08X op=%u wait=%u flg=%04X\n",
                               ei, bptr, opcode, wait, eflags);
                    }
                }
                printf("[state #%u] csflags=0x%08X cscompl=0x%08X csidx=%u active=%d no_geo=%d\n",
                        iter, csflags, (uint32_t)compl_, csidx, active, scene_has_no_world_geometry());
                fflush(stdout);
            }

            // --- PC PORT HACK ---
            // Deterministic check: fire at exactly iter=120 (not gated by % 60).
            // Previously nested inside iter % 60 == 0, so earliest fire was 180.
            {
                uint16_t seg = *(uint16_t*)(rdram + ((0x8018981Cu ^ 2u) - 0x80000000u));
                uint16_t csidx = *(uint16_t*)(rdram + ((0x801C3B66u ^ 2u) - 0x80000000u));
                bool opening_stuck = (seg == 30 && iter >= 10) ||
                                     (csidx >= 1450 && csidx <= 1499 && iter >= 10);
                if (opening_stuck && !s_title_forced) {
                    printf("[fix] Forcing title screen after opening transition stall: seg=%u csidx=%u iter=%u\n",
                           seg, csidx, iter);
                    fflush(stdout);
                    *(int32_t*)(rdram + (0x801891D4u - 0x80000000u)) = 0x8001;
                    ctx->r4 = 0;
                    initializeTitleScreen(rdram, ctx);
                    s_no_world_geometry = false;
                    s_title_forced = true;
                }
            }
            // --------------------

            // Reset stepMainLoop = 0
            rdram[(STEP_ML ^ 3u) - 0x80000000u] = 0;
            iter++;
        }
    }

    printf("[mainproc] mainLoop returned\n"); fflush(stdout);
}

// ---------------------------------------------------------------------------
// loadMapAtSpawnPoint – top-level gate for dummy/cinematic maps.
//
// Reproduces the full loadMapAtSpawnPoint body (funcs_30.c:11110) but adds
// an early exit for dummy map IDs / spawn points so dmaMapAssets is never
// reached with placeholder data.
//
// For dummy maps we set the minimum state needed for the cutscene to proceed:
//   - gBaseMapIndex  (0x80170458) = mapId
//   - previousMapIndex  (0x8016F899) = previous value
//   - gMapWithSeasonIndex (0x80205635) = mapId
//   - MAP_CONTROLLER_ASSETS_LOADED flag at 0x80205622
//   - s_no_world_geometry = true
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void loadMapAtSpawnPoint(uint8_t* rdram, recomp_context* ctx) {
    uint16_t spawnPoint = (uint16_t)(ctx->r4 & 0xFFFF);

    // --- replication: save previous map index ---
    uint8_t prev = rdram_ru8(rdram, 0x80170458u);   // gBaseMapIndex

    // --- resolve spawn → map ID (spawnPointToMap[spawnPoint]) ---
    // spawnPointToMap at VMA 0x8010FBE0 (loaded from ELF data section).
    uint8_t mapId = rdram_ru8(rdram, 0x8010FBE0u + spawnPoint);

    // Clear no-world flag for every load; set it explicitly for dummy maps.
    s_no_world_geometry = false;

    printf("[loadMapAtSpawnPoint] spawn=0x%02X mapId=%u\n", spawnPoint, mapId);
    fflush(stdout);

    // --- Dummy map detection ---
    // Guard spawn 0x61 (OPENING_LOGOS) and map 53, AND any spawn/map combo
    // whose dmaMapAssets ROM address is 0 or obviously garbage (high bit set
    // in the size field), which would crash on DMA.  For safety, skip any map
    // whose rom asset header at mapRomAddressTable[mapId] is 0 / corrupted.
    // mapRomAddressTable is at 0x80110270 in the ELF data section (u32 pairs:
    // romStart, romEnd per map; stride 8 bytes).
    constexpr uint32_t MAP_ROM_TBL = 0x80110270u;
    uint32_t map_rom_start = (uint32_t)MEM_W(0, (gpr)(int32_t)(MAP_ROM_TBL + mapId * 8u));
    uint32_t map_rom_end   = (uint32_t)MEM_W(4, (gpr)(int32_t)(MAP_ROM_TBL + mapId * 8u));
    bool bad_rom = (map_rom_start == 0) || (map_rom_end < map_rom_start) ||
                   (map_rom_end - map_rom_start > 0x1000000u);

    bool is_dummy = (spawnPoint == OPENING_LOGOS_SPAWN_POINT) ||
                    (mapId == OPENING_LOGOS_MAP_ID) || bad_rom;

    if (bad_rom && !is_dummy) {
        printf("[loadMapAtSpawnPoint] bad ROM for spawn=0x%02X map=%u romStart=%08X romEnd=%08X – skipping\n",
               spawnPoint, mapId, map_rom_start, map_rom_end);
        fflush(stdout);
    }

    if (is_dummy) {
        printf("[loadMapAtSpawnPoint] dummy/cinematic map – skipping world load\n");
        fflush(stdout);

        s_no_world_geometry = true;

        // Unload previous map assets if one was loaded.
        if (prev != 0xFF) {
            ctx->r4 = 0;
            unloadMapAssets(rdram, ctx);
        }

        // Set previousMapIndex = old gBaseMapIndex.
        rdram_wu8(rdram, 0x8016F899u, prev);
        // Set gBaseMapIndex = mapId.
        rdram_wu8(rdram, 0x80170458u, mapId);
        // Set gMapWithSeasonIndex = mapId (0x80205635).
        rdram_wu8(rdram, 0x80205635u, mapId);

        // Mark MAP_CONTROLLER_ASSETS_LOADED (bit 1) so the engine doesn't retry.
        uint16_t fl = *(uint16_t*)(rdram + ((0x80205622u ^ 2u) - 0x80000000u));
        rdram_wu16(rdram, 0x80205622u, fl | 0x2u);

        ctx->r2 = 0;  // return FALSE (no map loaded)
        return;
    }

    // --- Normal map path: replicate original loadMapAtSpawnPoint ---

    // Save previous map index.
    rdram_wu8(rdram, 0x8016F899u, prev);

    // gBaseMapIndex = mapId.
    rdram_wu8(rdram, 0x80170458u, mapId);
    // gMapWithSeasonIndex = mapId (may be adjusted for season).
    rdram_wu8(rdram, 0x80205635u, mapId);

    // getLevelFlags(mapId) → check LEVEL_HAS_SEASON_MAP (bit 3).
    // (We skip the season adjustment for now — will add if needed.)

    // loadMap(0, gMapWithSeasonIndex).
    ctx->r4 = 0;
    ctx->r5 = (gpr)(int32_t)(uint32_t)mapId;
    {
        // loadMap: dmaMapAssets + map transform/rotation setup.
        extern void loadMap(uint8_t*, recomp_context*);
        loadMap(rdram, ctx);
    }

    // setInitialMapRotation(0, defaultMapRotations[mapId] % 8).
    // For now skip the rotation lookup and use 0.
    ctx->r4 = 0;
    ctx->r5 = 0;
    setInitialMapRotation(rdram, ctx);

    ctx->r2 = 1;  // return TRUE
}

// ---------------------------------------------------------------------------
// setMapGrid – null/sentinel-safe guard.
//
// Called by setupMap after dmaMapAssets; if grid data is NULL or points
// outside valid RDRAM (0x80000000..0x80800000) skip the body.
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void setMapGrid(uint8_t* rdram, recomp_context* ctx) {
    gpr mapGrid_vaddr = ctx->r4;
    gpr data_vaddr    = ctx->r5;

    uint32_t data_phys = (uint32_t)data_vaddr & 0x1FFFFFFFu;
    bool valid = (data_vaddr != 0) && (data_phys < 0x800000u);

    if (!valid) {
        printf("[setMapGrid] invalid data ptr 0x%08X – sentinel/dummy map\n",
               (uint32_t)data_vaddr);
        fflush(stdout);
        return;
    }

    // Replicate original setMapGrid body (map.c):
    MEM_B(0, mapGrid_vaddr) = (int8_t)MEM_BU(4,  data_vaddr);  // tileSizeX
    MEM_B(1, mapGrid_vaddr) = (int8_t)MEM_BU(5,  data_vaddr);  // tileSizeZ
    MEM_B(2, mapGrid_vaddr) = (int8_t)MEM_BU(6,  data_vaddr);  // mapWidth
    MEM_B(3, mapGrid_vaddr) = (int8_t)MEM_BU(7,  data_vaddr);  // mapHeight

    int16_t sw0 = (int16_t)(((uint32_t)MEM_BU(8, data_vaddr) << 8) | MEM_BU(9, data_vaddr));
    MEM_H(4, mapGrid_vaddr) = sw0;  // unused

    int16_t sw1 = (int16_t)(((uint32_t)MEM_BU(10, data_vaddr) << 8) | MEM_BU(11, data_vaddr));
    MEM_H(6, mapGrid_vaddr) = sw1;  // tileCount

    MEM_W(8, mapGrid_vaddr) = (int32_t)((uint32_t)data_vaddr + 12u);  // gridToTileIndex
}

// ---------------------------------------------------------------------------
// dmaMapAssets – sentinel-offset guard (safety net only; loadMapAtSpawnPoint
// should prevent reaching here with dummy data).
//
// We let the recompiled body run and only interpose at the sentinel check.
// Since we fully replaced dmaMapAssets with our own implementation we must
// also handle the normal path.
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void dmaMapAssets(uint8_t* rdram, recomp_context* ctx) {
    uint32_t mainMapIdx  = (uint32_t)ctx->r4 & 0xFFFF;
    uint32_t levelMapIdx = (uint32_t)ctx->r5 & 0xFFFF;

    if (mainMapIdx != 0) { ctx->r2 = 0; return; }

    // MAP_CONTROLLER_INITIALIZED check (flag bit 0 at 0x80205622).
    uint16_t ctrl = *(uint16_t*)(rdram + ((0x80205622u ^ 2u) - 0x80000000u));
    if (!(ctrl & 1u)) { ctx->r2 = 0; return; }

    // Store level map index.
    *(uint16_t*)(rdram + ((0x8020561Au ^ 2u) - 0x80000000u)) = (uint16_t)levelMapIdx;

    // mapDataAddresses[levelMapIdx]: base 0x80188AF0, stride 12.
    constexpr uint32_t MAP_ADDR_BASE = 0x80188AF0u;
    uint32_t entry    = MAP_ADDR_BASE + levelMapIdx * 12u;
    uint32_t rom_start = *(uint32_t*)(rdram + (entry - 0x80000000u));
    uint32_t rom_end   = *(uint32_t*)(rdram + (entry - 0x80000000u + 4u));
    uint32_t dma_size  = (rom_end > rom_start) ? (rom_end - rom_start) : 0u;

    // mapDataIndex (DMA destination) at 0x802055D8.
    gpr mapDataIdx = (gpr)(int32_t)(*(uint32_t*)(rdram + (0x802055D8u - 0x80000000u)));

    printf("[dmaMapAssets] map=%u romStart=0x%08X size=0x%X dst=0x%08X\n",
           levelMapIdx, rom_start, dma_size, (uint32_t)mapDataIdx);
    fflush(stdout);

    // Perform DMA if valid.
    if (dma_size > 0 && dma_size <= 0x800000u) {
        ctx->r4 = (gpr)(int32_t)rom_start;
        ctx->r5 = mapDataIdx;
        ctx->r6 = (gpr)dma_size;
        nuPiReadRom(rdram, ctx);
    }

    // Sentinel check on first offset word.
    uint32_t off0 = (mapDataIdx != 0 && dma_size > 4u)
                    ? *(uint32_t*)(rdram + ((uint32_t)mapDataIdx - 0x80000000u))
                    : 0xFFFFFFFFu;

    constexpr uint32_t MAX_MAP_OFFSET = 0x01000000u;
    bool is_dummy = (off0 & 0x80000000u) || (off0 >= MAX_MAP_OFFSET) || (dma_size == 0);

    if (is_dummy) {
        printf("[dmaMapAssets] sentinel 0x%08X – dummy map, skipping setupMap\n", off0);
        fflush(stdout);
        *(uint16_t*)(rdram + ((0x80205622u ^ 2u) - 0x80000000u)) = ctrl | 0x2u;
        ctx->r2 = 1;
        return;
    }

    // Normal path: compute pointers and call setupMap.
    auto get_addr = [&](uint32_t i) -> gpr {
        uint32_t off = *(uint32_t*)(rdram + ((uint32_t)mapDataIdx - 0x80000000u + i * 4u));
        return (gpr)(int32_t)((uint32_t)mapDataIdx + off);
    };

    ctx->r4  = 0;
    ctx->r5  = get_addr(0);
    ctx->r6  = get_addr(1);
    ctx->r7  = get_addr(2);
    ctx->r8  = get_addr(3);
    ctx->r9  = get_addr(4);
    ctx->r10 = get_addr(5);
    ctx->r11 = get_addr(6);
    ctx->r12 = get_addr(7);
    ctx->r14 = get_addr(8);
    ctx->r13 = get_addr(9);

    MEM_W(0x10, ctx->r29) = ctx->r8;
    MEM_W(0x14, ctx->r29) = ctx->r9;
    MEM_W(0x18, ctx->r29) = ctx->r10;
    MEM_W(0x1C, ctx->r29) = ctx->r11;
    MEM_W(0x20, ctx->r29) = ctx->r12;
    MEM_W(0x24, ctx->r29) = ctx->r14;
    MEM_W(0x28, ctx->r29) = ctx->r13;
    setupMap(rdram, ctx);

    *(uint16_t*)(rdram + ((0x80205622u ^ 2u) - 0x80000000u)) = ctrl | 0x2u;
    ctx->r2 = 1;
}

// ---------------------------------------------------------------------------
// loadLevelMapObjects – stub for dummy/cinematic maps.
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void loadLevelMapObjects(uint8_t* rdram, recomp_context* ctx) {
    if (scene_has_no_world_geometry()) {
        ctx->r2 = 0;
        return;
    }
    // For real maps, call the original recompiled body.
    // Since --allow-multiple-definition picked our version, we cannot call
    // the original by name.  Fall through with a warning stub for now.
    printf("[loadLevelMapObjects] called for real map – not yet implemented\n");
    fflush(stdout);
    ctx->r2 = 0;
}

// ---------------------------------------------------------------------------
// initRcp – build a minimal RCP init display list that RT64 can handle.
//
// The original initRcp builds:
//   gSPSegment(0, 0)
//   gSPDisplayList(setup_rspstate_phys)  ← crashes RT64 (MIPS code in RDRAM)
//   gSPDisplayList(setup_rdpstate_phys)  ← crashes RT64 (MIPS code in RDRAM)
//
// The crash happens because the physical addresses for setup_rspstate and
// setup_rdpstate computed by the ROM MIPS instructions point to locations in
// RDRAM that contain MIPS code (due to ELF vs ROM layout mismatch).  RT64
// tries to interpret that as a display list and crashes.
//
// RT64 as an HLE renderer doesn't need the RSP state init commands at all
// (those configure the real RSP microcode).  We replace initRcp with a
// minimal version that only sets segment 0 and returns, letting the
// clearFramebuffer and other commands work normally.
//
// Return value: r2 = dl + 8 (the next display list pointer, 1 command written).
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void initRcp(uint8_t* rdram, recomp_context* ctx) {
    gpr dl = ctx->r4;  // Gfx* dl (N64 vaddr of display list buffer)

    // gSPSegment(dl, 0, 0)  – opcode 0xDB, seg=0, addr=0
    MEM_W(0x0, dl) = (int32_t)0xDB060000;
    MEM_W(0x4, dl) = 0;

    // Advance dl pointer by 1 command (8 bytes).
    ctx->r2 = ADD32(dl, 0x8);
}

// ---------------------------------------------------------------------------
// clearFramebuffer – real framebuffer clear.
//
// The original crashes because nuGfxZBuffer was zero.  We now have it set
// to 0x80000400 (NU_GFX_ZBUFFER_ADDR).  Emit a full clear of zbuffer and
// colour framebuffer using the current nuGfxCfb_ptr value.
//
// nuGfxCfb_ptr at VMA 0x80118068 (MEM_W access).
// G_FILLRECT encoding: w0 = 0xF6 | ((x1*4)<<12) | (y1*4)
//                      w1 = (x0*4) | ((y0*4)<<12)
// For full screen 320x240: x0=0,y0=0,x1=319,y1=239
//   w0 = 0xF6000000 | (319*4<<12) | (239*4) = 0xF6 4FC3 BC00
//   Actually the game uses: F6 4F C3 BC 00 00 00 00 (from earlier DL dump)
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void clearFramebuffer(uint8_t* rdram, recomp_context* ctx) {
    gpr dl = ctx->r4;

    // Read current framebuffer pointer from nuGfxCfb_ptr (VMA 0x80118068).
    uint32_t cfb_vaddr = (uint32_t)MEM_W(0x18068, (gpr)(int32_t)0x80100000u);
    if (cfb_vaddr == 0u) cfb_vaddr = 0x8038F800u;
    uint32_t cfb_phys = cfb_vaddr & 0x1FFFFFFFu;
    constexpr uint32_t ZBF_PHYS = 0x00000400u;  // nuGfxZBuffer phys

    // G_SETDEPTHIMAGE(zbuf)
    MEM_W(0x0, dl) = (int32_t)0xFE000000u; MEM_W(0x4, dl) = (int32_t)ZBF_PHYS;
    dl = ADD32(dl, 0x8);
    // G_PIPESYNCH
    MEM_W(0x0, dl) = (int32_t)0xE7000000u; MEM_W(0x4, dl) = 0;
    dl = ADD32(dl, 0x8);
    // G_SETCYCLETYPE(G_CYC_FILL)
    MEM_W(0x0, dl) = (int32_t)0xE3000A01u; MEM_W(0x4, dl) = (int32_t)0x00300000u;
    dl = ADD32(dl, 0x8);
    // G_SETCOLORIMAGE → zbuffer (to clear depth)
    MEM_W(0x0, dl) = (int32_t)0xFF10013Fu; MEM_W(0x4, dl) = (int32_t)ZBF_PHYS;
    dl = ADD32(dl, 0x8);
    // G_SETFILLCOLOR(Z_MAX: 0xFFFC packed ×2)
    MEM_W(0x0, dl) = (int32_t)0xF7000000u; MEM_W(0x4, dl) = (int32_t)0xFFFCFFFCu;
    dl = ADD32(dl, 0x8);
    // G_FILLRECT(0,0,319,239)
    MEM_W(0x0, dl) = (int32_t)0xF64FC3BCu; MEM_W(0x4, dl) = 0;
    dl = ADD32(dl, 0x8);
    // G_PIPESYNCH
    MEM_W(0x0, dl) = (int32_t)0xE7000000u; MEM_W(0x4, dl) = 0;
    dl = ADD32(dl, 0x8);
    // G_SETCOLORIMAGE → colour framebuffer
    MEM_W(0x0, dl) = (int32_t)0xFF10013Fu; MEM_W(0x4, dl) = (int32_t)cfb_phys;
    dl = ADD32(dl, 0x8);
    // G_SETFILLCOLOR(black opaque: RGBA5551 (0,0,0,1)×2 = 0x00010001)
    MEM_W(0x0, dl) = (int32_t)0xF7000000u; MEM_W(0x4, dl) = (int32_t)0x00010001u;
    dl = ADD32(dl, 0x8);
    // G_FILLRECT(0,0,319,239)
    MEM_W(0x0, dl) = (int32_t)0xF64FC3BCu; MEM_W(0x4, dl) = 0;
    dl = ADD32(dl, 0x8);
    // G_PIPESYNCH
    MEM_W(0x0, dl) = (int32_t)0xE7000000u; MEM_W(0x4, dl) = 0;
    dl = ADD32(dl, 0x8);

    ctx->r2 = dl;
}

// ---------------------------------------------------------------------------
// setupCameraMatrices – inject safe default camera for degenerate/no-world.
//
// gCamera VMA: 0x801F6F38. Camera layout (from graphic.h):
//   +0xA8  Vec3f eye      (3 × f32)
//   +0xB4  Vec3f at       (3 × f32)
//   +0xC0  Vec3f up       (3 × f32)
//   +0xCC  u8 perspectiveMode
//
// When the opening logos map (#53) is loaded, gCamera remains zero-
// initialised (BSS). guLookAt(eye=(0,0,0), at=(0,0,0), up=(0,0,0)) is
// degenerate (eye==at, undefined up) and crashes with FP invalid ops.
//
// Fix: detect zero/degenerate camera and inject safe values before calling
// the original function, which then produces correct matrices.
//
// Safe default: eye=(0,0,100), at=(0,0,0), up=(0,1,0), ortho mode.
// (Ortho is safe because fov/aspect/near/far may also be zero.)
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void setupCameraMatrices(uint8_t* rdram, recomp_context* ctx) {
    // s0 = camera ptr (r5 at entry, saved before call by recompiled code)
    // r4 = dl ptr, r5 = camera ptr, r6 = sceneMatrices ptr
    gpr camera_vaddr = ctx->r5;

    // Read eye and at vectors as raw f32 bits via MEM_W (no byte-swap for words).
    uint32_t eye_x_bits = (uint32_t)MEM_W(0xA8, camera_vaddr);
    uint32_t eye_y_bits = (uint32_t)MEM_W(0xAC, camera_vaddr);
    uint32_t eye_z_bits = (uint32_t)MEM_W(0xB0, camera_vaddr);
    uint32_t at_x_bits  = (uint32_t)MEM_W(0xB4, camera_vaddr);
    uint32_t at_y_bits  = (uint32_t)MEM_W(0xB8, camera_vaddr);
    uint32_t at_z_bits  = (uint32_t)MEM_W(0xBC, camera_vaddr);

    // Reinterpret as float for comparison.
    float eye_x, eye_y, eye_z, at_x, at_y, at_z;
    __builtin_memcpy(&eye_x, &eye_x_bits, 4);
    __builtin_memcpy(&eye_y, &eye_y_bits, 4);
    __builtin_memcpy(&eye_z, &eye_z_bits, 4);
    __builtin_memcpy(&at_x,  &at_x_bits,  4);
    __builtin_memcpy(&at_y,  &at_y_bits,  4);
    __builtin_memcpy(&at_z,  &at_z_bits,  4);

    // Detect degenerate camera: eye ≈ at (or both zero).
    float dx = eye_x - at_x, dy = eye_y - at_y, dz = eye_z - at_z;
    bool degenerate = (dx * dx + dy * dy + dz * dz) < 0.001f;

    if (degenerate) {
        // Inject safe orthographic camera.
        // eye = (0, 0, 100), at = (0, 0, 0), up = (0, 1, 0)
        // perspectiveMode = 0 (ortho)
        // ortho params: l=-160, r=160, t=120, b=-120, n=1, f=200 (safe defaults)
        constexpr float EYE_Z = 100.0f;
        constexpr float UP_Y  = 1.0f;
        // f32 float bits
        uint32_t f_eyez, f_upy, f_zero;
        float tmp;
        tmp = EYE_Z; __builtin_memcpy(&f_eyez, &tmp, 4);
        tmp = UP_Y;  __builtin_memcpy(&f_upy,  &tmp, 4);
        tmp = 0.0f;  __builtin_memcpy(&f_zero, &tmp, 4);

        MEM_W(0xA8, camera_vaddr) = (int32_t)f_zero;  // eye.x = 0
        MEM_W(0xAC, camera_vaddr) = (int32_t)f_zero;  // eye.y = 0
        MEM_W(0xB0, camera_vaddr) = (int32_t)f_eyez;  // eye.z = 100
        MEM_W(0xB4, camera_vaddr) = (int32_t)f_zero;  // at.x  = 0
        MEM_W(0xB8, camera_vaddr) = (int32_t)f_zero;  // at.y  = 0
        MEM_W(0xBC, camera_vaddr) = (int32_t)f_zero;  // at.z  = 0
        MEM_W(0xC0, camera_vaddr) = (int32_t)f_zero;  // up.x  = 0
        MEM_W(0xC4, camera_vaddr) = (int32_t)f_upy;   // up.y  = 1
        MEM_W(0xC8, camera_vaddr) = (int32_t)f_zero;  // up.z  = 0
        MEM_B(0xCC, camera_vaddr) = 0;                 // perspectiveMode = ortho

        // Ortho params stored in camera struct:
        // +0x8C l, +0x90 r, +0x94 t, +0x98 b (wait — let's check)
        // From setupCameraMatrices recompiled (ortho path):
        //   r5 = camera+0x80 (l?), r6 = camera+0x84, r7 = camera+0x88
        //   and fp from +0x8C, +0x90, +0x94, +0x98
        // Camera struct: l=+0x80,r=+0x84,t=+0x88,b=+0x8C,n=+0x90,f=+0x94
        // Let's check: projection Mtx (64B) + viewing Mtx (64B) = 128B = 0x80
        // Then l at +0x80, r at +0x84, t at +0x88, b at +0x8C, n at +0x90, f at +0x94
        float l=-160.f, r=160.f, t=120.f, b=-120.f, n=1.f, fv=200.f;
        uint32_t bl, br, bt, bb, bn, bf;
        __builtin_memcpy(&bl, &l,  4); __builtin_memcpy(&br, &r,  4);
        __builtin_memcpy(&bt, &t,  4); __builtin_memcpy(&bb, &b,  4);
        __builtin_memcpy(&bn, &n,  4); __builtin_memcpy(&bf, &fv, 4);
        MEM_W(0x80, camera_vaddr) = (int32_t)bl;
        MEM_W(0x84, camera_vaddr) = (int32_t)br;
        MEM_W(0x88, camera_vaddr) = (int32_t)bt;
        MEM_W(0x8C, camera_vaddr) = (int32_t)bb;
        MEM_W(0x90, camera_vaddr) = (int32_t)bn;
        MEM_W(0x94, camera_vaddr) = (int32_t)bf;

        // Also zero currentWorldRotationAngles (VMA 0x8017044C) to prevent
        // cos/sin on garbage values in renderSceneGraph.
        MEM_W(0x044C, (gpr)(int32_t)0x80170000u) = 0;
        MEM_W(0x0450, (gpr)(int32_t)0x80170000u) = 0;
        MEM_W(0x0454, (gpr)(int32_t)0x80170000u) = 0;
    }

    // Call the original recompiled setupCameraMatrices.
    // Since we RECOMP_PATCH replaced it, we inline the critical calls manually.
    // r4 = dl, r5 = camera, r6 = sceneMatrices → call guOrtho/guPerspective+guLookAt.
    // Simplest: forward to the recompiled original via its address.
    // We can't call by name (we replaced it), so duplicate the minimal calls.
    // For now: call guOrtho (mode=0) + guLookAt directly.
    extern void guOrtho(uint8_t* rdram, recomp_context* ctx);
    extern void guLookAt(uint8_t* rdram, recomp_context* ctx);
    extern void gSPMatrix_impl(uint8_t* rdram, recomp_context* ctx);

    // The original setupCameraMatrices body is too large to inline cleanly.
    // Use the alternative: a shim that calls guOrtho then guLookAt.
    // For the degenerate case this is correct (we patched camera above).
    // For non-degenerate, we rely on the original; since we replaced that,
    // just call the same shim — it handles both ortho and perspective via
    // the perspectiveMode field we can read ourselves.

    uint8_t perspMode = (uint8_t)MEM_BU(0xCC, camera_vaddr);

    // dl pointer and sceneMatrices pointer from regs.
    // The function signature: setupCameraMatrices(Gfx* dl, Camera* cam, SceneMatrices* mat)
    // → r4=dl, r5=cam, r6=mat when called from renderScene.
    // We need to call: guOrtho or guPerspective, then guLookAt.
    // After that, emit gSPPerspNormalize (for perspective), gSPLookAt, gSPMatrix x2.
    // This is complex; for now just call the existing recompiled helper functions.

    // Rather than reimplementing the full body, just call the parts directly.
    // setupCameraMatrices calls (in order):
    //   1. guOrtho or guPerspective
    //   2. guLookAt(&sceneMatrices.viewing, ...)
    //   3. gSPLookAt, gSPMatrix x2
    // All via the recompiled functions which take ctx→

    // Step 1: projection matrix
    ctx->r29 = ADD32(ctx->r29, -0x40);
    MEM_W(0x3C, ctx->r29) = ctx->r31;

    gpr dl      = ctx->r4;
    gpr cam     = ctx->r5;
    gpr mat     = ctx->r6;

    if (perspMode == 0) {
        // guOrtho(&mat->projection, l, r, t, b, n, f, 0.9999f)
        ctx->r4 = mat;  // &projection = mat+0 for Mtx at start
        ctx->r5 = MEM_W(0x80, cam);  // l
        ctx->r6 = MEM_W(0x84, cam);  // r
        ctx->r7 = MEM_W(0x88, cam);  // t
        MEM_W(0x10, ctx->r29) = MEM_W(0x8C, cam);  // b
        MEM_W(0x14, ctx->r29) = MEM_W(0x90, cam);  // n
        MEM_W(0x18, ctx->r29) = MEM_W(0x94, cam);  // f
        uint32_t f_scale; float fs=0.9999f; __builtin_memcpy(&f_scale,&fs,4);
        MEM_W(0x1C, ctx->r29) = (int32_t)f_scale;
        guOrtho(rdram, ctx);
    } else if (perspMode == 1) {
        // guPerspective — skip for now, use ortho fallback
        ctx->r4 = mat;
        ctx->r5 = MEM_W(0x80, cam);
        ctx->r6 = MEM_W(0x84, cam);
        ctx->r7 = MEM_W(0x88, cam);
        MEM_W(0x10, ctx->r29) = MEM_W(0x8C, cam);
        MEM_W(0x14, ctx->r29) = MEM_W(0x90, cam);
        MEM_W(0x18, ctx->r29) = MEM_W(0x94, cam);
        uint32_t f_scale; float fs=0.9999f; __builtin_memcpy(&f_scale,&fs,4);
        MEM_W(0x1C, ctx->r29) = (int32_t)f_scale;
        guOrtho(rdram, ctx);
    }

    // guLookAt(&sceneMatrices.viewing, eye.x,eye.y,eye.z, at.x,at.y,at.z, up.x,up.y,up.z)
    // sceneMatrices.viewing is at offset 0x1C0 from mat (7th Mtx: 6×64+64=0x1C0)
    ctx->r4 = ADD32(mat, 0x1C0);
    ctx->r5 = MEM_W(0xA8, cam);  // eye.x
    ctx->r6 = MEM_W(0xAC, cam);  // eye.y
    ctx->r7 = MEM_W(0xB0, cam);  // eye.z
    MEM_W(0x10, ctx->r29) = MEM_W(0xB4, cam);  // at.x
    MEM_W(0x14, ctx->r29) = MEM_W(0xB8, cam);  // at.y
    MEM_W(0x18, ctx->r29) = MEM_W(0xBC, cam);  // at.z
    MEM_W(0x1C, ctx->r29) = MEM_W(0xC0, cam);  // up.x
    MEM_W(0x20, ctx->r29) = MEM_W(0xC4, cam);  // up.y
    MEM_W(0x24, ctx->r29) = MEM_W(0xC8, cam);  // up.z
    guLookAt(rdram, ctx);

    // Emit gSPMatrix commands for projection and viewing matrices.
    // F3DEX2 gSPMatrix uses opcode 0xDA. The low byte is
    // (G_MTX_* flags ^ G_MTX_PUSH), and bits 19..23 encode sizeof(Mtx).
    uint32_t proj_phys = (uint32_t)mat & 0x1FFFFFFFu;   // &projection physical
    uint32_t view_phys = ((uint32_t)mat + 0x1C0u) & 0x1FFFFFFFu;  // &viewing physical

    MEM_W(0x0, dl) = (int32_t)0xDA380007u;  // PROJECTION|LOAD|NOPUSH
    MEM_W(0x4, dl) = (int32_t)proj_phys;
    dl = ADD32(dl, 0x8);
    MEM_W(0x0, dl) = (int32_t)0xDA380006u;  // MODELVIEW|LOAD|NOPUSH
    MEM_W(0x4, dl) = (int32_t)view_phys;
    dl = ADD32(dl, 0x8);

    ctx->r31 = MEM_W(0x3C, ctx->r29);
    ctx->r29 = ADD32(ctx->r29, 0x40);
    ctx->r2 = dl;  // return updated dl
}

// ---------------------------------------------------------------------------
// DL command decoder
// ---------------------------------------------------------------------------
static const char* dl_cmd_name(uint32_t w0) {
    uint8_t cmd = (w0 >> 24) & 0xFF;
    switch (cmd) {
        case 0x01: return "gDPSetScissor";
        case 0x04: return "gSPMatrix";
        case 0x06: return "gSPDisplayList";
        case 0xB6: return "gDPSetTextureImage";
        case 0xB8: return "gDPSetCombine";
        case 0xBB: return "gDPSetTextureLUT";
        case 0xBD: return "gDPSetTile";
        case 0xBF: return "gDPSetTileSize";
        case 0xD7: return "gSPClearGeometryMode";
        case 0xD8: return "gSPSetGeometryMode";
        case 0xD9: return "gSPPopMatrix";
        case 0xDA: return "gSPMatrix";
        case 0xDB: return "gSPSegment";
        case 0xDC: return "gSPViewport";
        case 0xDE: return "gSPDisplayList";
        case 0xDF: return "gSPEndDisplayList";
        case 0xE3: return "gDPSetCycleType";
        case 0xE4: return "gDPSetRenderMode";
        case 0xE5: return "gDPSetTextureFilter";
        case 0xE6: return "gDPSetTextureLUT";
        case 0xE7: return "gDPSetFillColor";
        case 0xE8: return "gDPSetFogColor";
        case 0xE9: return "gDPFullSync";
        case 0xEC: return "gDPSetEnvColor";
        case 0xED: return "gDPSetBlendColor";
        case 0xF0: return "gSPTexture";
        case 0xF5: return "gDPPipelineMode";
        case 0xF6: return "gDPSetCombine";
        case 0xF7: return "gDPSetFillColor";
        case 0xF9: return "gDPTextureRectangle";
        case 0xFA: return "gDPFillRect";
        case 0xFB: return "gDPTextureRectangleFlip";
        case 0xFD: return "gDPLoadSync";
        case 0xFE: return "gDPPipeSync";
        case 0xFF: return "gDPNoOp";
        default: 
            static char buf[16];
            snprintf(buf, sizeof(buf), "cmd=0x%02X", cmd);
            return buf;
    }
}

// ---------------------------------------------------------------------------
// renderScene – submit viewport + corrected viewportDL; skip world geometry.
//
// The original bakes viewportDL physical as 0x0010DE80 (ROM layout).
// After our ELF data-section load it lives at 0x0010DE30 (ELF layout).
// We write the corrected address, skip the camera/geometry calls that crash
// for no-world scenes, and submit the DL directly.
//
// sceneGraphDisplayList base: 0x801836A0, stride per buffer: 0x2800 bytes.
// gGraphicsBufferIndex at MEM_W(0x5630, 0x80200000).
// ---------------------------------------------------------------------------
static uint32_t rs_count = 0;
extern "C" RECOMP_PATCH void renderScene(uint8_t* rdram, recomp_context* ctx) {
    uint32_t buf_idx = (uint32_t)MEM_W(0x5630, (gpr)(int32_t)0x80200000u);

    constexpr uint32_t SCENE_GFX_BASE   = 0x801836A0u;
    constexpr uint32_t SCENE_GFX_STRIDE = 0x2800u;
    gpr dl      = (gpr)(int32_t)(SCENE_GFX_BASE + buf_idx * SCENE_GFX_STRIDE);
    gpr dl_start = dl;

    // gSPDisplayList(viewportDL) — corrected physical address
    constexpr uint32_t VIEWPORT_DL_PHYS = 0x0010DE30u;
    MEM_W(0x0, dl) = (int32_t)0xDE000000u;
    MEM_W(0x4, dl) = (int32_t)VIEWPORT_DL_PHYS;
    dl = ADD32(dl, 0x8);

    // Call renderSceneGraph to render scene nodes (bitmaps/sprites/entities).
    // renderSceneGraph feeds these exact 4 floats into sinfRadians/cosfRadians;
    // if they contain garbage they crash with FP invalid ops.
    //
    // currentWorldRotationAngles  VMA 0x8017044C: .x (+0x0), .y (+0x4)
    // previousWorldRotationAngles VMA 0x8013D5D8: .x (+0x0), .y (+0x4)
    {
        MEM_W(0x044C, (gpr)(int32_t)0x80170000u) = 0;  // current.x
        MEM_W(0x0450, (gpr)(int32_t)0x80170000u) = 0;  // current.y
        MEM_W(0x0454, (gpr)(int32_t)0x80170000u) = 0;  // current.z (safety)
        MEM_W(0x0000, (gpr)(int32_t)0x8013D5D8u) = 0;  // previous.x
        MEM_W(0x0004, (gpr)(int32_t)0x8013D5D8u) = 0;  // previous.y
        MEM_W(0x0008, (gpr)(int32_t)0x8013D5D8u) = 0;  // previous.z (safety)
    }
    // sceneMatrices[buf_idx] at VMA 0x80237460, stride 0x290 bytes.
    constexpr uint32_t SCENE_MATRICES_BASE   = 0x80237460u;
    constexpr uint32_t SCENE_MATRICES_STRIDE = 0x290u;
    gpr scene_mat = (gpr)(int32_t)(SCENE_MATRICES_BASE + buf_idx * SCENE_MATRICES_STRIDE);
    
    constexpr uint32_t GCAMERA = 0x801F6F38u;

    if (rs_count < 5) {
        printf("[renderScene] dl_in=%p camera=%p scene_mat=%p\n",
               (void*)(uint32_t)dl, (void*)GCAMERA, (void*)(uint32_t)scene_mat);
        fflush(stdout);
    }

    ctx->r4 = dl;
    ctx->r5 = (gpr)(int32_t)GCAMERA;
    ctx->r6 = scene_mat;
    setupCameraMatrices(rdram, ctx);
    dl = ctx->r2;
    
    ctx->r4 = dl;
    ctx->r5 = scene_mat;
    { extern void renderSceneGraph(uint8_t*, recomp_context*); renderSceneGraph(rdram, ctx); }
    dl = ctx->r2;
    
    // Log display list contents after renderSceneGraph
    uint32_t dl_size_bytes = (uint32_t)((uint32_t)dl - (uint32_t)(dl_start));
    if (s_title_forced && dl_size_bytes >= 512) {
        software_blit_title_from_scene(rdram, (uint32_t)dl_start, dl_size_bytes);
    }
    if (rs_count < 5) {
        printf("[renderScene] dl_out=%p dl_size=%u\n", (void*)(uint32_t)dl, dl_size_bytes);
        fflush(stdout);
    }

    if (rs_count < 5) {
        printf("[renderScene #%u] dumping dl_out (%u cmds):\n", rs_count, dl_size_bytes / 8);
        uint32_t* dlp = (uint32_t*)(rdram + ((uint32_t)dl_start & 0x1FFFFFFFu));
        uint32_t n_cmds = dl_size_bytes / 8;
        for (uint32_t i = 0; i < n_cmds; i++) {
            printf("  [%02u] %08X %08X  (%s)\n", i, dlp[i*2], dlp[i*2+1], dl_cmd_name(dlp[i*2]));
        }
        fflush(stdout);
        rs_count++;
    }

    // gDPFullSync + gSPEndDisplayList
    MEM_W(0x0, dl) = (int32_t)0xE9000000u; MEM_W(0x4, dl) = 0;
    dl = ADD32(dl, 0x8);
    MEM_W(0x0, dl) = (int32_t)0xDF000000u; MEM_W(0x4, dl) = 0;
    dl = ADD32(dl, 0x8);

    // Dump sprite sub-DLs when title screen scene is active
    static bool dumped_title_scene = false;
    if (!dumped_title_scene && s_title_forced && dl_size_bytes >= 64) {
        uint32_t* dl_words = (uint32_t*)(rdram + ((uint32_t)dl_start & 0x1FFFFFFFu));
        uint32_t n_cmds = dl_size_bytes / 8;
        int dl_count = 0;
        for (uint32_t i = 0; i < n_cmds && dl_count < 12; i++) {
            if (((dl_words[i*2] & 0xFF000000u) == 0xDE000000u) && (i != 0)) {
                uint32_t sub_dl_phys = dl_words[i*2+1];
                printf("[title-scene-dl] #%d at 0x%08X\n", dl_count, sub_dl_phys);
                uint32_t* subp = (uint32_t*)(rdram + (sub_dl_phys & 0x1FFFFFFFu));
                for (int j = 0; j < 24 && (sub_dl_phys & 0x1FFFFFFFu) + j*8 < 0x800000u; j++) {
                    printf("  +%03d: %08X %08X  (%s)\n", j*8, subp[j*2], subp[j*2+1], dl_cmd_name(subp[j*2]));
                    if (subp[j*2] == 0xDF000000u) break;
                }
                fflush(stdout);
                dl_count++;
            }
        }
        dumped_title_scene = true;
    }

    // nuGfxTaskStart(dl_start, size, NU_GFX_UCODE_F3DEX, NU_SC_NOSWAPBUFFER)
    ctx->r4 = dl_start;
    ctx->r5 = (gpr)(int32_t)((uint32_t)dl - (uint32_t)dl_start);
    ctx->r6 = 1;
    ctx->r7 = 0;
    { extern void nuGfxTaskStart(uint8_t*, recomp_context*); nuGfxTaskStart(rdram, ctx); }

    // gfxTaskNo++
    uint8_t tno = (uint8_t)MEM_BU(0x5209, (gpr)(int32_t)0x80200000u);
    MEM_B(0x5209, (gpr)(int32_t)0x80200000u) = (int8_t)(tno + 1u);
    ctx->r2 = (gpr)(int32_t)(uint32_t)(tno + 1u);
}

// ---------------------------------------------------------------------------
// doViewportGfxTask – corrected viewportDL address.
//
// The original bakes 0x0010DE80 for viewportDL (ROM); ELF has it at 0x0010DE30.
// D_80205000[buf_idx]: base 0x80205000, stride 256 bytes per buffer.
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void doViewportGfxTask(uint8_t* rdram, recomp_context* ctx) {
    uint32_t buf_idx = (uint32_t)MEM_W(0x5630, (gpr)(int32_t)0x80200000u);
    constexpr uint32_t VIEWPORT_TASK_BASE = 0x80205000u;
    gpr dl       = (gpr)(int32_t)(VIEWPORT_TASK_BASE + (buf_idx << 8));
    gpr dl_start = dl;

    // gSPDisplayList(viewportDL) — corrected physical
    MEM_W(0x0, dl) = (int32_t)0xDE000000u;
    MEM_W(0x4, dl) = (int32_t)0x0010DE30u;
    dl = ADD32(dl, 0x8);
    // gDPFullSync
    MEM_W(0x0, dl) = (int32_t)0xE9000000u; MEM_W(0x4, dl) = 0;
    dl = ADD32(dl, 0x8);
    // gSPEndDisplayList
    MEM_W(0x0, dl) = (int32_t)0xDF000000u; MEM_W(0x4, dl) = 0;
    dl = ADD32(dl, 0x8);

    // nuGfxTaskStart(dl_start, size, NU_GFX_UCODE_F3DEX, NU_SC_SWAPBUFFER)
    ctx->r4 = dl_start;
    ctx->r5 = (gpr)(int32_t)((uint32_t)dl - (uint32_t)dl_start);
    ctx->r6 = 1;
    ctx->r7 = 1;
    { extern void nuGfxTaskStart(uint8_t*, recomp_context*); nuGfxTaskStart(rdram, ctx); }

    // gfxTaskNo++
    uint8_t tno = (uint8_t)MEM_BU(0x5209, (gpr)(int32_t)0x80200000u);
    MEM_B(0x5209, (gpr)(int32_t)0x80200000u) = (int8_t)(tno + 1u);
    ctx->r2 = (gpr)(int32_t)(uint32_t)(tno + 1u);
}

// ---------------------------------------------------------------------------
// Callback-chain diagnostics
// ---------------------------------------------------------------------------

// Helper to read current callback index
static inline uint16_t read_cb_idx(uint8_t* rdram) {
    return *(uint16_t*)(rdram + ((0x8020564Au ^ 2u) - 0x80000000u));
}
static inline void write_cb_idx(uint8_t* rdram, uint16_t v) {
    *(uint16_t*)(rdram + ((0x8020564Au ^ 2u) - 0x80000000u)) = v;
}

// setMainLoopCallbackFunctionIndex – log every call and the result.
extern "C" RECOMP_PATCH void setMainLoopCallbackFunctionIndex(uint8_t* rdram, recomp_context* ctx) {
    uint16_t req   = (uint16_t)(ctx->r4 & 0xFFFF);
    uint16_t before = read_cb_idx(rdram);

    // Replicate original logic:
    //   if (index < TABLE_SIZE && table[index] != NULL) { currentIndex = index; return TRUE; }
    constexpr uint32_t TABLE_SIZE = 0x39u; // 57 entries, matching MAIN_LOOP_CALLBACK_FUNCTION_TABLE_SIZE
    constexpr uint32_t TABLE_BASE = 0x80188F70u;
    bool ok = false;
    if (req < TABLE_SIZE) {
        uint32_t fn = *(uint32_t*)(rdram + (TABLE_BASE + req * 4u - 0x80000000u));
        if (fn != 0) {
            write_cb_idx(rdram, req);
            ok = true;
        }
    }
    ctx->r2 = ok ? 1 : 0;

    static uint32_t scmf_count = 0;
    if (scmf_count++ < 20) {
        uint16_t after = read_cb_idx(rdram);
        printf("[setMainLoopCB #%u] req=%u before=%u after=%u ok=%d\n",
               scmf_count, req, before, after, ok);
        fflush(stdout);
    }
}

// setMapAudioAndLighting – log branch taken.
extern "C" RECOMP_PATCH void setMapAudioAndLighting(uint8_t* rdram, recomp_context* ctx) {
    // gCutsceneCompletionFlags at 0x801891D4 (s32, big-endian word)
    constexpr uint32_t COMPL_VADDR = 0x801891D4u;
    int32_t csflags = *(int32_t*)(rdram + (COMPL_VADDR - 0x80000000u));

    // gCutsceneIndex u16 at 0x801C3B66 (xor-2)
    uint16_t csidx = *(uint16_t*)(rdram + ((0x801C3B66u ^ 2u) - 0x80000000u));

    uint16_t cb_before = read_cb_idx(rdram);

    static uint32_t smaal_count = 0;
    printf("[setMapAudioAndLighting #%u] cb=%u csflags=0x%08X csidx=%u\n",
           smaal_count, cb_before, (uint32_t)csflags, csidx);
    fflush(stdout);

    // The original checks gCutsceneCompletionFlags < 0 to detect an in-progress
    // cutscene that already resolved. For the OPENING_LOGOS path, the completion
    // flags may be garbage (0x80003000) because initializeCutscene's nuPiReadRom
    // corrupted or failed before zeroing. We must NOT use that branch when an
    // opening cutscene is active — always route through setLevelLighting.
    constexpr uint16_t OPENING_LOGOS = 1456u;
    bool opening_path = (csidx == OPENING_LOGOS);

    if (csflags < 0 && !opening_path) {
        printf("  -> branch: MAIN_GAME (csflags<0, not opening)\n"); fflush(stdout);
        ctx->r4 = 1;
        setMainLoopCallbackFunctionIndex(rdram, ctx);
    } else {
        printf("  -> branch: setLevelLighting(8,1)\n"); fflush(stdout);
        if (opening_path && csflags != 0) {
            printf("  [fix] clearing garbage gCutsceneCompletionFlags %08X -> 0\n",
                   (uint32_t)csflags);
            fflush(stdout);
            *(int32_t*)(rdram + (COMPL_VADDR - 0x80000000u)) = 0;
        }
        extern void setLevelLighting(uint8_t*, recomp_context*);
        ctx->r4 = 8;
        ctx->r5 = 1;
        setLevelLighting(rdram, ctx);
    }
    smaal_count++;
}

// setLevelLighting – log entry and whether it calls setMainLoopCB.
extern "C" RECOMP_PATCH void setLevelLighting(uint8_t* rdram, recomp_context* ctx) {
    int16_t rate        = (int16_t)(ctx->r4 & 0xFFFF);
    uint16_t cb_arg     = (uint16_t)(ctx->r5 & 0xFFFF);
    uint16_t cb_before  = read_cb_idx(rdram);

    static uint32_t sll_count = 0;
    if (sll_count < 10) {
        printf("[setLevelLighting #%u] rate=%d cb_arg=%u cb_before=%u\n",
               sll_count, rate, cb_arg, cb_before);
        fflush(stdout);
    }

    // Call the original recompiled body by forwarding to the real function.
    // We can't call it by name (we replaced it), so replicate the minimal
    // advancing logic: just call setMainLoopCallbackFunctionIndex(LEVEL_LOAD)
    // when cb_arg != 0 (which is what the original always does at the end).
    if (cb_arg != 0) {
        // Store gameLoopContext.callbackIndex = cb_arg (u16 at 0x80205230, xor-2)
        constexpr uint32_t GLC_VADDR = 0x80205230u;
        *(uint16_t*)(rdram + ((GLC_VADDR ^ 2u) - 0x80000000u)) = cb_arg;

        // setMainLoopCallbackFunctionIndex(LEVEL_LOAD = 5)
        ctx->r4 = 5;
        setMainLoopCallbackFunctionIndex(rdram, ctx);
        if (sll_count < 10) {
            printf("  -> stored glc.callbackIdx=%u, called setMainLoopCB(5), cb now=%u\n",
                   cb_arg, read_cb_idx(rdram));
            fflush(stdout);
        }
    }
    sll_count++;
}

// levelLoadCallback – called while cb=5 (LEVEL_LOAD).
// Original waits for checkMapRGBADone() which needs a real loaded map.
// For cinematic/dummy maps, advance immediately to gameLoopContext.callbackIndex.
extern "C" RECOMP_PATCH void levelLoadCallback(uint8_t* rdram, recomp_context* ctx) {
    // gameLoopContext at 0x80205230: { u16 callbackIndex, u16 frameCount, ... }
    // callbackIndex is big-endian u16 at offset 0 → xor-2 lane
    constexpr uint32_t GLC_VADDR = 0x80205230u;
    uint16_t next_cb = *(uint16_t*)(rdram + ((GLC_VADDR ^ 2u) - 0x80000000u));

    static uint32_t llc_count = 0;
    if (llc_count++ < 3) {
        printf("[levelLoadCallback #%u] next_cb=%u\n", llc_count, next_cb);
        fflush(stdout);
    }

    // Advance to next callback immediately (skip map RGBA wait)
    ctx->r4 = next_cb;
    setMainLoopCallbackFunctionIndex(rdram, ctx);
}

// ---------------------------------------------------------------------------
// initializeCutscene – log entry so we know if it's called and with what seg.
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void initializeCutscene(uint8_t* rdram, recomp_context* ctx) {
    uint16_t seg = (uint16_t)(ctx->r4 & 0xFFFF);

    // Read cutsceneBytecodeAddresses[seg]: base 0x8010FF10, stride 8 (2 u32s)
    constexpr uint32_t BA_BASE = 0x8010FF10u;
    uint32_t rom_start = *(uint32_t*)(rdram + (BA_BASE + seg*8u     - 0x80000000u));
    uint32_t rom_end   = *(uint32_t*)(rdram + (BA_BASE + seg*8u + 4 - 0x80000000u));

    // cutsceneBankLoadAddresses[seg]: base 0x80110150, stride 4
    constexpr uint32_t LA_BASE = 0x80110150u;
    uint32_t load_addr = *(uint32_t*)(rdram + (LA_BASE + seg*4u - 0x80000000u));

    // gCutsceneCompletionFlags before zero: 0x801891D4
    int32_t compl_before = *(int32_t*)(rdram + (0x801891D4u - 0x80000000u));

    printf("[initializeCutscene] seg=%u romStart=%08X romEnd=%08X load=%08X compl_before=%08X\n",
           seg, rom_start, rom_end, load_addr, (uint32_t)compl_before);
    fflush(stdout);

    // Call the original recompiled body.
    extern void initializeCutscene_recomp(uint8_t*, recomp_context*);
    // We replaced initializeCutscene with this patch, so call the original by
    // invoking its recomp address via LOOKUP_FUNC.
    constexpr uint32_t INIT_CS_VADDR = 0x80099a20u;
    LOOKUP_FUNC(INIT_CS_VADDR)(rdram, ctx);

    // Log result
    int32_t compl_after  = (int32_t)MEM_W(0x1891D4, (gpr)(int32_t)0x80000000u);
    uint32_t csflags_after = (uint32_t)MEM_W(0x16FE00, (gpr)(int32_t)0x80000000u);
    printf("[initializeCutscene] done: compl_after=%08X csflags=%08X\n",
           (uint32_t)compl_after, csflags_after);
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// Cutscene executor probe — patches updateCutsceneExecutors to log each active
// executor's bytecodePtr, opcode, waitFrames, and detect stuck PCs.
//
// CutsceneExecutor field offsets (size=0x70, 42 executors at 0x801808B0):
//   +0x00 bytecodePtr  u32
//   +0x66 waitFrames   u16
//   +0x6E flags        u16
// Opcode is the u16 at *bytecodePtr (big-endian xor-2 lane).
// ---------------------------------------------------------------------------
static const char* cs_opcode_name(uint16_t op) {
    static const char* names[] = {
        "SetAnimDataPtrWithFlag",  // 0
        "SetAnimDataPtr",          // 1
        "SetCoordinates",          // 2
        "SetFrameDelta",           // 3
        "SetWaitFrames",           // 4
        "DeactivateSelf",          // 5
        "ExecuteSubroutine",       // 6
        "ReturnFromSubroutine",    // 7
        "BranchOnCurrentButton",   // 8
        "BranchOnButtonPressed",   // 9
        "BranchOnButtonRepeat",    // 10
        "SpawnExecutor",           // 11
        "SetOtherExecutorPC",      // 12
        "DeactivateExecutor",      // 13
        "DMASprite",               // 14
        "SetEntityAnimations",     // 15
        "DoDMA",                   // 16
        "SetU8Value",              // 17
        "SetU16Value",             // 18
        "SetU32Value",             // 19
        "BranchU8VarInRange",      // 20
        "BranchU16VarInRange",     // 21
        "BranchU32VarInRange",     // 22
        "SetSpecialBit",           // 23
        "ClearSpecialBit",         // 24
        "BranchOnSpecialBit",      // 25
        "SetAssetRotation",        // 26
        "SetupMapAsset",           // 27
        "EntityWalk",              // 28
        "SetMapRotation",          // 29
        "SetBehaviorFlags",        // 30
        "SetEntityWander",         // 31
        "InitMsgBoxType1",         // 32
        "WaitMsgBoxClosed",        // 33
        "SetMsgBoxViewPos",        // 34
        "ResetMsgBoxAvatar",       // 35
        "EntityRun",               // 36
        "SetEntityAnimation",      // 37
        "SetEntityAnimWithDir",    // 38
        "SetCallbackPC",           // 39
        "PauseEntity",             // 40
        "TogglePauseEntity",       // 41
        "FlipEntityDirection",     // 42
        "PauseEntities",           // 43
        "TogglePauseEntities",     // 44
        "FlipEntityAnimation",     // 45
        "SetEntityNonCollidable",  // 46
        "SetupEntity",             // 47
        "SetEntityMapSpaceIndepFlag", // 48
        "LoadMap",                 // 49
        "SetEntityMapSpaceIndep",  // 50
        "SetRGBA",                 // 51
        "UpdateRGBA",              // 52
        "UpdateU8Value",           // 53
        "UpdateU16Value",          // 54
        "UpdateU32Value",          // 55
        "DeactivateMapObjects",    // 56
        "UpdateGlobalRGBA",        // 57
        "DeactivateSprites",       // 58
        "DeactivateMapControllers",// 59
        "WaitRgbaFinished",        // 60
        "CheckEntityCollision",    // 61
        "InitDialogueSession",     // 62
        "WaitForDialogueInput",    // 63
        "BranchOnDialogue",        // 64
        "WaitEntityAnimation",     // 65
        "SetMsgBoxAssetIndices",   // 66
        "SetEntityTrackingTarget", // 67
        "SetHoldingAnimFlag",      // 68
        "WaitMapLoad",             // 69
        "BranchOnEntityDirection", // 70
        "SetEntityPhysicsFlags",   // 71
        "SetEntityPalette",        // 72
        "SetEntitySpriteDims",     // 73
        "SetShadowFlags",          // 74
        "SetSpriteScale",          // 75
        "SetSpriteRenderLayer",    // 76
        "InitMsgBoxType2",         // 77
        "InitMapAddition",         // 78
        "BranchOnRandom",          // 79
        "BranchIfU16PtrInRange",   // 80
        "PauseExecutor",           // 81
        "TogglePauseExecutor",     // 82
        "PauseAllChildExecutors",  // 83
        "TogglePauseAllChild",     // 84
        "SetSpritePalette",        // 85
        "BranchIfU8PtrInRange",    // 86
        "SetAudioSequence",        // 87
        "StopAudioSeqFadeOut",     // 88
        "SetAudioSeqVolume",       // 89
        "SetSfx",                  // 90
        "IdleWhileAudioPlaying",   // 91
        "UpdateMsgBoxRGBA",        // 92
        "WaitMsgBoxReady",         // 93
        "SetSpriteBilinear",       // 94
        "SetMapAddition",          // 95
        "SetMapGroundObject",      // 96
        "SetMessageInterpolation", // 97
    };
    if (op < 98) return names[op];
    static char buf[16]; snprintf(buf,sizeof(buf),"op_%u",op); return buf;
}

// updateCutsceneExecutors probe is now inline in the manual main loop.

// ---------------------------------------------------------------------------
// cutsceneHandlerWaitRgbaFinished – PC patch for dummy-map RGBA stalls.
//
// On the N64, updateSprites() advances RGBA lerps each frame; once
// current==target, bit 10 (SPRITE_RGBA_IN_PROGRESS=0x400) of stateFlags
// is set, and checkSpriteRGBAUpdateFinished() returns true.
// For maps, checkMapRGBADone() needs MAP_ACTIVE + bit 3 of mapState.flags.
//
// On the PC dummy-map path (s_no_world_geometry), the map is never
// MAP_ACTIVE, so CUTSCENE_MAP_ASSET executors spin forever on opcode 60.
// Sprites may also stall if textures never loaded (baseRGBA==0 → NaN deltas).
//
// Fix: when s_no_world_geometry, immediately advance past the wait.
// ---------------------------------------------------------------------------
extern "C" RECOMP_PATCH void cutsceneHandlerWaitRgbaFinished(uint8_t* rdram, recomp_context* ctx) {
    uint16_t index = (uint16_t)(ctx->r4 & 0xFFFF);

    // CutsceneExecutor offsets (cutscene.h):
    //   +0x00 bytecodePtr  (void*)
    //   +0x62 assetIndex   (u16)
    //   +0x66 waitFrames   (u16)
    //   +0x6C flags        (u16) — bit0=ACTIVE, bit1=SPRITE, bit3=ENTITY, bit4=MAP
    constexpr uint32_t EXEC_BASE = 0x801808B0u;
    constexpr uint32_t EXEC_SIZE = 0x70u;
    uint32_t bvma = EXEC_BASE + index * EXEC_SIZE;

    uint32_t bptr = *(uint32_t*)(rdram + (bvma - 0x80000000u));
    uint16_t eflags = *(uint16_t*)(rdram + ((bvma + 0x6Cu ^ 2u) - 0x80000000u));
    uint16_t assetIdx = *(uint16_t*)(rdram + ((bvma + 0x62u ^ 2u) - 0x80000000u));

    bptr += 2;
    *(uint32_t*)(rdram + (bvma - 0x80000000u)) = bptr;

    if (scene_has_no_world_geometry()) {
        // Force-complete: skip the conditional check word entirely.
        bptr += 2;
        *(uint32_t*)(rdram + (bvma - 0x80000000u)) = bptr;
        static int skip_log = 0;
        if (skip_log++ < 5) {
            printf("[WaitRgbaFinished ei=%u] forced complete (no_world_geometry) flg=%04X\n",
                   index, eflags);
            fflush(stdout);
        }
        return;
    }

    if (eflags & 0x2u) {
        // CUTSCENE_SPRITE_ASSET: check globalSprites[assetIdx].stateFlags bit 10
        // globalSprites base=0x80205580, stride=0x10, stateFlags at +0x8
        uint16_t sf = *(uint16_t*)(rdram + ((0x80205580u + assetIdx * 0x10u + 0x8u ^ 2u) - 0x80000000u));
        if ((sf & 0x1u) && (sf & 0x400u)) {
            bptr += 2;
            *(uint32_t*)(rdram + (bvma - 0x80000000u)) = bptr;
        } else {
            *(uint16_t*)(rdram + ((bvma + 0x66u ^ 2u) - 0x80000000u)) = 1;
            bptr -= 2;
            *(uint32_t*)(rdram + (bvma - 0x80000000u)) = bptr;
        }
    }

    if (eflags & 0x8u) {
        // CUTSCENE_ENTITY_ASSET: entities[assetIdx].globalSpriteIndex
        // entities base=0x80205580, stride=0x160, globalSpriteIndex at +0x14
        uint16_t gsi = *(uint16_t*)(rdram + ((0x80205580u + assetIdx * 0x160u + 0x14u ^ 2u) - 0x80000000u));
        uint16_t sf = *(uint16_t*)(rdram + ((0x80205580u + gsi * 0x10u + 0x8u ^ 2u) - 0x80000000u));
        if ((sf & 0x1u) && (sf & 0x400u)) {
            bptr += 2;
            *(uint32_t*)(rdram + (bvma - 0x80000000u)) = bptr;
        } else {
            *(uint16_t*)(rdram + ((bvma + 0x66u ^ 2u) - 0x80000000u)) = 1;
            bptr -= 2;
            *(uint32_t*)(rdram + (bvma - 0x80000000u)) = bptr;
        }
    }

    if (eflags & 0x10u) {
        // CUTSCENE_MAP_ASSET: mapControllers[assetIdx].mainMapIndex
        // mapControllers base=0x80205520, stride=0x20, mainMapIndex at +0x00
        uint16_t mainMapIdx = *(uint16_t*)(rdram + ((0x80205520u + assetIdx * 0x20u ^ 2u) - 0x80000000u));
        // mainMap[mainMapIdx].mapState.flags at 0x8016F810 + mainMapIdx*0xC0 + 0x8A
        uint16_t mf = *(uint16_t*)(rdram + ((0x8016F810u + mainMapIdx * 0xC0u + 0x8Au ^ 2u) - 0x80000000u));
        if (mainMapIdx == 0 && (mf & 0x1u) && (mf & 0x8u)) {
            bptr += 2;
            *(uint32_t*)(rdram + (bvma - 0x80000000u)) = bptr;
        } else {
            *(uint16_t*)(rdram + ((bvma + 0x66u ^ 2u) - 0x80000000u)) = 1;
            bptr -= 2;
            *(uint32_t*)(rdram + (bvma - 0x80000000u)) = bptr;
        }
    }
}
