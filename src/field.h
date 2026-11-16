#ifndef FIELD_H
#define FIELD_H

#include <stdint.h>
#include <zvb_gfx.h>

/*
 * The screen is 40x30 tiles of 16x16 pixels (640x480, mode 6).
 * The top two tile rows are reserved for the HUD.  The playfield is the
 * remaining 40x28 tile rows.
 *
 * The playfield is described by a packed logical map with 4x4 pixel cells.
 * Four cells fit exactly in one 16x16 hardware tile, so a tile can be
 * generated from four bytes of the map without any seams or partial cells.
 *
 * Absolute logical-cell coordinates use the whole 160x120 screen:
 *   y = 8..119 is the playfield, y = 0..7 is the HUD.
 * The field map itself starts at y = 8, i.e. map row 0 == screen cell row 8.
 */
#define FIELD_TILE_SIZE 16
#define FIELD_COLS      40
#define FIELD_ROWS      30
#define FIELD_TOP       2
#define FIELD_BOTTOM    (FIELD_ROWS - 1)

#define FIELD_BOARD_W   FIELD_COLS
#define FIELD_BOARD_H   (FIELD_ROWS - FIELD_TOP)
#define FIELD_BOARD_CELLS (FIELD_BOARD_W * FIELD_BOARD_H)

/* Logical 4x4-pixel grid. */
#define FIELD_CELL_SIZE 4
#define FIELD_MAP_W     160
#define FIELD_MAP_H     112
#define FIELD_MAP_STRIDE 40
#define FIELD_MAP_BYTES (FIELD_MAP_H * FIELD_MAP_STRIDE)
#define FIELD_MAP_Y0    8

/* Playable frame limits, in absolute screen cells. */
#define FIELD_PLAY_X0   2
#define FIELD_PLAY_X1   157
#define FIELD_PLAY_Y0   10
#define FIELD_PLAY_Y1   117

/* The sea is the inner rectangle; the 8-pixel dead zone and the 8-pixel
 * frame are both LAND in the map. */
#define FIELD_SEA_X0    4
#define FIELD_SEA_X1    155
#define FIELD_SEA_Y0    12
#define FIELD_SEA_Y1    115

/* Packed cell states (two bits per cell). */
#define CELL_SEA        0
#define CELL_LAND       1
#define CELL_TRAIL      2
#define CELL_VISITED    3

/*
 * Static tiles in assets/field.gif.  Slots 0..11 are loaded from the asset,
 * slots 213..254 hold the generated HUD/text font and icons, slot 255 holds
 * the generated spider, and slots 12..212 are generated at runtime for mixed
 * sea/land tiles.
 */
#define TILE_SEA        0
#define TILE_WALL_TOP   1
#define TILE_WALL_BOTTOM 2
#define TILE_WALL_LEFT  3
#define TILE_WALL_RIGHT 4
#define TILE_CORNER_TL  5
#define TILE_CORNER_TR  6
#define TILE_CORNER_BL  7
#define TILE_CORNER_BR  8
#define TILE_LAND       9
#define TILE_PLAYER     10
#define TILE_BALL       11
#define TILE_SPIDER     255

/* Generated font: digits 0-9, A-Z, ':', '%', flag, clock, '>' and '<'. */
#define TILE_HUD_DIGIT_BASE  213   /* 0-9 */
#define TILE_HUD_LETTER_BASE 223   /* A-Z */
#define TILE_HUD_COLON       249
#define TILE_HUD_PERCENT     250
#define TILE_HUD_FLAG        251
#define TILE_HUD_CLOCK       252
#define TILE_HUD_GT          253
#define TILE_HUD_LT          254

#define TILE_DYNAMIC_BASE   12
#define TILE_DYNAMIC_LAST   212
#define TILE_DYNAMIC_COUNT  (TILE_DYNAMIC_LAST - TILE_DYNAMIC_BASE + 1)

/* Lives indicator in the reserved HUD area (top tile row). */
#define FIELD_LIVES_MAX     5
#define FIELD_LIVES_X       0
#define FIELD_LIVES_Y       0

gfx_error field_load(gfx_context* ctx);
void field_draw_lives(gfx_context* ctx, uint8_t lives);

/* Reset the map to the initial sea/frame and redraw the whole board. */
void field_reset(gfx_context* ctx);

/* Render the tiles marked dirty by field_set_cell()/capture. */
void field_render_dirty(gfx_context* ctx);

/* Captured sea area since the last field_reset(). */
uint16_t field_captured_cells(void);
uint8_t field_captured_percent(void);

/* HUD helpers: tile coordinates on layer 0. */
void field_hud_tile(gfx_context* ctx, uint8_t x, uint8_t y, uint8_t tile);
void field_hud_number(gfx_context* ctx, uint8_t x, uint8_t y,
                      uint16_t value, uint8_t digits);

/* Text using the generated HUD font (digits, A-Z, ':', '%', '>' and '<').
 * Unknown characters are drawn as blanks. */
void field_hud_text(gfx_context* ctx, uint8_t x, uint8_t y, const char* text);
void field_hud_clear_row(gfx_context* ctx, uint8_t y);

/* Blink the message letters by switching their palette entry between the
 * yellow of palette slot 4 and black.  Other graphics are not affected. */
void field_hud_blink(gfx_context* ctx, uint8_t visible);

/* Cell access in absolute screen-cell coordinates (x 0..159, y 0..119).
 * Cells outside the playfield are treated as LAND/solid. */
uint8_t field_get_cell(uint8_t x, uint8_t y);
void field_set_cell(uint8_t x, uint8_t y, uint8_t state);
uint8_t field_cell_at_px(uint16_t px, uint16_t py);

/*
 * Capture: convert the current trail to LAND, then fill the sea that the trail
 * cut off from every ball.  The two regions on either side of the trail are
 * probed first; the usual small, ball-free side is filled without walking the
 * whole sea.  If that cannot decide, the exact original rule is applied: flood
 * from every ball centre and turn the unreachable water into LAND.
 * ball_x/ball_y are sprite top-left pixel coordinates; the ball centre is used
 * as the flood-fill seed.
 */
void field_capture(gfx_context* ctx, const uint16_t* ball_x,
                   const uint16_t* ball_y, uint8_t count);

/* Dynamic tileset manager (used by the renderer and by the capture pass). */
void field_tiles_reset(void);
uint8_t field_tile_get(gfx_context* ctx, uint32_t key, const uint8_t* tile);
void field_tile_upload(gfx_context* ctx, uint8_t index, const uint8_t* tile);

/* Copy the complete 40x28 tile map to layer 0. */
void field_render_tiles(gfx_context* ctx);

#endif
