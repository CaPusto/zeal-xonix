#include <stdint.h>
#include <string.h>

#include <zos_errors.h>
#include <zos_vfs.h>
#include <zos_keyboard.h>
#include <zvb_gfx.h>
#include <zgdk/input.h>

#include "highscore.h"
#include "sound.h"
#include "field.h"

/*
 * HISCORE.DAT layout (107 bytes):
 *
 *   0..3    magic "XHS1"
 *   4..104  payload, XOR-obfuscated:
 *             [0]      number of valid records, 0..10
 *             [1..]    10 slots of 8 name bytes + uint16_t score (little endian)
 *   105..106 16-bit checksum of the clear payload, little endian
 *
 * Missing, truncated or corrupted files are treated as an empty table.
 */
#define HS_PAYLOAD_SIZE (1 + HIGHSCORE_MAX * (HIGHSCORE_NAME_LEN + 2))
#define HS_FILE_SIZE    (4 + HS_PAYLOAD_SIZE + 2)

#define HS_MAGIC0       'X'
#define HS_MAGIC1       'H'
#define HS_MAGIC2       'S'
#define HS_MAGIC3       '1'

#define HS_TABLE_X      10
#define HS_NAME_X       ((FIELD_COLS - HIGHSCORE_NAME_LEN) / 2)
#define HS_TABLE_Y      6

#define HS_WAIT_FRAMES  (25u * 60u)

static uint8_t hs_file[HS_FILE_SIZE];
static uint8_t hs_key;
static uint8_t hs_release_pending;

static void hs_xor_payload(void) {
    uint8_t i;

    for(i = 0; i < HS_PAYLOAD_SIZE; i++) {
        hs_file[4 + i] ^= (uint8_t)(0xA5u ^ i);
    }
}

static uint16_t hs_checksum(void) {
    const uint8_t* data = &hs_file[4];
    uint16_t sum = 0x1357;
    uint8_t length = HS_PAYLOAD_SIZE;

    while(length-- != 0) {
        sum = (uint16_t)(sum + *data++);
        sum = (uint16_t)((sum << 3) | (sum >> 13));
        sum ^= 0x5A5Au;
    }

    return sum;
}

#define HS_ENTRY(index) (&hs_file[5 + (uint8_t)((index) * (HIGHSCORE_NAME_LEN + 2))])

static uint16_t hs_score_of(uint8_t index) {
    const uint8_t* entry = HS_ENTRY(index);

    return (uint16_t)(entry[HIGHSCORE_NAME_LEN] |
                      ((uint16_t)entry[HIGHSCORE_NAME_LEN + 1] << 8));
}

static void hs_load(void) {
    zos_dev_t dev;
    uint16_t size = HS_FILE_SIZE;
    uint16_t stored;

    hs_file[4] = 0;

    dev = open(HIGHSCORE_FILE, O_RDONLY);
    if(dev < 0) {
        return;
    }
    if(read(dev, hs_file, &size) != ERR_SUCCESS || size != HS_FILE_SIZE) {
        close(dev);
        hs_file[4] = 0;
        return;
    }
    close(dev);

    if(hs_file[0] != HS_MAGIC0 || hs_file[1] != HS_MAGIC1 ||
       hs_file[2] != HS_MAGIC2 || hs_file[3] != HS_MAGIC3) {
        hs_file[4] = 0;
        return;
    }

    hs_xor_payload();

    stored = (uint16_t)(hs_file[4 + HS_PAYLOAD_SIZE] |
                        ((uint16_t)hs_file[5 + HS_PAYLOAD_SIZE] << 8));
    if(stored != hs_checksum() || hs_file[4] > HIGHSCORE_MAX) {
        hs_file[4] = 0;
    }
}

