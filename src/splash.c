#include <stdio.h>
#include <stdint.h>

#include <zos_errors.h>
#include <zos_vfs.h>
#include <zgdk.h>

#include "splash.h"

#define SPLASH_ZTP      "assets/splash.ztp"
#define SPLASH_ZTS      "assets/splash.zts"
#define SPLASH_ZTM      "assets/splash.ztm"

#define SPLASH_CHUNK    512
#define SPLASH_PAL_LEN  16
#define SPLASH_HOLD_SEC 25
#define SPLASH_FRAMES   (SPLASH_HOLD_SEC * 60)

#define SPLASH_ROWS     30
#define SPLASH_COLS     40

static uint8_t  splash_buf[SPLASH_CHUNK];
static uint16_t splash_pal_target[SPLASH_PAL_LEN];
static uint16_t splash_pal_current[SPLASH_PAL_LEN];

static uint8_t splash_file_exists(const char* path) {
    zos_dev_t dev = open(path, O_RDONLY);

    if(dev < 0) {
        printf("Warning: splash asset missing: %s\n", path);
        return 0;
    }

    close(dev);
    return 1;
}

uint8_t splash_check(void) {
    uint8_t ok = 1;

    if(!splash_file_exists(SPLASH_ZTP)) ok = 0;
    if(!splash_file_exists(SPLASH_ZTS)) ok = 0;
    if(!splash_file_exists(SPLASH_ZTM)) ok = 0;

    return ok;
}

static void splash_apply_brightness(gfx_context* ctx, uint8_t level) {
    uint8_t i;

    for(i = 0; i < SPLASH_PAL_LEN; i++) {
        const uint16_t c = splash_pal_target[i];
        const uint16_t r = (uint16_t)((((c >> 11) & 0x1F) * level) >> 5);
        const uint16_t g = (uint16_t)((((c >> 5) & 0x3F) * level) >> 5);
        const uint16_t b = (uint16_t)(((c & 0x1F) * level) >> 5);

        splash_pal_current[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }

    gfx_palette_load(ctx, splash_pal_current, sizeof(splash_pal_current), 0);
}

static void splash_wait_frame(gfx_context* ctx) {
    gfx_wait_vblank(ctx);
    gfx_wait_end_vblank(ctx);
}

static uint8_t splash_load(gfx_context* ctx) {
    zos_dev_t dev;
    uint16_t size;
    uint16_t off;
    uint8_t y;

    /* Palette: 16 RGB565 entries. */
    dev = open(SPLASH_ZTP, O_RDONLY);
    if(dev < 0) return 0;
    size = (uint16_t)sizeof(splash_pal_target);
    if(read(dev, splash_pal_target, &size) != ERR_SUCCESS ||
       size < sizeof(splash_pal_target)) {
        close(dev);
        return 0;
    }
    close(dev);
    splash_apply_brightness(ctx, 0);

    /* Tileset: load straight into the video board, 512 bytes at a time. */
    dev = open(SPLASH_ZTS, O_RDONLY);
    if(dev < 0) return 0;
    off = 0;
    for(;;) {
        size = SPLASH_CHUNK;
        if(read(dev, splash_buf, &size) != ERR_SUCCESS || size == 0) {
            break;
        }
        if(gfx_tileset_load_none(ctx, splash_buf, size, off, 0, 0) != GFX_SUCCESS) {
            close(dev);
            return 0;
        }
        off = (uint16_t)(off + size);
    }
    close(dev);

    /* Tilemap: 30 tile rows for layer0, then 30 attribute rows for layer1. */
    dev = open(SPLASH_ZTM, O_RDONLY);
    if(dev < 0) return 0;
    for(y = 0; y < SPLASH_ROWS; y++) {
        size = SPLASH_COLS;
        if(read(dev, splash_buf, &size) != ERR_SUCCESS || size < SPLASH_COLS) {
            close(dev);
            return 0;
        }
        gfx_tilemap_load(ctx, splash_buf, SPLASH_COLS, 0, 0, y);
    }
    for(y = 0; y < SPLASH_ROWS; y++) {
        size = SPLASH_COLS;
        if(read(dev, splash_buf, &size) != ERR_SUCCESS || size < SPLASH_COLS) {
            close(dev);
            return 0;
        }
        gfx_tilemap_load(ctx, splash_buf, SPLASH_COLS, 1, 0, y);
    }
    close(dev);

    return 1;
}

uint8_t splash_show(gfx_context* ctx) {
    uint16_t frames = 0;
    uint8_t level;

    if(!splash_load(ctx)) {
        return 0;
    }

    /* Fade in from black. */
    gfx_enable_screen(1);
    for(level = 0; level <= 32; level++) {
        splash_wait_frame(ctx);
        splash_apply_brightness(ctx, level);
        frames++;
    }

    /* Wait for any key, or start the game after 25 seconds. */
    while(frames < SPLASH_FRAMES) {
        if(input_read() != 0) {
            break;
        }
        splash_wait_frame(ctx);
        frames++;
    }

    /* Fade out to black. */
    for(level = 32; level != 0xff; level--) {
        splash_wait_frame(ctx);
        splash_apply_brightness(ctx, level);
    }

    return 1;
}
