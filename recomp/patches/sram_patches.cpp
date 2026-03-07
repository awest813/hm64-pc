/**
 * sram_patches.cpp – PC-native SRAM (battery-backed save) implementation.
 *
 * HM64 saves game data to a 32 KB cartridge SRAM at physical address
 * 0x08000000.  On real hardware, sramLoad()/sramWrite() in
 * src/system/memory.c use osEPiStartDma to transfer data between SRAM and
 * RDRAM.  Both osEPiStartDma and osRecvMesg are stubbed as no-ops in the PC
 * runtime, so without these patches save/load operations would silently do
 * nothing and the game would always see blank save slots.
 *
 * Layout of the N64 cartridge SRAM (SRAM_SIZE = 0x8000 bytes):
 *   0x0000 – 0x3FFF  Game save slots (4 slots × 0x1000 bytes each)
 *                      slot n starts at offset (n << 12)
 *   0x4000 – 0x7FFF  Farm-ranking data (slots × 0x100 bytes)
 *
 * On PC the same layout is preserved in a flat file "hm64.sav" placed in
 * the current working directory (i.e. next to the hm64_pc binary).  The
 * file is created on the first save and zero-filled to SRAM_SIZE so that
 * later partial reads of unwritten regions behave like blank SRAM.
 *
 * Mapping:  physical devAddr 0x08000000 + offset  →  file offset
 *   sramLoad (devAddr, rdramAddr, size)  →  fread  at offset
 *   sramWrite(devAddr, rdramAddr, size)  →  fwrite at offset
 *
 * N64 virtual → host pointer:  rdram + (vaddr & 0x1FFFFFFF)
 *   K0 cached   0x80XXXXXX → strip top 3 bits
 *   K1 uncached 0xA0XXXXXX → strip top 3 bits
 */

#include "librecomp/recomp.h"

#include <cstdio>
#include <cstdint>
#include <cstring>

// Physical base address of the N64 cartridge SRAM (PI domain 2).
static constexpr uint32_t SRAM_BASE = 0x08000000u;
// Total SRAM capacity in bytes.
static constexpr uint32_t SRAM_SIZE = 0x8000u;
// File used to persist SRAM contents on PC.
static constexpr const char* SAVE_FILE = "hm64.sav";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Convert an N64 virtual address (K0 or K1) to a host pointer into RDRAM. */
static inline uint8_t* rdram_ptr(uint8_t* rdram, uint32_t vaddr) {
    return rdram + (vaddr & 0x1FFFFFFFu);
}

/**
 * Open (or create) the save file in read-write binary mode.
 *
 * If the file does not yet exist it is created and zero-padded to SRAM_SIZE,
 * mirroring the blank state of a fresh cartridge battery-backed SRAM.
 *
 * Returns a valid FILE* on success, nullptr on failure (error already logged).
 */
static FILE* open_save_file_rw() {
    // Try to open an existing file for update.
    FILE* f = fopen(SAVE_FILE, "r+b");
    if (f) {
        return f;
    }

    // File does not exist – create it and zero-fill to SRAM_SIZE.
    f = fopen(SAVE_FILE, "w+b");
    if (!f) {
        fprintf(stderr, "[hm64_pc] sram: failed to create %s: ", SAVE_FILE);
        perror(nullptr);
        return nullptr;
    }

    // Write a block of zeros in one shot.
    // Declared static so it lives in BSS (not on the stack); zero-filled
    // by the C runtime before main() is called.
    static const uint8_t zeros[SRAM_SIZE] = {};
    if (fwrite(zeros, 1, SRAM_SIZE, f) != SRAM_SIZE) {
        fprintf(stderr, "[hm64_pc] sram: failed to initialize %s\n", SAVE_FILE);
        fclose(f);
        return nullptr;
    }
    // Leave the file pointer at the beginning so the caller can seek.
    rewind(f);
    return f;
}

// ---------------------------------------------------------------------------
// sramLoad – read SRAM data from the PC save file into RDRAM
// ---------------------------------------------------------------------------
// Original signature:  void sramLoad(void *devAddr, void *dramAddr, u32 size)
//   a0 (r4) = devAddr  – N64 physical SRAM address (0x08000000-based)
//   a1 (r5) = dramAddr – N64 virtual RDRAM address to fill
//   a2 (r6) = size     – byte count
RECOMP_PATCH void sramLoad(uint8_t* rdram, recomp_context* ctx) {
    uint32_t dev_addr  = (uint32_t)ctx->r4;
    uint32_t dram_virt = (uint32_t)ctx->r5;
    uint32_t size      = (uint32_t)ctx->r6;

    if (size == 0) {
        return;
    }
    if (dev_addr < SRAM_BASE || dev_addr >= SRAM_BASE + SRAM_SIZE) {
        fprintf(stderr, "[hm64_pc] sramLoad: devAddr 0x%08X out of range\n", dev_addr);
        return;
    }

    uint32_t offset = dev_addr - SRAM_BASE;
    // Clamp to avoid reads beyond the SRAM image.
    if (offset + size > SRAM_SIZE) {
        size = SRAM_SIZE - offset;
    }

    uint8_t* dest = rdram_ptr(rdram, dram_virt);

    FILE* f = fopen(SAVE_FILE, "rb");
    if (!f) {
        // No save file yet – treat as blank SRAM (all zeros).
        memset(dest, 0, size);
        return;
    }

    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        memset(dest, 0, size);
    } else {
        size_t nread = fread(dest, 1, size, f);
        if (nread < size) {
            // Partial read (file shorter than expected): zero the remainder.
            memset(dest + nread, 0, size - nread);
        }
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// sramWrite – write RDRAM data to the PC save file
// ---------------------------------------------------------------------------
// Original signature:  void sramWrite(void *devAddr, void *dramAddr, u32 size)
//   a0 (r4) = devAddr  – N64 physical SRAM address (0x08000000-based)
//   a1 (r5) = dramAddr – N64 virtual RDRAM address to read from
//   a2 (r6) = size     – byte count
RECOMP_PATCH void sramWrite(uint8_t* rdram, recomp_context* ctx) {
    uint32_t dev_addr  = (uint32_t)ctx->r4;
    uint32_t dram_virt = (uint32_t)ctx->r5;
    uint32_t size      = (uint32_t)ctx->r6;

    if (size == 0) {
        return;
    }
    if (dev_addr < SRAM_BASE || dev_addr >= SRAM_BASE + SRAM_SIZE) {
        fprintf(stderr, "[hm64_pc] sramWrite: devAddr 0x%08X out of range\n", dev_addr);
        return;
    }

    uint32_t offset = dev_addr - SRAM_BASE;
    if (offset + size > SRAM_SIZE) {
        size = SRAM_SIZE - offset;
    }

    const uint8_t* src = rdram_ptr(rdram, dram_virt);

    FILE* f = open_save_file_rw();
    if (!f) {
        return; // error already logged in open_save_file_rw()
    }

    if (fseek(f, (long)offset, SEEK_SET) != 0) {
        fprintf(stderr, "[hm64_pc] sramWrite: seek to offset 0x%X failed\n", offset);
    } else if (fwrite(src, 1, size, f) != size) {
        fprintf(stderr, "[hm64_pc] sramWrite: short write at offset 0x%X\n", offset);
    }
    fclose(f);
}
