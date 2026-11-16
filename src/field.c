#include <stddef.h>
#include <string.h>
#include <zvb_dma.h>
#include "field.h"

/* Generated from assets/field.gif by gif2zeal and embedded by embed_assets() */
extern uint8_t _field_ztp_start;
extern uint8_t _field_ztp_end;
extern uint8_t _field_zts_start;
extern uint8_t _field_zts_end;

#define FIELD_TILE_BYTES 128

/*
 * The DMA controller holds the bus for the duration of one descriptor.  The
 * ZGDK sprite uploader found that 32-byte chunks preserve keyboard release
 * events at the default clock divider (0x56); larger chunks can starve the
 * keyboard interrupt.  Four chunks make one tile.
 */
#define FIELD_DMA_CHUNK     32
#define FIELD_DMA_CHUNKS    (FIELD_TILE_BYTES / FIELD_DMA_CHUNK)
#define FIELD_DMA_ALIGN     128

static __sfr __at(0xf0) field_mmu_page0;

/* Packed logical field, two bits per 4x4-pixel cell. */
static uint8_t field_map[FIELD_MAP_BYTES];

/* Current hardware tile index for every playfield tile. */
static uint8_t tile_grid[FIELD_BOARD_CELLS];

/*
 * One dirty bit per hardware tile.  Five bytes hold one 40-tile row, which
 * avoids divisions when walking the dirty set.
 */
static uint8_t tile_dirty[FIELD_BOARD_H][5];

/*
 * Bounding box of the cells that became TRAIL since the last capture.  The
 * capture only has to inspect this box instead of the whole map.
 */
static uint8_t trail_x0, trail_x1;
static uint8_t trail_y0, trail_y1;  /* map rows, not absolute screen rows */

/* One bit per field-map byte that currently contains at least one TRAIL cell.
 * This avoids scanning the whole bounding box of a large loop. */
static uint8_t trail_map[FIELD_MAP_H][5];
static const uint8_t trail_bit[16] = {
    0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0
};

/* Lookup table: one field-map byte -> eight 4bpp pixels (16 px, two per byte). */
/* Colour of one 4x4 logical cell, expanded in field_build_tile(). */
static uint8_t cell_color[4];

/* Lookup table: one field-map byte -> 4-bit mask of the cells that are SEA. */
static uint8_t open_mask4[256];

/* Lookup table: number of cells whose even (low) bit is set in a byte. */
static uint8_t pair_count[256];

/* Sea cells captured since the last field_reset(), and the cached percent. */
static uint16_t captured_cells;
static uint8_t captured_percent;

/* Staging buffer for one generated 16x16 4bpp tile. */
static uint8_t tile_scratch[FIELD_TILE_BYTES];

/*
 * The HUD message uses palette slot 5, which no tile or sprite uses.  A copy
 * of the 16-colour palette is kept in RAM so the slot can be toggled between
 * the yellow of slot 4 and black without touching any other colour.
 */
static uint8_t hud_palette[32];
static uint8_t hud_yellow_lo;
static uint8_t hud_yellow_hi;

/*
 * Spider sprite, uploaded to TILE_SPIDER once at boot.  Palette 0 is
 * transparent, 7 is white (body and legs) and 6 is red (eyes).  Each byte
 * holds two 4-bit pixels, high nibble first.
 */
static const uint8_t spider_tile[FIELD_TILE_BYTES] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00,
    0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x00, 0x07, 0x00, 0x00,
    0x00, 0x00, 0x07, 0x00, 0x00, 0x70, 0x00, 0x00,
    0x00, 0x00, 0x07, 0x77, 0x77, 0x70, 0x00, 0x00,
    0x00, 0x00, 0x77, 0x76, 0x67, 0x77, 0x00, 0x00,
    0x00, 0x07, 0x77, 0x77, 0x77, 0x77, 0x70, 0x00,
    0x00, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x00,
    0x00, 0x07, 0x77, 0x77, 0x77, 0x77, 0x70, 0x00,
    0x00, 0x00, 0x77, 0x77, 0x77, 0x77, 0x00, 0x00,
    0x00, 0x00, 0x07, 0x77, 0x77, 0x70, 0x00, 0x00,
    0x00, 0x00, 0x07, 0x00, 0x00, 0x70, 0x00, 0x00,
    0x00, 0x00, 0x70, 0x00, 0x00, 0x07, 0x00, 0x00,
    0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00,
    0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00,
};

/*
 * HUD font: 8x8 glyphs scaled 2x into 16x16 4bpp tiles at boot.  The order is
 * digits 0-9, the full A-Z alphabet, ':', '%', flag, clock, '>' and '<'.
 * It matches TILE_HUD_DIGIT_BASE..TILE_HUD_LT.  Bit 7 is the leftmost pixel.
 */
static const uint8_t hud_glyphs[42][8] = {
    { 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00 }, /* 0 */
    { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 }, /* 1 */
    { 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00 }, /* 2 */
    { 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00 }, /* 3 */
    { 0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00 }, /* 4 */
    { 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00 }, /* 5 */
    { 0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00 }, /* 6 */
    { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 }, /* 7 */
    { 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00 }, /* 8 */
    { 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00 }, /* 9 */
    { 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00 }, /* A */
    { 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00 }, /* B */
    { 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00 }, /* C */
    { 0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00 }, /* D */
    { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00 }, /* E */
    { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00 }, /* F */
    { 0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00 }, /* G */
    { 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00 }, /* H */
    { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 }, /* I */
    { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00 }, /* J */
    { 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00 }, /* K */
    { 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00 }, /* L */
    { 0x66, 0x7E, 0x7E, 0x76, 0x66, 0x66, 0x66, 0x00 }, /* M */
    { 0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00 }, /* N */
    { 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 }, /* O */
    { 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00 }, /* P */
    { 0x3C, 0x66, 0x66, 0x66, 0x6E, 0x6C, 0x36, 0x00 }, /* Q */
    { 0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00 }, /* R */
    { 0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00 }, /* S */
    { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 }, /* T */
    { 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 }, /* U */
    { 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00 }, /* V */
    { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 }, /* W */
    { 0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00 }, /* X */
    { 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00 }, /* Y */
    { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00 }, /* Z */
    { 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00 }, /* : */
    { 0x62, 0x64, 0x08, 0x10, 0x26, 0x46, 0x00, 0x00 }, /* % */
    { 0x40, 0x7C, 0x7C, 0x78, 0x70, 0x40, 0x40, 0x40 }, /* flag */
    { 0x3C, 0x42, 0x99, 0x99, 0x81, 0x81, 0x42, 0x3C }, /* clock */
    { 0x40, 0x20, 0x10, 0x08, 0x10, 0x20, 0x40, 0x00 }, /* > */
    { 0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08, 0x00 }, /* < */
};

/* Expand one 8x8 glyph, doubled, into a 16x16 4bpp tile.  `down` shifts the
 * glyph down by that many source rows (2 pixels each). */