static uint8_t hs_save(void) {
    zos_dev_t dev;
    uint16_t size = HS_FILE_SIZE;
    uint16_t check;
    uint8_t ok;

    hs_file[0] = HS_MAGIC0;
    hs_file[1] = HS_MAGIC1;
    hs_file[2] = HS_MAGIC2;
    hs_file[3] = HS_MAGIC3;

    check = hs_checksum();
    hs_file[4 + HS_PAYLOAD_SIZE] = (uint8_t)(check & 0xffu);
    hs_file[5 + HS_PAYLOAD_SIZE] = (uint8_t)(check >> 8);

    hs_xor_payload();

    dev = open(HIGHSCORE_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if(dev < 0) {
        hs_xor_payload();
        return 0;
    }

    ok = (uint8_t)(write(dev, hs_file, &size) == ERR_SUCCESS &&
                   size == HS_FILE_SIZE);
    close(dev);
    hs_xor_payload();

    return ok;
}

static uint8_t hs_qualifies(uint16_t score) {
    if(score == 0) {
        return 0;
    }
    if(hs_file[4] < HIGHSCORE_MAX) {
        return 1;
    }
    return (uint8_t)(score > hs_score_of(HIGHSCORE_MAX - 1));
}

static uint8_t hs_insert(const char* name, uint8_t length, uint16_t score) {
    uint8_t pos;
    uint8_t i;
    uint8_t* entry;

    if(length > HIGHSCORE_NAME_LEN) {
        length = HIGHSCORE_NAME_LEN;
    }

    if(hs_file[4] < HIGHSCORE_MAX) {
        pos = hs_file[4];
        hs_file[4]++;
    } else {
        pos = HIGHSCORE_MAX - 1;
    }

    while(pos != 0 && hs_score_of((uint8_t)(pos - 1)) < score) {
        memcpy(HS_ENTRY(pos), HS_ENTRY((uint8_t)(pos - 1)),
               HIGHSCORE_NAME_LEN + 2);
        pos--;
    }

    entry = HS_ENTRY(pos);
    for(i = 0; i < HIGHSCORE_NAME_LEN; i++) {
        entry[i] = (uint8_t)(i < length ? name[i] : ' ');
    }
    entry[HIGHSCORE_NAME_LEN] = (uint8_t)(score & 0xffu);
    entry[HIGHSCORE_NAME_LEN + 1] = (uint8_t)(score >> 8);

    return pos;
}

/* ------------------------------------------------------------------------- */
/* Keyboard                                                                  */
/* ------------------------------------------------------------------------- */

/* Read once from DEV_STDIN and keep the first printable key press. */
static void hs_key_poll(void) {
    uint8_t buffer[32];
    uint16_t size = (uint16_t)sizeof(buffer);
    uint8_t i;

    if(read(DEV_STDIN, buffer, &size) != ERR_SUCCESS) {
        return;
    }

    for(i = 0; i < size; i++) {
        const uint8_t code = buffer[i];

        if(code == KB_RELEASED) {
            hs_release_pending = 1;
            continue;
        }
        if(hs_release_pending) {
            hs_release_pending = 0;
            continue;
        }
        if(hs_key != 0) {
            continue;
        }

        if(code >= 'a' && code <= 'z') {
            hs_key = (uint8_t)(code - 'a' + 'A');
        } else if(code >= 'A' && code <= 'Z') {
            hs_key = code;
        } else if(code >= '0' && code <= '9') {
            hs_key = code;
        } else if(code == KB_KEY_SPACE) {
            hs_key = ' ';
        } else if(code == KB_KEY_ENTER || code == KB_NUMPAD_ENTER) {
            hs_key = '\n';
        } else if(code == KB_KEY_BACKSPACE) {
            hs_key = '\b';
        }
    }
}

static uint8_t hs_key_take(void) {
    const uint8_t key = hs_key;

    hs_key = 0;
    return key;
}

/* Discard pending input for a short settling period. */
static void hs_drain(gfx_context* ctx, uint8_t frames) {
    while(frames-- != 0) {
        gfx_wait_vblank(ctx);
        hs_key_poll();
        hs_key_take();
        xsfx_update();
        gfx_wait_end_vblank(ctx);
    }
}

/* ------------------------------------------------------------------------- */
/* Rendering                                                                 */
/* ------------------------------------------------------------------------- */

static void hs_clear_screen(gfx_context* ctx) {
    uint8_t y;

    for(y = 0; y < FIELD_ROWS; y++) {
        field_hud_clear_row(ctx, y);
    }
}

static void hs_entry_name(const uint8_t* entry, char* name) {
    uint8_t i;

    for(i = 0; i < HIGHSCORE_NAME_LEN; i++) {
        name[i] = (char)entry[i];
    }
    name[HIGHSCORE_NAME_LEN] = 0;
}

static void hs_draw_name_line(gfx_context* ctx, const char* name,
                              uint8_t length) {
    char line[HIGHSCORE_NAME_LEN + 1];
    uint8_t i;

    for(i = 0; i < HIGHSCORE_NAME_LEN; i++) {
        line[i] = (char)(i < length ? name[i] : ' ');
    }
    line[HIGHSCORE_NAME_LEN] = 0;

    field_hud_text(ctx, HS_NAME_X, 14, line);
    if(length < HIGHSCORE_NAME_LEN) {
        field_hud_tile(ctx, (uint8_t)(HS_NAME_X + length), 14, TILE_HUD_GT);
    }
}

static void hs_draw_name_screen(gfx_context* ctx, const char* name,
                                uint8_t length, uint16_t score) {
    hs_clear_screen(ctx);

    field_hud_text(ctx, 13, 4, "NEW HIGH SCORE");
    field_hud_text(ctx, 13, 8, "SCORE");
    field_hud_number(ctx, 19, 8, score, 5);
    field_hud_text(ctx, 12, 11, "ENTER YOUR NAME");
    hs_draw_name_line(ctx, name, length);
    field_hud_text(ctx, 9, 18, "ENTER OK BACKSPACE DEL");
}

static void hs_draw_row(gfx_context* ctx, uint8_t y, uint8_t rank,
                        const uint8_t* entry, uint8_t marker) {
    char name[HIGHSCORE_NAME_LEN + 1];
    uint16_t score;

    hs_entry_name(entry, name);
    score = (uint16_t)(entry[HIGHSCORE_NAME_LEN] |
                       ((uint16_t)entry[HIGHSCORE_NAME_LEN + 1] << 8));

    field_hud_tile(ctx, HS_TABLE_X, y, marker ? TILE_HUD_GT : TILE_SEA);
    field_hud_number(ctx, HS_TABLE_X + 1, y, rank, 2);
    field_hud_text(ctx, HS_TABLE_X + 4, y, name);
    field_hud_number(ctx, HS_TABLE_X + 14, y, score, 5);
}

static void hs_draw_table(gfx_context* ctx, uint8_t highlight) {
    uint8_t i;

    hs_clear_screen(ctx);
    field_hud_text(ctx, 14, 2, "HIGH SCORES");
    field_hud_text(ctx, HS_TABLE_X, 4, "    NAME      SCORE");

    for(i = 0; i < hs_file[4]; i++) {
        hs_draw_row(ctx, (uint8_t)(HS_TABLE_Y + i), (uint8_t)(i + 1),
                    HS_ENTRY(i), (uint8_t)(i == highlight));
    }

    if(hs_file[4] == 0) {
        field_hud_text(ctx, 13, 12, "NO SCORES YET");
    }

    field_hud_text(ctx, 13, 20, "PRESS ANY KEY");
}

/* ------------------------------------------------------------------------- */
/* Blocking screens                                                          */
/* ------------------------------------------------------------------------- */

static uint8_t hs_enter_name(gfx_context* ctx, uint16_t score, char* name) {
    uint8_t length = 0;

    name[0] = 0;
    hs_draw_name_screen(ctx, name, length, score);
    hs_drain(ctx, 30);

    for(;;) {
        uint8_t key;

        gfx_wait_vblank(ctx);
        hs_key_poll();
        key = hs_key_take();
        xsfx_update();
        if(key == 0) {
            gfx_wait_end_vblank(ctx);
            continue;
        }

        if(key == '\n') {
            break;
        }
        if(key == '\b') {
            if(length != 0) {
                length--;
            }
        } else if((key >= 'A' && key <= 'Z') ||
                  (key >= '0' && key <= '9') || key == ' ') {
            if(length < HIGHSCORE_NAME_LEN) {
                name[length] = (char)key;
                length++;
            }
        }

        name[length] = 0;
        hs_draw_name_line(ctx, name, length);
        gfx_wait_end_vblank(ctx);
    }

    while(length != 0 && name[length - 1] == ' ') {
        length--;
    }
    name[length] = 0;

    if(length == 0) {
        memcpy(name, "PLAYER", 7);
        length = 6;
    }

    return length;
}

static void hs_wait_table(gfx_context* ctx) {
    uint16_t frames = 0;

    hs_drain(ctx, 30);

    while(frames < HS_WAIT_FRAMES) {
        gfx_wait_vblank(ctx);
        hs_key_poll();
        xsfx_update();
        if(hs_key_take() != 0) {
            break;
        }
        gfx_wait_end_vblank(ctx);
        frames++;
    }

    hs_key = 0;
}

/* ------------------------------------------------------------------------- */
/* Public entry point                                                        */
/* ------------------------------------------------------------------------- */

void highscore_game_over(gfx_context* ctx, uint16_t score) {
    char name[HIGHSCORE_NAME_LEN + 1];
    uint8_t qualified;
    uint8_t rank = 0xff;
    uint8_t length;

    hs_key = 0;
    hs_release_pending = 0;
    input_flush();
    hs_load();

    qualified = hs_qualifies(score);
    if(qualified) {
        length = hs_enter_name(ctx, score, name);
        rank = hs_insert(name, length, score);
        hs_save();
        if(rank == 0) {
            xsfx_victory();
        }
    }

    hs_draw_table(ctx, rank);
    hs_wait_table(ctx);

    field_hud_blink(ctx, 1);
    input_flush();
    hs_key = 0;
}
