// Included after the actual generated renderer functions by test_map_rendering.py.
// Synthetic MIPS memory uses the game's word-swapped layout and o32 ABI.
int main() {
    std::vector<uint8_t> memory(0x800000);
    uint8_t* rdram = memory.data();
    constexpr int32_t map = int32_t(0x80600000);
    constexpr int32_t dl = int32_t(0x80700000);
    constexpr int32_t textures = int32_t(0x80710000);
    constexpr int32_t palettes = int32_t(0x80711000);
    constexpr int32_t coordinates = int32_t(0x80712000);
    constexpr int32_t stack = int32_t(0x807F0000);

    // One four-vertex tile, no extra triangle commands, and a 16x16 CI texture.
    MEM_W(0x30, map) = coordinates;
    MEM_B(0x3B, map) = 4;
    MEM_W(0x10, map) = textures;
    MEM_W(0x14, map) = palettes;
    MEM_W(0, textures) = 4;
    MEM_W(0, palettes) = 4;
    MEM_H(8, textures) = 0x1000; // Texture dimensions are little-endian assets.
    MEM_H(10, textures) = 0x1000;

    for (auto function : {appendTileToDL, prepareTileTextures}) {
        recomp_context ctx{};
        ctx.r4 = dl;
        ctx.r5 = map;
        ctx.r6 = 0;
        ctx.r29 = stack;
        ctx.f_odd = &ctx.f0.u32h;
        ctx.mips3_float_mode = 1;
        ctx.r16 = 0x20000; // renderTiles uses s0 to address the visibility grid.
        ctx.r17 = 0x123456;
        ctx.r18 = 0x234567;
        ctx.r19 = 0x345678;
        ctx.r20 = 0x456789;
        ctx.r21 = 0x56789A;
        ctx.r22 = 0x6789AB;
        ctx.r23 = map;
        ctx.r30 = 0x789ABC;
        function(rdram, &ctx);
        assert(ctx.r16 == 0x20000);
        assert(ctx.r17 == 0x123456);
        assert(ctx.r18 == 0x234567);
        assert(ctx.r19 == 0x345678);
        assert(ctx.r20 == 0x456789);
        assert(ctx.r21 == 0x56789A);
        assert(ctx.r22 == 0x6789AB);
        assert(ctx.r23 == uint64_t(int64_t(map)));
        assert(ctx.r29 == uint64_t(int64_t(stack)));
        assert(ctx.r30 == 0x789ABC);
        assert(ctx.r2 > uint64_t(int64_t(dl)));
        assert(ctx.r2 < uint64_t(int64_t(dl + 0x1000)));
        if (function == appendTileToDL) {
            assert(uint32_t(MEM_W(0, dl)) == 0x01004008); // F3DEX2 vertex load.
            assert(MEM_HU(0x1A612, map) == 4);
        }
    }
    // The scroll timer starts at offset 0x6A, so it must use halfword stores.
    // A cast to a four-byte-aligned Interpolator used to zero its rate and hang
    // the opening dialogue on its third line.
    constexpr int32_t box = int32_t(0x80188B70);
    MEM_W(0xA4, box) = 0x43; // active, initialized, scrolling down
    MEM_B(0x61, box) = 14;
    MEM_B(0x9C, box) = 2;
    MEM_B(0x97, box) = 3;
    MEM_H(0x68, box) = 0x1234; // Neighbor must survive timer initialization.
    recomp_context ctx{};
    ctx.r4 = int32_t(box + 0x6A);
    ctx.r5 = 1;
    ctx.r6 = 0;
    ctx.r29 = stack;
    initializeMessageBoxInterpolator(rdram, &ctx);
    assert(MEM_H(0x68, box) == 0x1234);
    assert(MEM_H(0x6A, box) == 1);
    for (int frame = 0; frame < 16; ++frame) {
        ctx.r4 = 0;
        updateScrollDownAnimation(rdram, &ctx);
        assert(bool(MEM_W(0xA4, box) & 0x40) == (frame < 15));
    }
    assert(MEM_BU(0x94, box) == 1);
    assert(MEM_BU(0x97, box) == 2);
    ctx.r4 = int32_t(box + 0x6A);
    ctx.r5 = -4;
    ctx.r6 = 0;
    initializeMessageBoxInterpolator(rdram, &ctx);
    for (int frame = 0; frame < 8; ++frame) {
        ctx.r4 = int32_t(box + 0x6A);
        stepMessageBoxInterpolator(rdram, &ctx);
        assert(ctx.r2 == (frame % 4 == 0 ? 1 : 0));
    }
    assert(MEM_H(0x6E, box) == 2);
    puts("Map rendering preserves registers; dialogue scrolling completes in 16 frames.");
}