static void field_build_hud_tile(const uint8_t* glyph, uint8_t* out,
                                 uint8_t color, uint8_t down) {
    uint8_t y;

    for(y = 0; y < 16; y++) {
        uint8_t sy = (uint8_t)(y >> 1);
        uint8_t bits = (sy >= down) ? glyph[sy - down] : 0;
        uint8_t x;

        for(x = 0; x < 8; x++) {
            uint8_t c = (bits & (uint8_t)(0x80 >> x)) ? color : 0;

            out[(uint16_t)y * 8 + x] = (uint8_t)(c | (uint8_t)(c << 4));
        }
    }
}

#define SEA_ROW0        (FIELD_SEA_Y0 - FIELD_MAP_Y0)
#define SEA_ROW1        (FIELD_SEA_Y1 - FIELD_MAP_Y0)
#define SEA_BYTE0       (FIELD_SEA_X0 / 4)
#define SEA_BYTE_COUNT  ((FIELD_SEA_X1 - FIELD_SEA_X0 + 1) / 4)
#define FIELD_SEA_CELLS ((uint16_t)((FIELD_SEA_X1 - FIELD_SEA_X0 + 1) * \
                                    (FIELD_SEA_Y1 - FIELD_SEA_Y0 + 1)))

static uint8_t tile_is_border(uint8_t tx, uint8_t ty) {
    return (tx == 0 || tx == FIELD_BOARD_W - 1 ||
            ty == 0 || ty == FIELD_BOARD_H - 1);
}

static uint8_t field_border_tile(uint8_t tx, uint8_t ty) {
    if(ty == 0) {
        if(tx == 0) return TILE_CORNER_TL;
        if(tx == FIELD_BOARD_W - 1) return TILE_CORNER_TR;
        return TILE_WALL_TOP;
    }

    if(ty == FIELD_BOARD_H - 1) {
        if(tx == 0) return TILE_CORNER_BL;
        if(tx == FIELD_BOARD_W - 1) return TILE_CORNER_BR;
        return TILE_WALL_BOTTOM;
    }

    if(tx == 0) return TILE_WALL_LEFT;
    if(tx == FIELD_BOARD_W - 1) return TILE_WALL_RIGHT;
    return TILE_SEA;
}

static void tile_dirty_set(uint8_t tx, uint8_t ty) {
    tile_dirty[ty][tx >> 3] |= (uint8_t)(1u << (tx & 7));
}

/* Raw map access; the caller must provide valid map coordinates. */
static uint8_t map_get(uint8_t cx, uint8_t cy) {
    uint8_t b;
    uint16_t off;

    if(cx >= FIELD_MAP_W || cy < FIELD_MAP_Y0 || cy >= FIELD_MAP_Y0 + FIELD_MAP_H) {
        return CELL_LAND;
    }

    off = (uint16_t)(cy - FIELD_MAP_Y0) * FIELD_MAP_STRIDE + (cx >> 2);
    b = field_map[off];
    return (uint8_t)((b >> ((cx & 3) << 1)) & 3);
}

uint8_t field_get_cell(uint8_t cx, uint8_t cy) {
    return map_get(cx, cy);
}

void field_set_cell(uint8_t cx, uint8_t cy, uint8_t state) {
    uint16_t off;
    uint8_t old;
    uint8_t value;
    uint8_t shift;
    uint8_t mask;

    if(cx >= FIELD_MAP_W || cy < FIELD_MAP_Y0 || cy >= FIELD_MAP_Y0 + FIELD_MAP_H) {
        return;
    }

    off = (uint16_t)(cy - FIELD_MAP_Y0) * FIELD_MAP_STRIDE + (cx >> 2);
    shift = (uint8_t)((cx & 3) << 1);
    mask = (uint8_t)(3u << shift);
    old = field_map[off];
    value = (uint8_t)((old & (uint8_t)~mask) | (uint8_t)(state << shift));

    if(value != old) {
        uint8_t fy = (uint8_t)(cy - FIELD_MAP_Y0);

        field_map[off] = value;
        tile_dirty_set((uint8_t)(cx >> 2), (uint8_t)(fy >> 2));

        if(state == CELL_TRAIL) {
            const uint8_t bx = (uint8_t)(cx >> 2);

            trail_map[fy][bx >> 3] |= (uint8_t)(1u << (bx & 7));
            if(cx < trail_x0) trail_x0 = cx;
            if(cx > trail_x1) trail_x1 = cx;
            if(fy < trail_y0) trail_y0 = fy;
            if(fy > trail_y1) trail_y1 = fy;
        }
    }
}

uint8_t field_cell_at_px(uint16_t px, uint16_t py) {
    return map_get((uint8_t)(px >> 2), (uint8_t)(py >> 2));
}

static void field_lut_init(void) {
    uint16_t idx;

    cell_color[CELL_SEA] = 0x00;
    cell_color[CELL_LAND] = 0x33;
    cell_color[CELL_TRAIL] = 0x22;
    cell_color[CELL_VISITED] = 0x00;

    for(idx = 0; idx < 256; idx++) {
        uint8_t mask = 0;
        uint8_t p;

        for(p = 0; p < 4; p++) {
            if(((idx >> (p << 1)) & 3) == CELL_SEA) {
                mask |= (uint8_t)(1u << p);
            }
        }
        open_mask4[idx] = mask;
        pair_count[idx] = (uint8_t)(((idx & 0x01) ? 1 : 0) +
                                    ((idx & 0x04) ? 1 : 0) +
                                    ((idx & 0x10) ? 1 : 0) +
                                    ((idx & 0x40) ? 1 : 0));
    }
}

/*
 * Build one 128-byte tile from four field-map bytes.  Each logical map byte
 * becomes an 8-byte pixel row, repeated four times (the cell is 4 px high).
 */
static void field_build_tile(const uint8_t* row_ptr, uint8_t* out) {
    uint8_t r;

    for(r = 0; r < 4; r++) {
        const uint8_t bits = *row_ptr;
        const uint8_t c0 = cell_color[(bits >> 0) & 3];
        const uint8_t c1 = cell_color[(bits >> 2) & 3];
        const uint8_t c2 = cell_color[(bits >> 4) & 3];
        const uint8_t c3 = cell_color[(bits >> 6) & 3];
        uint8_t y;

        for(y = 0; y < 4; y++) {
            out[0] = c0;
            out[1] = c0;
            out[2] = c1;
            out[3] = c1;
            out[4] = c2;
            out[5] = c2;
            out[6] = c3;
            out[7] = c3;
            out += 8;
        }
        row_ptr += FIELD_MAP_STRIDE;
    }
}

/* ------------------------------------------------------------------------- */
/* Dynamic tile pool                                                         */
/* ------------------------------------------------------------------------- */

#define FIELD_HASH_SIZE     512
#define FIELD_HASH_MASK     (FIELD_HASH_SIZE - 1)

static uint32_t dyn_hash_keys[FIELD_HASH_SIZE];
static uint8_t  dyn_hash_slots[FIELD_HASH_SIZE];
static uint8_t  dyn_hash_used[FIELD_HASH_SIZE];
static uint32_t dyn_slot_keys[TILE_DYNAMIC_COUNT];
static uint8_t  dyn_slot_used[TILE_DYNAMIC_COUNT];
static uint8_t  dyn_refs[256];
static uint8_t  tile_pool_stalled;

static void field_hash_clear(void) {
    memset(dyn_hash_used, 0, sizeof(dyn_hash_used));
}

