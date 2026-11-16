#include <stdio.h>
#include <stdint.h>
#include <zos_errors.h>
#include <zos_vfs.h>
#include <zos_sys.h>
#include <zos_video.h>
#include <zgdk.h>

#include "main.h"
#include "field.h"
#include "game.h"
#include "levels.h"
#include "splash.h"
#include "sound.h"

gfx_context vctx;

static void start_game(uint8_t start_level, uint8_t want_splash);

/*
 * Zeal 8-bit OS passes the whole parameter string as argv[0] (argc is 1 for an
 * external program).  Scan every argv entry so it also works when a caller
 * provides a real argv array.
 * Returns 0 when --level was not supplied.
 */
static uint8_t parse_level(int argc, char** argv) {
    uint8_t level = 0;
    int i;

    for(i = 0; i < argc; i++) {
        uint8_t value = level_parse_arg(argv[i]);
        if(value != 0) {
            level = value;
        }
    }

    return level;
}

int main(int argc, char** argv) {
    const uint8_t requested_level = parse_level(argc, argv);
    const uint8_t start_level = requested_level != 0 ? requested_level : 1;
    /* --level is a testing shortcut: skip the intro splash for it. */
    const uint8_t want_splash = (uint8_t)(requested_level == 0);

    init(start_level, want_splash);

    for(;;) {
        uint8_t restart;

        for(;;) {
            uint16_t keys;

            gfx_wait_vblank(&vctx);

            keys = input();
            if(keys & BUTTON_SELECT) {
                restart = 0;
                break;
            }

            game_update(&vctx, keys);
            xsfx_update();
            if(game_restart_requested()) {
                restart = 1;
                break;
            }

            gfx_wait_end_vblank(&vctx);
        }

        game_shutdown(&vctx);
        if(!restart) {
            break;
        }

        /* Out of lives: return to the beginning of the program. */
        start_game(start_level, want_splash);
    }

    xsfx_stop_all();
    deinit();
    return 0;
}

/*
 * gfx_initialize() only clears layer0, and not even its last row. In 4bpp mode
 * layer1 holds the tile's 9th bit and the palette bank, so the attributes left
 * over from text mode make the board render garbage tiles. Clear both layers:
 * tile 0 with palette 0 is a solid black tile.
 */
static void clear_screen(void) {
    static uint8_t blank_line[80];
    for (uint8_t y = 0; y < 40; y++) {
        gfx_tilemap_load(&vctx, blank_line, sizeof(blank_line), 0, 0, y);
        gfx_tilemap_load(&vctx, blank_line, sizeof(blank_line), 1, 0, y);
    }
}

static void handle_error(zos_err_t err, const char* message, uint8_t fatal) {
    if(err != ERR_SUCCESS) {
        if(fatal) deinit();
       
        printf("\nError[%d] (%02x) %s", err, err, message);
        if(fatal) exit(err);
    }
}

/* Keep the text-console warning readable before switching to graphics mode. */
static void warning_delay(void) {
    uint8_t i;

    for(i = 0; i < 120; i++) {
        gfx_wait_vblank(&vctx);
        gfx_wait_end_vblank(&vctx);
    }
}

void init(uint8_t start_level, uint8_t want_splash) {
    zos_err_t err;

    /*
     * Initialize the keyboard and, when one is connected, the SNES pad on
     * port 1.  A missing controller is not fatal: input_init() still leaves
     * the keyboard usable and returns ERR_BAD_MODE.
     */
    err = input_init(1);
    if(err != ERR_SUCCESS && err != ERR_BAD_MODE) {
        handle_error(err, "failed to init input", 1);
    }

    /*
     * Check for the splash files while the video board is still in text mode,
     * so the warning is visible.  Missing files are not fatal: the game starts
     * normally without the splash.
     */
    if(want_splash) {
        if(!splash_check()) {
            printf("Warning: splash assets not found in assets/.\n");
            printf("Expected: assets/splash.zts, assets/splash.ztp, assets/splash.ztm\n");
            printf("Starting the game without the splash screen...\n");
            warning_delay();
            want_splash = 0;
        }
    }

    // disable the screen to prevent artifacts from showing
    gfx_enable_screen(0);

    err = gfx_initialize(ZVB_CTRL_VID_MODE_GFX_640_4BIT, &vctx);
    handle_error(err, "failed to init graphics", 1);

    xsfx_init();
    start_game(start_level, want_splash);
}

/*
 * Start a fresh game session.  Also used after the high-score table so that
 * running out of lives really returns to the beginning of the program,
 * including the optional splash screen.
 */
static void start_game(uint8_t start_level, uint8_t want_splash) {
    zos_err_t err;

    xsfx_stop_all();
    gfx_enable_screen(0);

    if(want_splash) {
        /* A read error after the existence check simply skips the splash. */
        splash_show(&vctx);
        gfx_enable_screen(0);
    }

    err = field_load(&vctx);
    handle_error(err, "failed to load field", 1);

    clear_screen();
    game_init(&vctx, start_level);

    gfx_enable_screen(1);
}

void deinit(void) {
    // reset screen
    ioctl(DEV_STDOUT, CMD_RESET_SCREEN, NULL);
}

uint16_t input(void) {
    return input_read();
}