static uint16_t field_hash_index(uint32_t key) {
    return (uint16_t)((key ^ (key >> 16)) & FIELD_HASH_MASK);
}

static uint8_t field_hash_find(uint32_t key) {
    uint16_t h = field_hash_index(key);

    while(dyn_hash_used[h]) {
        if(dyn_hash_keys[h] == key) {
            return dyn_hash_slots[h];
        }
        h = (uint16_t)((h + 1) & FIELD_HASH_MASK);
    }
    return 0;
}

static void field_hash_insert(uint32_t key, uint8_t slot) {
    uint16_t h = field_hash_index(key);

    while(dyn_hash_used[h]) {
        h = (uint16_t)((h + 1) & FIELD_HASH_MASK);
    }
    dyn_hash_used[h] = 1;
    dyn_hash_keys[h] = key;
    dyn_hash_slots[h] = slot;
}

void field_tiles_reset(void) {
    memset(dyn_slot_used, 0, sizeof(dyn_slot_used));
    field_hash_clear();
    tile_pool_stalled = 0;
}

/*
 * Free generated slots that are no longer referenced by the visible tile map
 * and rebuild the key cache from the remaining ones.  Called only when the
 * slot pool is exhausted, so the scan cost is paid rarely.  Returns non-zero
 * when at least one slot became free.
 */
static uint8_t field_tiles_gc(void) {
    uint16_t i;
    uint8_t freed = 0;

    memset(dyn_refs, 0, sizeof(dyn_refs));
    for(i = 0; i < FIELD_BOARD_CELLS; i++) {
        dyn_refs[tile_grid[i]] = 1;
    }

    for(i = 0; i < TILE_DYNAMIC_COUNT; i++) {
        if(dyn_slot_used[i] && !dyn_refs[TILE_DYNAMIC_BASE + i]) {
            dyn_slot_used[i] = 0;
            freed = 1;
        }
    }

    /* The existing hash is still valid when nothing was released. */
    if(freed) {
        field_hash_clear();
        for(i = 0; i < TILE_DYNAMIC_COUNT; i++) {
            if(dyn_slot_used[i]) {
                field_hash_insert(dyn_slot_keys[i], (uint8_t)(TILE_DYNAMIC_BASE + i));
            }
        }
    }

    return freed;
}

uint8_t field_tile_get(gfx_context* ctx, uint32_t key, const uint8_t* tile) {
    uint8_t found = field_hash_find(key);
    uint8_t off;

    if(found) {
        return found;
    }

    off = 0;
    while(off < TILE_DYNAMIC_COUNT && dyn_slot_used[off]) {
        off++;
    }

    if(off == TILE_DYNAMIC_COUNT) {
        /*
         * Run the collector at most once per render pass when it cannot free
         * anything: otherwise a board that needs more patterns than the pool
         * holds would rescan the whole tile map for every new tile, which
         * stalls the game for seconds right after a large capture.
         */
        if(!tile_pool_stalled) {
            if(!field_tiles_gc()) {
                tile_pool_stalled = 1;
            }
            off = 0;
            while(off < TILE_DYNAMIC_COUNT && dyn_slot_used[off]) {
                off++;
            }
        }
        if(off == TILE_DYNAMIC_COUNT) {
            return TILE_SEA;    /* pool exhausted: renderer falls back to LAND */
        }
    }

    {
        const uint8_t index = (uint8_t)(TILE_DYNAMIC_BASE + off);

        field_tile_upload(ctx, index, tile);
        dyn_slot_used[off] = 1;
        dyn_slot_keys[off] = key;
        field_hash_insert(key, index);
        return index;
    }
}

/* ------------------------------------------------------------------------- */
/* DMA / tileset upload                                                      */
/* ------------------------------------------------------------------------- */

/*
 * DMA working set.  Both the descriptor array and the staging buffer must not
 * cross a 16 KiB MMU page, otherwise the single-page virtual-to-physical
 * translation performed by the SDK would point at the wrong physical address.
 * Aligning to 128 bytes guarantees that (16 KiB is a multiple of 128).
 * Everything is pointed to at runtime because SDCC does not support aligned
 * static objects.
 */
static uint8_t dma_desc_storage[FIELD_DMA_CHUNKS * sizeof(zvb_dma_descriptor_t)
                                + FIELD_DMA_ALIGN - 1];
static uint8_t dma_stage_storage[FIELD_TILE_BYTES + FIELD_DMA_ALIGN - 1];
static zvb_dma_descriptor_t* dma_desc;
static uint8_t* dma_stage;
static uint8_t dma_ready;
static uint8_t dma_probe_src;
static uint8_t dma_probe_dst;

static uint8_t field_backup_page;

static void* field_align128(void* ptr) {
    const uint16_t aligned = (uint16_t)(((uint16_t)ptr + (FIELD_DMA_ALIGN - 1))
                                        & ~(uint16_t)(FIELD_DMA_ALIGN - 1));
    return (void*)aligned;
}

static void field_dma_init(void) {
    dma_desc = (zvb_dma_descriptor_t*)field_align128(dma_desc_storage);
    dma_stage = (uint8_t*)field_align128(dma_stage_storage);

    /* One-byte RAM-to-RAM probe: DMA may be absent or disconnected. */
    dma_probe_src = 0xa5;
    dma_probe_dst = 0;
    {
        zvb_dma_descriptor_t* probe = &dma_desc[0];
        memset(probe, 0, sizeof(*probe));
        probe->length = 1;
        probe->flags.raw = DMA_DESC_LAST;
        zvb_dma_set_read_virt(probe, &dma_probe_src);
        zvb_dma_set_write_virt(probe, &dma_probe_dst);

        /* zvb_dma_start_transfer() leaves the peripheral bank selected. */
        {
            const uint8_t dev = zvb_config_dev_idx;
            zvb_dma_start_transfer(probe);
            zvb_config_dev_idx = dev;
        }
    }

    dma_ready = (dma_probe_dst == dma_probe_src);
    if(!dma_ready) {
        return;
    }

    {
        const uint32_t src = zvb_dma_virt_to_phys(dma_stage);
        uint8_t i;

        for(i = 0; i < FIELD_DMA_CHUNKS; i++) {
            zvb_dma_descriptor_t* desc = &dma_desc[i];
            const uint32_t chunk = src + (uint16_t)i * FIELD_DMA_CHUNK;

            memset(desc, 0, sizeof(*desc));
            desc->length = FIELD_DMA_CHUNK;
            desc->flags.raw = DMA_DESC_LAST;
            desc->rd_addr_lo = (uint16_t)chunk;
            desc->rd_addr_hi = (uint8_t)(chunk >> 16);
        }
    }
}

static void field_dma_upload(uint8_t index, const uint8_t* tile) {
    uint32_t dst;
    uint8_t i;

    memcpy(dma_stage, tile, FIELD_TILE_BYTES);
    dst = (uint32_t)VID_MEM_TILESET_ADDR + (uint16_t)index * FIELD_TILE_BYTES;

    for(i = 0; i < FIELD_DMA_CHUNKS; i++) {
        dma_desc[i].wr_addr_lo = (uint16_t)dst;
        dma_desc[i].wr_addr_hi = (uint8_t)(dst >> 16);
        dst += FIELD_DMA_CHUNK;
    }

    {
        const uint32_t desc_phys = zvb_dma_virt_to_phys(dma_desc);
        const uint8_t dev = zvb_config_dev_idx;

        zvb_config_dev_idx = ZVB_PERI_DMA_IDX;
        for(i = 0; i < FIELD_DMA_CHUNKS; i++) {
            const uint32_t addr = desc_phys
                                + (uint16_t)i * sizeof(zvb_dma_descriptor_t);

            zvb_peri_dma_addr0 = (uint8_t)addr;
            zvb_peri_dma_addr1 = (uint8_t)(addr >> 8);
            zvb_peri_dma_addr2 = (uint8_t)(addr >> 16);
            zvb_peri_dma_ctrl = ZVB_PERI_DMA_CTRL_START;
        }
        zvb_config_dev_idx = dev;
    }
}

void field_tile_upload(gfx_context* ctx, uint8_t index, const uint8_t* tile) {
    if(dma_ready) {
        field_dma_upload(index, tile);
        return;
    }

    gfx_tileset_load_none(ctx, (uint8_t*)tile, FIELD_TILE_BYTES,
                          (uint16_t)index * FIELD_TILE_BYTES, 0, 0);
}

/* ------------------------------------------------------------------------- */
/* Board rendering                                                           */
/* ------------------------------------------------------------------------- */

#define FIELD_SMALL_CHANGES 16

static uint8_t change_x[FIELD_SMALL_CHANGES];
static uint8_t change_y[FIELD_SMALL_CHANGES];
static uint8_t change_tile[FIELD_SMALL_CHANGES];

void field_render_dirty(gfx_context* ctx) {
    uint8_t n = 0;
    uint8_t full = 0;
    uint8_t ty;

    tile_pool_stalled = 0;

    for(ty = 0; ty < FIELD_BOARD_H; ty++) {
        uint8_t byte;

        for(byte = 0; byte < 5; byte++) {
            uint8_t bits = tile_dirty[ty][byte];
            uint8_t bit;

            if(!bits) {
                continue;
            }
            tile_dirty[ty][byte] = 0;

            for(bit = 0; bit < 8; bit++) {
                uint8_t tx;
                uint8_t tile;
                uint16_t idx;

                if(!(bits & (uint8_t)(1u << bit))) {
                    continue;
                }

                tx = (uint8_t)(byte * 8 + bit);
                if(tx >= FIELD_BOARD_W) {
                    break;
                }

                idx = (uint16_t)ty * FIELD_BOARD_W + tx;

                if(tile_is_border(tx, ty)) {
                    tile = field_border_tile(tx, ty);
                } else {
                    const uint8_t* rp = &field_map[(uint16_t)(ty * 4) *
                                                   FIELD_MAP_STRIDE + tx];
                    uint8_t p0 = rp[0];
                    uint8_t p1 = rp[FIELD_MAP_STRIDE];
                    uint8_t p2 = rp[FIELD_MAP_STRIDE * 2];
                    uint8_t p3 = rp[FIELD_MAP_STRIDE * 3];

                    if((p0 | p1 | p2 | p3) == 0) {
                        tile = TILE_SEA;
                    } else if(p0 == 0x55 && p1 == 0x55 &&
                              p2 == 0x55 && p3 == 0x55) {
                        tile = TILE_LAND;
                    } else {
                        uint32_t key = (uint32_t)p0 |
                                       ((uint32_t)p1 << 8) |
                                       ((uint32_t)p2 << 16) |
                                       ((uint32_t)p3 << 24);

                        field_build_tile(rp, tile_scratch);
                        tile = field_tile_get(ctx, key, tile_scratch);
                        if(tile == TILE_SEA) {
                            tile = TILE_LAND;
                        }
                    }
                }

                if(tile_grid[idx] != tile) {
                    tile_grid[idx] = tile;

                    if(n < FIELD_SMALL_CHANGES) {
                        change_x[n] = tx;
                        change_y[n] = ty;
                        change_tile[n] = tile;
                        n++;
                    } else {
                        full = 1;
                    }
                }
            }
        }
    }

    if(full) {
        field_render_tiles(ctx);
        return;
    }

    for(ty = 0; ty < n; ty++) {
        gfx_tilemap_place(ctx, change_tile[ty], 0,
                          change_x[ty], (uint8_t)(change_y[ty] + FIELD_TOP));
    }
}

void field_render_tiles(gfx_context* ctx) {
    field_backup_page = ctx->backup_page;

    __asm__("di");
    field_mmu_page0 = VID_MEM_PHYS_ADDR_START >> 14;

    /*
     * Layer0 is 80 tiles wide while the playfield is 40 tiles wide.  Copy one
     * 40-byte row per iteration with LDIR, then step the destination over the
     * 40 unused columns.  The playfield starts at tile row FIELD_TOP, so the
     * first destination is 2 * 80 = 0x00a0 and there are 28 rows.
     */
    __asm__(
        "ld hl, #_tile_grid\n"
        "ld de, #0x00a0\n"
        "ld b, #0x1c\n"
        "_field_render_row:\n"
        "push bc\n"
        "ld bc, #0x0028\n"
        "ldir\n"
        "pop bc\n"
        "ld a, e\n"
        "add a, #0x28\n"
        "ld e, a\n"
        "jr nc, _field_render_row_nc\n"
        "inc d\n"
        "_field_render_row_nc:\n"
        "djnz _field_render_row\n"
    );

    field_mmu_page0 = field_backup_page;
    __asm__("ei");
}

void field_reset(gfx_context* ctx) {
    uint8_t y;
    uint8_t ty;

    trail_x0 = 0xff;
    trail_x1 = 0;
    trail_y0 = 0xff;
    trail_y1 = 0;
    memset(trail_map, 0, sizeof(trail_map));
    captured_cells = 0;
    captured_percent = 0;

    /* Everything is LAND: dead zone, frame and, initially, the whole field. */
    memset(field_map, 0x55, sizeof(field_map));

    /* The sea rectangle is 38 whole bytes wide and starts on a byte boundary. */
    for(y = SEA_ROW0; y <= SEA_ROW1; y++) {
        memset(&field_map[(uint16_t)y * FIELD_MAP_STRIDE + SEA_BYTE0],
               0, SEA_BYTE_COUNT);
    }

    field_tiles_reset();
    memset(tile_dirty, 0, sizeof(tile_dirty));

    for(ty = 0; ty < FIELD_BOARD_H; ty++) {
        uint8_t tx;

        for(tx = 0; tx < FIELD_BOARD_W; tx++) {
            uint16_t idx = (uint16_t)ty * FIELD_BOARD_W + tx;

            tile_grid[idx] = tile_is_border(tx, ty)
                           ? field_border_tile(tx, ty)
                           : TILE_SEA;
        }
    }

    field_render_tiles(ctx);
}

void field_draw_lives(gfx_context* ctx, uint8_t lives) {
    uint8_t i;

    for(i = 0; i < FIELD_LIVES_MAX; i++) {
        gfx_tilemap_place(ctx,
                          i < lives ? TILE_PLAYER : TILE_SEA,
                          0,
                          FIELD_LIVES_X + i,
                          FIELD_LIVES_Y);
    }
}

void field_hud_tile(gfx_context* ctx, uint8_t x, uint8_t y, uint8_t tile) {
    gfx_tilemap_place(ctx, tile, 0, x, y);
}

/* Draw a fixed-width decimal number, leading zeroes included. */
void field_hud_number(gfx_context* ctx, uint8_t x, uint8_t y,
                      uint16_t value, uint8_t digits) {
    while(digits != 0) {
        digits--;
        gfx_tilemap_place(ctx,
                          (uint8_t)(TILE_HUD_DIGIT_BASE + (uint8_t)(value % 10u)),
                          0,
                          (uint8_t)(x + digits),
                          y);
        value = (uint16_t)(value / 10u);
    }
}

/* Map a character to a generated HUD tile; spaces/unknown become blanks. */
static uint8_t hud_char_tile(char c) {
    if(c >= '0' && c <= '9') {
        return (uint8_t)(TILE_HUD_DIGIT_BASE + (uint8_t)(c - '0'));
    }
    if(c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if(c >= 'A' && c <= 'Z') {
        return (uint8_t)(TILE_HUD_LETTER_BASE + (uint8_t)(c - 'A'));
    }

    switch(c) {
        case ':': return TILE_HUD_COLON;
        case '%': return TILE_HUD_PERCENT;
        case '>': return TILE_HUD_GT;
        case '<': return TILE_HUD_LT;
        default:  return TILE_SEA;
    }
}

void field_hud_text(gfx_context* ctx, uint8_t x, uint8_t y, const char* text) {
    while(*text) {
        field_hud_tile(ctx, x, y, hud_char_tile(*text));
        x++;
        text++;
    }
}

void field_hud_clear_row(gfx_context* ctx, uint8_t y) {
    uint8_t x;

    for(x = 0; x < FIELD_COLS; x++) {
        field_hud_tile(ctx, x, y, TILE_SEA);
    }
}

void field_hud_blink(gfx_context* ctx, uint8_t visible) {
    hud_palette[5 * 2] = visible ? hud_yellow_lo : 0;
    hud_palette[5 * 2 + 1] = visible ? hud_yellow_hi : 0;
    gfx_palette_load(ctx, hud_palette, sizeof(hud_palette), 0);
}

/* Upload the generated HUD font into the reserved tileset slots. */
static void field_hud_load(gfx_context* ctx) {
    uint8_t i;

    for(i = 0; i < 42; i++) {
        /* Digits and status icons are white; message letters and high-score
         * names use the dedicated palette slot 5, which field_hud_blink()
         * toggles.  Letters are lowered by 2 pixels inside the HUD line. */
        const uint8_t is_letter = (uint8_t)(i >= 10 && i < 36);

        field_build_hud_tile(hud_glyphs[i], tile_scratch,
                             (uint8_t)(is_letter ? 5 : 7),
                             (uint8_t)(is_letter ? 1 : 0));
        field_tile_upload(ctx, (uint8_t)(TILE_HUD_DIGIT_BASE + i), tile_scratch);
    }
}

/* ------------------------------------------------------------------------- */
/* Capture                                                                   */
/* ------------------------------------------------------------------------- */

/*
 * Capturing must not depend on the size of the whole sea: on a 10 MHz Z80 a
 * full-field pass costs hundreds of milliseconds, and the original cell-by-cell
 * BFS cost several seconds.  A closed trail only splits one sea region in two,
 * so the capture probes the regions on both sides of the trail.  The small
 * ball-free side (the normal case) is filled, and the large side is left alone.
 *
 * Visited cells live in a separate map with the same two-bit layout as the
 * field map, so (field_map[off] | flood_vmap[off]) is zero exactly for a SEA
 * cell the current fill has not reached yet.  One open_mask4 lookup then tests
 * four cells at once, and discarding a probe only means clearing its bounding
 * box.
 *
 * If the two probes cannot decide (both sides are large, or the small side
 * contains a ball), the exact original rule is applied: flood from every ball
 * and turn the unreachable water into land.  That path is slower, but it is
 * only reached for huge or unusual captures.
 */

#define FLOOD_STACK_SIZE      1024
/*
 * The two seeds always sit on opposite sides of the trail.  The first one is
 * usually the large open sea (the search starts at the top-left of the trail
 * box), so it gets a small budget; the second one, normally the enclosed area,
 * gets a budget large enough for typical captures.
 */
#define FLOOD_PROBE_FIRST     256
#define FLOOD_PROBE_SECOND    2048

static uint8_t flood_vmap[FIELD_MAP_BYTES];
static uint8_t flood_qx0[FLOOD_STACK_SIZE];
static uint8_t flood_qx1[FLOOD_STACK_SIZE];
static uint8_t flood_qy[FLOOD_STACK_SIZE];
static uint16_t flood_count;
static uint8_t flood_overflow;
static uint8_t flood_aborted;
static uint16_t flood_filled;

/* Bounding box of the marks made by the current fill, in map bytes. */
static uint8_t flood_bx0, flood_bx1, flood_by0, flood_by1;

static const uint8_t bit_mask[4] = { 1, 2, 4, 8 };

/* open_mask4 of the four cells, with the visited cells removed. */
#define FLOOD_MASK(off) \
    open_mask4[(uint8_t)(field_map[(off)] | flood_vmap[(off)])]

static void flood_push(uint8_t x0, uint8_t x1, uint8_t y) {
    if(flood_count >= FLOOD_STACK_SIZE) {
        flood_overflow = 1;
        return;
    }

    flood_qx0[flood_count] = x0;
    flood_qx1[flood_count] = x1;
    flood_qy[flood_count] = y;
    flood_count++;
}

/* Clear the marks of the previous fill.  Bounded by its own bounding box. */
static void flood_clear(void) {
    uint8_t fy;

    if(flood_by0 <= flood_by1) {
        for(fy = flood_by0; fy <= flood_by1; fy++) {
            memset(&flood_vmap[(uint16_t)fy * FIELD_MAP_STRIDE + flood_bx0], 0,
                   (size_t)(uint8_t)(flood_bx1 - flood_bx0 + 1));
        }
    }

    flood_bx0 = 0xff;
    flood_bx1 = 0;
    flood_by0 = 0xff;
    flood_by1 = 0;
}

/* Mark [x0, x1] of a row as visited.  The whole run is SEA, so an OR is safe. */
static void flood_mark_run(uint16_t row, uint8_t x0, uint8_t x1, uint8_t y) {
    uint8_t b0 = (uint8_t)(x0 >> 2);
    uint8_t b1 = (uint8_t)(x1 >> 2);
    uint8_t fy = (uint8_t)(y - FIELD_MAP_Y0);

    if(b0 == b1) {
        flood_vmap[row + b0] |= (uint8_t)((0xffu << ((x0 & 3) << 1)) &
                                          (0xffu >> (6 - ((x1 & 3) << 1))));
    } else {
        uint8_t b;

        flood_vmap[row + b0] |= (uint8_t)(0xffu << ((x0 & 3) << 1));
        for(b = (uint8_t)(b0 + 1); b < b1; b++) {
            flood_vmap[row + b] = 0xff;
        }
        flood_vmap[row + b1] |= (uint8_t)(0xffu >> (6 - ((x1 & 3) << 1)));
    }

    flood_filled = (uint16_t)(flood_filled + (uint16_t)(x1 - x0 + 1));
    if(fy < flood_by0) flood_by0 = fy;
    if(fy > flood_by1) flood_by1 = fy;
    if(b0 < flood_bx0) flood_bx0 = b0;
    if(b1 > flood_bx1) flood_bx1 = b1;
}

/* Queue each open run of the row limited to [x0, x1]. */
static void flood_push_runs(uint16_t row, uint8_t x0, uint8_t x1, uint8_t y) {
    uint8_t b0 = (uint8_t)(x0 >> 2);
    uint8_t b1 = (uint8_t)(x1 >> 2);
    uint8_t run_start = 0xff;
    uint8_t run_end = 0;
    uint8_t b = b0;

    while(b <= b1) {
        const uint8_t raw = (uint8_t)(field_map[row + b] |
                                      flood_vmap[row + b]);
        uint8_t m;
        uint8_t p;

        if(raw == 0) {
            /* All four cells are open: skip consecutive open bytes. */
            const uint8_t start = (b == b0) ? x0 : (uint8_t)(b << 2);
            uint8_t end;

            while(b < b1 &&
                  (uint8_t)(field_map[row + b + 1] |
                            flood_vmap[row + b + 1]) == 0) {
                b++;
            }
            end = (b == b1) ? x1 : (uint8_t)((b << 2) + 3);

            if(run_start == 0xff) {
                run_start = start;
            }
            run_end = end;
            b++;
            continue;
        }

        m = open_mask4[raw];
        if(b == b0) {
            m &= (uint8_t)(0x0fu << (x0 & 3));
        }
        if(b == b1) {
            m &= (uint8_t)(0x0fu >> (3 - (x1 & 3)));
        }

        if(m == 0) {
            if(run_start != 0xff) {
                flood_push(run_start, run_end, y);
                run_start = 0xff;
            }
            b++;
            continue;
        }

        for(p = 0; p < 4; p++) {
            if(m & bit_mask[p]) {
                const uint8_t x = (uint8_t)((b << 2) + p);

                if(run_start == 0xff) {
                    run_start = x;
                }
                run_end = x;
            } else if(run_start != 0xff) {
                flood_push(run_start, run_end, y);
                run_start = 0xff;
            }
        }
        b++;
    }

    if(run_start != 0xff) {
        flood_push(run_start, run_end, y);
    }
}

/*
 * Process queued runs.  A positive budget stops the fill once that many cells
 * have been marked, which is used to recognise the large side cheaply.
 */
static void flood_run(uint16_t budget) {
    while(flood_count != 0) {
        uint8_t x0, x1, y;
        uint16_t row;

        flood_count--;
        x0 = flood_qx0[flood_count];
        x1 = flood_qx1[flood_count];
        y = flood_qy[flood_count];
        row = (uint16_t)(y - FIELD_MAP_Y0) * FIELD_MAP_STRIDE;

        /* The segment may have been partly marked by another parent. */
        while(x0 <= x1 &&
              !(FLOOD_MASK((uint16_t)(row + (x0 >> 2))) & bit_mask[x0 & 3])) {
            x0++;
        }
        if(x0 > x1) {
            continue;
        }

        /* Expand the segment over the complete open run on this row. */
        while(x0 > FIELD_SEA_X0 &&
              (FLOOD_MASK((uint16_t)(row + ((x0 - 1) >> 2))) &
               bit_mask[(x0 - 1) & 3])) {
            x0--;
        }
        while(x1 < FIELD_SEA_X1 &&
              (FLOOD_MASK((uint16_t)(row + ((x1 + 1) >> 2))) &
               bit_mask[(x1 + 1) & 3])) {
            x1++;
        }

        flood_mark_run(row, x0, x1, y);

        if(y > FIELD_SEA_Y0) {
            flood_push_runs((uint16_t)(row - FIELD_MAP_STRIDE), x0, x1,
                            (uint8_t)(y - 1));
        }
        if(y < FIELD_SEA_Y1) {
            flood_push_runs((uint16_t)(row + FIELD_MAP_STRIDE), x0, x1,
                            (uint8_t)(y + 1));
        }

        if(flood_count == 0) {
            break;
        }
        if(budget != 0 && flood_filled >= budget) {
            flood_aborted = 1;
            return;
        }
    }
}

/* Start a fill from one seed.  Returns 1 when the region was fully marked. */
static uint8_t flood_region(uint8_t x, uint8_t y, uint16_t budget) {
    flood_clear();
    flood_count = 0;
    flood_overflow = 0;
    flood_aborted = 0;
    flood_filled = 0;

    if(x < FIELD_MAP_W && y >= FIELD_MAP_Y0 && y < FIELD_MAP_Y0 + FIELD_MAP_H &&
       map_get(x, y) == CELL_SEA) {
        flood_push(x, x, y);
        flood_run(budget);
    }

    return (uint8_t)(!flood_aborted && !flood_overflow);
}

static uint8_t flood_marked_cell(uint8_t x, uint8_t y) {
    uint16_t off;

    if(x >= FIELD_MAP_W || y < FIELD_MAP_Y0 || y >= FIELD_MAP_Y0 + FIELD_MAP_H) {
        return 0;
    }

    off = (uint16_t)(y - FIELD_MAP_Y0) * FIELD_MAP_STRIDE + (x >> 2);
    return (uint8_t)((flood_vmap[off] >> ((x & 3) << 1)) & 3);
}

static uint8_t flood_has_ball(const uint16_t* ball_x, const uint16_t* ball_y,
                              uint8_t count) {
    uint8_t i;

    for(i = 0; i < count; i++) {
        if(flood_marked_cell((uint8_t)((ball_x[i] + 8) >> 2),
                             (uint8_t)((ball_y[i] + 8) >> 2))) {
            return 1;
        }
    }

    return 0;
}

/*
 * Correctness fallback for a work-stack overflow: propagate the visited cells
 * with repeated sweeps.  Only reached for pathological mazes.
 */
static uint8_t flood_vget(uint8_t x, uint8_t y) {
    uint16_t off;

    if(x >= FIELD_MAP_W || y < FIELD_MAP_Y0 || y >= FIELD_MAP_Y0 + FIELD_MAP_H) {
        return 0;
    }

    off = (uint16_t)(y - FIELD_MAP_Y0) * FIELD_MAP_STRIDE + (x >> 2);
    return (uint8_t)((flood_vmap[off] >> ((x & 3) << 1)) & 3);
}

static void flood_propagate(void) {
    uint8_t changed;

    flood_by0 = SEA_ROW0;
    flood_by1 = SEA_ROW1;
    flood_bx0 = SEA_BYTE0;
    flood_bx1 = (uint8_t)(SEA_BYTE0 + SEA_BYTE_COUNT - 1);

    do {
        uint8_t fy;

        changed = 0;
        for(fy = SEA_ROW0; fy <= SEA_ROW1; fy++) {
            uint16_t base = (uint16_t)fy * FIELD_MAP_STRIDE;
            uint8_t y = (uint8_t)(fy + FIELD_MAP_Y0);
            uint8_t bx;

            for(bx = SEA_BYTE0; bx < SEA_BYTE0 + SEA_BYTE_COUNT; bx++) {
                uint8_t both = (uint8_t)(field_map[base + bx] |
                                         flood_vmap[base + bx]);
                uint8_t open = (uint8_t)((uint8_t)~both &
                                         (uint8_t)~(both >> 1) & 0x55);
                uint8_t add = 0;
                uint8_t p;

                for(p = 0; p < 4; p++) {
                    if(open & (uint8_t)(1u << (p << 1))) {
                        uint8_t x = (uint8_t)((bx << 2) + p);

                        if(flood_vget((uint8_t)(x - 1), y) ||
                           flood_vget((uint8_t)(x + 1), y) ||
                           flood_vget(x, (uint8_t)(y - 1)) ||
                           flood_vget(x, (uint8_t)(y + 1))) {
                            add |= (uint8_t)(3u << (p << 1));
                            changed = 1;
                        }
                    }
                }

                if(add) {
                    flood_vmap[base + bx] |= add;
                }
            }
        }
    } while(changed);
}

/*
 * Convert every TRAIL cell (10) to LAND (01).  Only field-map bytes listed in
 * trail_map are examined, so a large loop does not scan its whole interior.
 * The bit trick finds all pairs equal to 10 at once.
 */
static void field_convert_trail(void) {
    uint8_t fy;

    for(fy = 0; fy < FIELD_MAP_H; fy++) {
        uint16_t base = (uint16_t)fy * FIELD_MAP_STRIDE;
        uint8_t bi;

        for(bi = 0; bi < 5; bi++) {
            uint8_t bits = trail_map[fy][bi];

            while(bits) {
                const uint8_t low = (uint8_t)(bits & 0x0f);
                const uint8_t bit = low ? trail_bit[low]
                                        : (uint8_t)(4 + trail_bit[bits >> 4]);
                const uint8_t bx = (uint8_t)((bi << 3) + bit);
                const uint8_t b = field_map[base + bx];
                const uint8_t h = (uint8_t)(b & 0xaa);
                const uint8_t l = (uint8_t)(b & 0x55);
                const uint8_t m = (uint8_t)((h >> 1) & (uint8_t)~l);

                bits &= (uint8_t)(bits - 1);
                if(m) {
                    field_map[base + bx] =
                        (uint8_t)((b & (uint8_t)~(m | (m << 1))) | m);
                    captured_cells = (uint16_t)(captured_cells + pair_count[m]);
                    tile_dirty_set(bx, (uint8_t)(fy >> 2));
                }
            }
        }
    }

    memset(trail_map, 0, sizeof(trail_map));
    trail_x0 = 0xff;
    trail_x1 = 0;
    trail_y0 = 0xff;
    trail_y1 = 0;
}

/*
 * Find a trail cell with sea on both sides.  The two seeds usually belong to
 * the two different regions created by the trail; the capture also handles the
 * case where they turn out to be connected.  Must run before the trail is
 * converted to land.
 */
static uint8_t field_find_seeds(uint8_t* ax, uint8_t* ay,
                                uint8_t* bx, uint8_t* by) {
    uint8_t fy;

    if(trail_y0 > trail_y1) {
        return 0;
    }

    for(fy = 0; fy < FIELD_MAP_H; fy++) {
        uint16_t base = (uint16_t)fy * FIELD_MAP_STRIDE;
        uint8_t bi;

        for(bi = 0; bi < 5; bi++) {
            uint8_t bits = trail_map[fy][bi];

            while(bits) {
                const uint8_t low = (uint8_t)(bits & 0x0f);
                const uint8_t bit = low ? trail_bit[low]
                                        : (uint8_t)(4 + trail_bit[bits >> 4]);
                const uint8_t map_byte = (uint8_t)((bi << 3) + bit);
                const uint8_t b = field_map[base + map_byte];
                const uint8_t t = (uint8_t)((b & 0xaa) &
                                            (uint8_t)~((b << 1) & 0xaa));
                uint8_t tb;

                bits &= (uint8_t)(bits - 1);
                for(tb = 1; tb < 8; tb += 2) {
                    uint8_t cx, cy, l, r;

                    if(!(t & (uint8_t)(1u << tb))) {
                        continue;
                    }

                    cx = (uint8_t)((tb >> 1) + (map_byte << 2));
                    cy = (uint8_t)(fy + FIELD_MAP_Y0);

                    l = map_get((uint8_t)(cx - 1), cy);
                    r = map_get((uint8_t)(cx + 1), cy);
                    if(l == CELL_SEA && r == CELL_SEA) {
                        *ax = (uint8_t)(cx - 1);
                        *ay = cy;
                        *bx = (uint8_t)(cx + 1);
                        *by = cy;
                        return 1;
                    }

                    l = map_get(cx, (uint8_t)(cy - 1));
                    r = map_get(cx, (uint8_t)(cy + 1));
                    if(l == CELL_SEA && r == CELL_SEA) {
                        *ax = cx;
                        *ay = (uint8_t)(cy - 1);
                        *bx = cx;
                        *by = (uint8_t)(cy + 1);
                        return 1;
                    }
                }
            }
        }
    }

    return 0;
}

/* Turn the currently marked region into LAND and mark its tiles dirty. */
static void field_fill_marked(void) {
    uint8_t fy;

    if(flood_by0 > flood_by1) {
        return;
    }

    for(fy = flood_by0; fy <= flood_by1; fy++) {
        uint16_t base = (uint16_t)fy * FIELD_MAP_STRIDE;
        uint8_t bx;
        uint8_t last_tx = 0xff;

        for(bx = flood_bx0; bx <= flood_bx1; bx++) {
            uint8_t v = flood_vmap[base + bx];

            if(v == 0xff) {
                field_map[base + bx] = 0x55;
                captured_cells = (uint16_t)(captured_cells + 4);
            } else if(v) {
                uint8_t b = field_map[base + bx];
                uint8_t low = (uint8_t)(v & 0x55);

                field_map[base + bx] = (uint8_t)((b & (uint8_t)~(low << 1)) | low);
                captured_cells = (uint16_t)(captured_cells + pair_count[low]);
            } else {
                continue;
            }
            if(bx != last_tx) {
                tile_dirty_set(bx, (uint8_t)(fy >> 2));
                last_tx = bx;
            }
        }
    }

    flood_clear();
}

/*
 * Exact rule from the original algorithm: flood from every ball and turn the
 * water that stayed unreachable into land.  This is only used for degenerate
 * trails and work-stack overflows, so it may walk the whole sea.
 */
static void field_capture_drain(const uint16_t* ball_x, const uint16_t* ball_y,
                                uint8_t count) {
    uint8_t i;
    uint8_t fy;

    flood_clear();
    flood_count = 0;
    flood_overflow = 0;
    flood_aborted = 0;
    flood_filled = 0;

    for(i = 0; i < count; i++) {
        uint8_t cx = (uint8_t)((ball_x[i] + 8) >> 2);
        uint8_t cy = (uint8_t)((ball_y[i] + 8) >> 2);

        if(cx < FIELD_MAP_W && cy >= FIELD_MAP_Y0 &&
           cy < FIELD_MAP_Y0 + FIELD_MAP_H && map_get(cx, cy) == CELL_SEA) {
            flood_push(cx, cx, cy);
        }
    }

    flood_run(0);
    if(flood_overflow) {
        flood_propagate();
    }

    for(fy = SEA_ROW0; fy <= SEA_ROW1; fy++) {
        uint16_t base = (uint16_t)fy * FIELD_MAP_STRIDE;
        uint8_t bx;

        for(bx = SEA_BYTE0; bx < SEA_BYTE0 + SEA_BYTE_COUNT; bx++) {
            uint8_t both = (uint8_t)(field_map[base + bx] |
                                     flood_vmap[base + bx]);
            uint8_t land = (uint8_t)((uint8_t)~both &
                                     (uint8_t)~(both >> 1) & 0x55);

            if(land) {
                field_map[base + bx] = (uint8_t)(field_map[base + bx] | land);
                captured_cells = (uint16_t)(captured_cells + pair_count[land]);
                tile_dirty_set(bx, (uint8_t)(fy >> 2));
            }
        }
    }

    flood_clear();
}

static void field_capture_impl(gfx_context* ctx, const uint16_t* ball_x,
                               const uint16_t* ball_y, uint8_t count) {
    uint8_t ax, ay, bx, by;
    uint8_t a_done, b_done;
    uint8_t i;
    uint8_t sea_balls = 0;

    (void)ctx;

    for(i = 0; i < count; i++) {
        if(map_get((uint8_t)((ball_x[i] + 8) >> 2),
                   (uint8_t)((ball_y[i] + 8) >> 2)) == CELL_SEA) {
            sea_balls++;
        }
    }

    /* Without any ball in the water every sea cell is unreachable, exactly as
     * in the original rule.  This also covers a ball sitting on fresh trail. */
    if(sea_balls == 0 || !field_find_seeds(&ax, &ay, &bx, &by)) {
        field_convert_trail();
        field_capture_drain(ball_x, ball_y, count);
        return;
    }

    field_convert_trail();

    /* Probe the first side.  A completed, ball-free side can be filled without
     * looking at the other one: the component held a ball, so it must be
     * there. */
    a_done = flood_region(ax, ay, FLOOD_PROBE_FIRST);
    if(a_done) {
        if(!flood_has_ball(ball_x, ball_y, count)) {
            field_fill_marked();
            return;
        }
        if(flood_marked_cell(bx, by)) {
            /* Both seeds are in the same region, which holds a ball. */
            flood_clear();
            return;
        }
    }

    /* Probe the second side, normally the enclosed area.  If the budget was
     * hit, continue the same fill instead of throwing its work away. */
    b_done = flood_region(bx, by, FLOOD_PROBE_SECOND);
    if(!b_done) {
        if(flood_overflow) {
            flood_clear();
            field_capture_drain(ball_x, ball_y, count);
            return;
        }
        flood_aborted = 0;
        flood_run(0);
        if(flood_overflow) {
            flood_clear();
            field_capture_drain(ball_x, ball_y, count);
            return;
        }
    }

    if(!flood_has_ball(ball_x, ball_y, count)) {
        field_fill_marked();
        return;
    }

    if(a_done) {
        /* Second side holds a ball as well and the first side was already
         * examined: there is no ball-free side to fill. */
        flood_clear();
        return;
    }

    if(flood_marked_cell(ax, ay)) {
        /* The two seeds belong to the same region; it holds a ball. */
        flood_clear();
        return;
    }

    /* The first probe was truncated, so examine that side exactly now. */
    if(!flood_region(ax, ay, 0)) {
        flood_clear();
        field_capture_drain(ball_x, ball_y, count);
        return;
    }
    if(!flood_has_ball(ball_x, ball_y, count)) {
        field_fill_marked();
        return;
    }

    flood_clear();
}

void field_capture(gfx_context* ctx, const uint16_t* ball_x,
                   const uint16_t* ball_y, uint8_t count) {
    field_capture_impl(ctx, ball_x, ball_y, count);

    captured_percent = (uint8_t)(((uint32_t)captured_cells * 100u) /
                                 (uint32_t)FIELD_SEA_CELLS);
}

uint16_t field_captured_cells(void) {
    return captured_cells;
}

uint8_t field_captured_percent(void) {
    return captured_percent;
}

/* ------------------------------------------------------------------------- */
/* Asset loading                                                             */
/* ------------------------------------------------------------------------- */

static gfx_error field_load_lz(gfx_context* ctx, const uint8_t* data, uint16_t size) {
    uint8_t buffer[FIELD_TILE_BYTES];
    uint16_t i = 0;
    uint16_t j = 0;
    uint16_t tile = 0;

    while(i < size) {
        const uint8_t code = data[i++];
        const uint8_t type = code & 0xc0;

        if(type == 0x00) {
            buffer[j++] = code & 0x3f;
        } else if(type == 0x40) {
            uint8_t count = (uint8_t)((code & 0x3f) + 1);
            while(count-- != 0 && i < size) {
                buffer[j++] = data[i++];
            }
        } else if(type == 0x80) {
            const uint8_t length = (uint8_t)(((code & 0x30) >> 4) + 3);
            const uint8_t offset = (uint8_t)((code & 0x0f) + 1);
            uint8_t k;
            for(k = 0; k < length; k++) {
                buffer[j] = buffer[j - offset];
                j++;
            }
        } else {
            const uint8_t length = (uint8_t)((code & 0x3f) + 4);
            const uint8_t offset = (uint8_t)(data[i++] + 1);
            uint8_t k;
            for(k = 0; k < length; k++) {
                buffer[j] = buffer[j - offset];
                j++;
            }
        }

        if(j >= FIELD_TILE_BYTES) {
            gfx_tileset_load_none(ctx, buffer, FIELD_TILE_BYTES,
                                  (uint16_t)(tile * FIELD_TILE_BYTES), 0, 0);
            tile++;
            j = 0;
        }
    }

    return GFX_SUCCESS;
}

gfx_error field_load(gfx_context* ctx) {
    const size_t palette_size = &_field_ztp_end - &_field_ztp_start;
    const uint16_t tileset_size = (uint16_t)(&_field_zts_end - &_field_zts_start);
    gfx_error err;

    field_dma_init();
    field_lut_init();

    err = gfx_palette_load(ctx, &_field_ztp_start, palette_size, 0);
    if(err != GFX_SUCCESS) return err;

    /* Keep a RAM copy so the message colour can blink.  The asset palette is
     * 16 colours (32 bytes); slot 4 is the yellow used by the player. */
    memset(hud_palette, 0, sizeof(hud_palette));
    memcpy(hud_palette, &_field_ztp_start,
           palette_size < sizeof(hud_palette) ? palette_size : sizeof(hud_palette));
    hud_yellow_lo = hud_palette[4 * 2];
    hud_yellow_hi = hud_palette[4 * 2 + 1];
    field_hud_blink(ctx, 1);

    err = field_load_lz(ctx, &_field_zts_start, tileset_size);
    if(err != GFX_SUCCESS) return err;

    field_tile_upload(ctx, TILE_SPIDER, spider_tile);
    field_hud_load(ctx);
    return GFX_SUCCESS;
}
