#include <stddef.h>
#include <string.h>
#include <zvb_sprite.h>
#include <zgdk/input/button_map.h>

#include "game.h"
#include "field.h"
#include "spider.h"
#include "levels.h"
#include "highscore.h"
#include "sound.h"

#define PLAYER_SPRITE   0
#define BALL_SPRITE     1
#define BALL_MAX        5
#define SPIDER_SPRITE_BASE  (BALL_SPRITE + BALL_MAX)

/*
 * The player's logical position is the 4x4 cell under the centre of the
 * sprite.  Cell 82 is the centre of tile column 20; cell row 11 is the inner
 * half of the 8-pixel top frame, so pressing down immediately enters the sea.
 */
#define PLAYER_START_CX 82
#define PLAYER_START_CY 11

#define BALL_MIN_X      (FIELD_SEA_X0 * FIELD_CELL_SIZE)                    /* 16  */
#define BALL_MAX_X      (((FIELD_SEA_X1 + 1) * FIELD_CELL_SIZE) - FIELD_TILE_SIZE) /* 608 */
#define BALL_MIN_Y      (FIELD_SEA_Y0 * FIELD_CELL_SIZE)                    /* 48  */
#define BALL_MAX_Y      (((FIELD_SEA_Y1 + 1) * FIELD_CELL_SIZE) - FIELD_TILE_SIZE) /* 448 */

#define DIR_NONE        0
#define DIR_LEFT        1
#define DIR_RIGHT       2
#define DIR_UP          3
#define DIR_DOWN        4

#define MOVE_OK         0
#define MOVE_BLOCKED    1
#define MOVE_DIED       2

/* HUD layout, in tilemap coordinates on the top tile row. */
#define HUD_Y           0
#define HUD_LEVEL_X     6
#define HUD_TIME_X      10
#define HUD_PCT_X       17
#define HUD_SCORE_X     22

/* Shown on the second HUD line between levels. */
#define WAIT_TEXT       ">>> PRESS ANY KEY TO CONTINUE <<<"
#define WAIT_BLINK_FRAMES 25
/* A held or stuck key must not leave the transition prompt forever. */
#define WAIT_ARM_FRAMES   60
#define WAIT_AUTO_FRAMES  (30u * 60u)

typedef struct {
    int16_t x;
    int16_t y;
    int8_t  vx;
    int8_t  vy;
    uint8_t sub_x;      /* fractional pixel accumulator, 0..4 */
    uint8_t sub_y;
    uint8_t frac;       /* extra pixel every 5 frames: 0, 2 or 4 */
    uint8_t active;
} ball_t;

static ball_t balls[BALL_MAX];
static uint8_t ball_count;

/* Initial ball positions spread over the sea.  x/y are sprite top-left. */
static const int16_t ball_start_x[BALL_MAX] = { 200, 420, 300, 150, 470 };
static const int16_t ball_start_y[BALL_MAX] = { 200, 280, 120, 360, 380 };
static const int8_t  ball_start_vx[BALL_MAX] = { 1, -1, 1, -1, 1 };
static const int8_t  ball_start_vy[BALL_MAX] = { 1, 1, -1, -1, 1 };

/* Level state. */
static uint8_t level;
static uint16_t score;
static uint16_t level_time;
static uint16_t time_left;
static uint8_t time_frames;
static uint8_t level_ball_speed;
static uint8_t level_spiders;

/* Values currently shown on the HUD, to avoid redrawing every frame. */
static uint8_t hud_level;
static uint8_t hud_lives;
static uint8_t hud_pct;
static uint16_t hud_score;
static uint16_t hud_time;

/* Between-level pause: message shown, waiting for a fresh key press. */
static uint8_t wait_for_key;
static uint8_t wait_armed;
static uint8_t wait_blink;
static uint8_t wait_visible;
static uint8_t wait_arm_frames;
static uint16_t wait_total;
static uint8_t pending_level;

static uint8_t player_cx;
static uint8_t player_cy;
static uint8_t player_dir;
static uint8_t turn_request;
static uint8_t drawing;
static uint8_t lives;
static uint16_t prev_keys;
static uint8_t await_release;
static uint8_t restart_requested;

static gfx_sprite player_sprite;

static uint8_t is_dir_button(uint16_t keys) {
    return (keys & (BUTTON_LEFT | BUTTON_RIGHT | BUTTON_UP | BUTTON_DOWN)) != 0;
}

static uint8_t dir_from_keys(uint16_t keys) {
    if(keys & BUTTON_LEFT)  return DIR_LEFT;
    if(keys & BUTTON_RIGHT) return DIR_RIGHT;
    if(keys & BUTTON_UP)    return DIR_UP;
    if(keys & BUTTON_DOWN)  return DIR_DOWN;
    return DIR_NONE;
}

static uint8_t opposite_dir(uint8_t dir) {
    switch(dir) {
        case DIR_LEFT:  return DIR_RIGHT;
        case DIR_RIGHT: return DIR_LEFT;
        case DIR_UP:    return DIR_DOWN;
        case DIR_DOWN:  return DIR_UP;
        default:        return DIR_NONE;
    }
}

static int8_t dir_dx(uint8_t dir) {
    if(dir == DIR_LEFT)  return -1;
    if(dir == DIR_RIGHT) return 1;
    return 0;
}

static int8_t dir_dy(uint8_t dir) {
    if(dir == DIR_UP)   return -1;
    if(dir == DIR_DOWN) return 1;
    return 0;
}

/* Next playable cell in the given direction, if any. */
static uint8_t next_cell(uint8_t dir, uint8_t* x, uint8_t* y) {
    int16_t nx = (int16_t)player_cx + dir_dx(dir);
    int16_t ny = (int16_t)player_cy + dir_dy(dir);

    if(nx < FIELD_PLAY_X0 || nx > FIELD_PLAY_X1 ||
       ny < FIELD_PLAY_Y0 || ny > FIELD_PLAY_Y1) {
        return 0;
    }

    *x = (uint8_t)nx;
    *y = (uint8_t)ny;
    return 1;
}

/* True when the player is on the 8-pixel frame (not on captured land). */
static uint8_t player_on_frame(void) {
    return player_cx <= 3 || player_cx >= 156 ||
           player_cy <= 11 || player_cy >= 116;
}

/* A turn is useful only if it stays inside the playfield and is not a trail. */
static uint8_t can_turn(uint8_t dir) {
    uint8_t x, y;

    if(!next_cell(dir, &x, &y)) {
        return 0;
    }
    return field_get_cell(x, y) != CELL_TRAIL;
}

static void reset_balls(void);
static void render_player(gfx_context* ctx);
static void render_balls(gfx_context* ctx);
static void render_spiders(gfx_context* ctx);
static void lose_life(gfx_context* ctx);
static void level_load(gfx_context* ctx, uint8_t n);
static void hud_draw_all(gfx_context* ctx);
static void hud_update(gfx_context* ctx);

static void hide_sprite(gfx_context* ctx, uint8_t index) {
    gfx_sprite sprite;

    sprite.tile = 0;
    sprite.x = 0;
    sprite.y = 0;
    sprite.flags = 0;
    sprite.options = 0;

    gfx_sprite_render(ctx, index, &sprite);
}

/*
 * Move one 4x4 cell.  Returning to LAND while a trail is active closes the
 * shape and runs the capture immediately.
 */
static uint8_t step(gfx_context* ctx, uint8_t dir) {
    uint8_t nx, ny;
    uint8_t state;

    if(!next_cell(dir, &nx, &ny)) {
        return MOVE_BLOCKED;
    }

    state = field_get_cell(nx, ny);
    if(state == CELL_TRAIL) {
        return MOVE_DIED;
    }

    player_cx = nx;
    player_cy = ny;
    player_dir = dir;

    if(state == CELL_SEA) {
        field_set_cell(nx, ny, CELL_TRAIL);
        drawing = 1;
    } else if(drawing) {
        uint16_t bx[BALL_MAX];
        uint16_t by[BALL_MAX];
        uint8_t i;

        for(i = 0; i < ball_count; i++) {
            bx[i] = (uint16_t)balls[i].x;
            by[i] = (uint16_t)balls[i].y;
        }

        field_capture(ctx, bx, by, ball_count);
        xsfx_capture();
        drawing = 0;
    }

    return MOVE_OK;
}

/*
 * Move the player for one frame.  The run continues in the current direction;
 * a new direction is applied at the next 4-pixel node.  Returns 1 when the
 * level was reset because the player crossed their own trail.
 */
static uint8_t update_player(gfx_context* ctx, uint16_t keys) {
    uint8_t want = DIR_NONE;

    if(await_release) {
        if(!is_dir_button(keys)) {
            await_release = 0;
        }
    } else {
        want = dir_from_keys((uint16_t)(keys & ~prev_keys));
    }

    prev_keys = keys;

    if(want != DIR_NONE) {
        if(player_dir == DIR_NONE) {
            uint8_t result = step(ctx, want);

            if(result == MOVE_DIED) {
                goto died;
            }
            if(result == MOVE_BLOCKED) {
                player_dir = DIR_NONE;
            }
            return 0;
        }

        if(want != player_dir) {
            if(want == opposite_dir(player_dir)) {
                if(player_on_frame() && can_turn(want)) {
                    turn_request = want;
                }
            } else if(can_turn(want)) {
                turn_request = want;
            }
        }
    }

    if(player_dir != DIR_NONE) {
        uint8_t dir = player_dir;
        uint8_t result;

        if(turn_request != DIR_NONE) {
            dir = turn_request;
            turn_request = DIR_NONE;
        }

        result = step(ctx, dir);
        if(result == MOVE_DIED) {
            goto died;
        }
        if(result == MOVE_BLOCKED) {
            player_dir = DIR_NONE;
            turn_request = DIR_NONE;
        }
    }

    return 0;

died:
    lose_life(ctx);
    return 1;
}

static void render_player(gfx_context* ctx) {
    /*
     * The sprite hardware coordinate is the 16x16 tile's top-left plus 16.
     * Place the ball centre on the logical cell centre:
     *   cell centre = cx * 4 + 2
     *   sprite top  = cell centre - 8
     *   hardware x  = sprite top + 16 = cx * 4 + 10
     */
    player_sprite.tile = TILE_PLAYER;
    player_sprite.x = (uint16_t)((uint16_t)player_cx * FIELD_CELL_SIZE + 10);
    player_sprite.y = (uint16_t)((uint16_t)player_cy * FIELD_CELL_SIZE + 10);
    player_sprite.flags = SPRITE_PALETTE_00;
    player_sprite.options = 0;

    gfx_sprite_render(ctx, PLAYER_SPRITE, &player_sprite);
}

static void render_ball(gfx_context* ctx, uint8_t index) {
    gfx_sprite sprite;

    sprite.tile = TILE_BALL;
    sprite.x = (uint16_t)(balls[index].x + FIELD_TILE_SIZE);
    sprite.y = (uint16_t)(balls[index].y + FIELD_TILE_SIZE);
    sprite.flags = SPRITE_PALETTE_00;
    sprite.options = 0;

    gfx_sprite_render(ctx, (uint8_t)(BALL_SPRITE + index), &sprite);
}

static void render_balls(gfx_context* ctx) {
    uint8_t i;

    for(i = 0; i < BALL_MAX; i++) {
        if(balls[i].active) {
            render_ball(ctx, i);
        } else {
            hide_sprite(ctx, (uint8_t)(BALL_SPRITE + i));
        }
    }
}

/*
 * Spiders move one 4-pixel cell every speed_div frames.  Between steps the
 * sprite is drawn towards the next cell so the motion stays smooth.
 */
static void render_spiders(gfx_context* ctx) {
    gfx_sprite sprite;
    uint8_t i;

    sprite.flags = SPRITE_PALETTE_00;
    sprite.options = 0;

    for(i = 0; i < SPIDER_COUNT; i++) {
        const spider_t* sp = &spiders[i];

        if(!sp->active) {
            sprite.tile = 0;
            sprite.x = 0;
            sprite.y = 0;
        } else {
            const uint8_t div = sp->speed_div != 0 ? sp->speed_div : 1;
            const uint16_t off = (uint16_t)(((uint16_t)sp->tick_timer *
                                             FIELD_CELL_SIZE) / div);
            const int16_t x = (int16_t)((uint16_t)sp->sx * FIELD_CELL_SIZE + 10) +
                              (int16_t)sp->dx * (int16_t)off;
            const int16_t y = (int16_t)((uint16_t)sp->sy * FIELD_CELL_SIZE + 10) +
                              (int16_t)sp->dy * (int16_t)off;

            sprite.tile = TILE_SPIDER;
            sprite.x = (uint16_t)x;
            sprite.y = (uint16_t)y;
        }

        gfx_sprite_render(ctx, (uint8_t)(SPIDER_SPRITE_BASE + i), &sprite);
    }
}

static void relocate_ball(uint8_t index) {
    uint8_t start_y = (uint8_t)(FIELD_SEA_Y0 + 2 + ((uint16_t)index * 37u) % 100u);
    uint8_t n;

    /* Scan whole rows, starting at different rows for each ball. */
    for(n = 0; n < 100; n++) {
        uint8_t cy = (uint8_t)(FIELD_SEA_Y0 + 2 + ((start_y - (FIELD_SEA_Y0 + 2) + n) % 100));
        uint8_t cx;

        for(cx = FIELD_SEA_X0 + 2; cx <= FIELD_SEA_X1 - 2; cx++) {
            if(field_get_cell(cx, cy) == CELL_SEA) {
                balls[index].x = (int16_t)cx * FIELD_CELL_SIZE - 6;
                balls[index].y = (int16_t)cy * FIELD_CELL_SIZE - 6;
                balls[index].vx = (index & 1) ? -1 : 1;
                balls[index].vy = (index & 1) ? 1 : -1;
                return;
            }
        }
    }

    /* Tiny field fallback: put the ball in the middle of the sea. */
    balls[index].x = (int16_t)BALL_MIN_X;
    balls[index].y = (int16_t)BALL_MIN_Y;
    balls[index].vx = 1;
    balls[index].vy = 1;
}

static void reset_ball(uint8_t index) {
    balls[index].x = ball_start_x[index];
    balls[index].y = ball_start_y[index];
    balls[index].vx = ball_start_vx[index];
    balls[index].vy = ball_start_vy[index];
    balls[index].sub_x = 0;
    balls[index].sub_y = 0;
    balls[index].frac = level_speed_frac(level_ball_speed);
    balls[index].active = 1;

    if(field_cell_at_px((uint16_t)(balls[index].x + 8),
                        (uint16_t)(balls[index].y + 8)) != CELL_SEA) {
        relocate_ball(index);
    }
}

static void reset_balls(void) {
    uint8_t i;

    for(i = 0; i < BALL_MAX; i++) {
        if(i < ball_count) {
            reset_ball(i);
        } else {
            balls[i].active = 0;
        }
    }
}

static uint8_t ball_hits_trail(int16_t px, int16_t py) {
    return field_cell_at_px((uint16_t)px, (uint16_t)py) == CELL_TRAIL;
}

static uint8_t ball_hits_solid(int16_t px, int16_t py) {
    return field_cell_at_px((uint16_t)px, (uint16_t)py) != CELL_SEA;
}

static int16_t clamp_ball_x(int16_t x) {
    if(x < BALL_MIN_X) return BALL_MIN_X;
    if(x > BALL_MAX_X) return BALL_MAX_X;
    return x;
}

static int16_t clamp_ball_y(int16_t y) {
    if(y < BALL_MIN_Y) return BALL_MIN_Y;
    if(y > BALL_MAX_Y) return BALL_MAX_Y;
    return y;
}

/* Slow balls move 2 px per frame; medium/fast add 2/5 and 4/5 px on average. */
static uint8_t ball_axis_step(uint8_t* sub, uint8_t frac) {
    *sub = (uint8_t)(*sub + frac);
    if(*sub >= 5) {
        *sub = (uint8_t)(*sub - 5);
        return 3;
    }
    return 2;
}

/* Move the active balls; returns 1 when one of them touches the trail. */
static uint8_t update_balls(gfx_context* ctx) {
    uint8_t hit = 0;
    uint8_t bounced = 0;
    uint8_t i;

    (void)ctx;

    for(i = 0; i < BALL_MAX; i++) {
        ball_t* ball = &balls[i];
        int16_t nx, ny;
        uint8_t step_x, step_y;

        if(!ball->active) {
            continue;
        }

        step_x = ball_axis_step(&ball->sub_x, ball->frac);
        step_y = ball_axis_step(&ball->sub_y, ball->frac);

        nx = (int16_t)(ball->x + ball->vx * (int16_t)step_x);
        ny = (int16_t)(ball->y + ball->vy * (int16_t)step_y);

        if(nx < BALL_MIN_X) {
            nx = BALL_MIN_X;
            ball->vx = (int8_t)-ball->vx;
            bounced = 1;
        } else if(nx > BALL_MAX_X) {
            nx = BALL_MAX_X;
            ball->vx = (int8_t)-ball->vx;
            bounced = 1;
        }

        if(ny < BALL_MIN_Y) {
            ny = BALL_MIN_Y;
            ball->vy = (int8_t)-ball->vy;
            bounced = 1;
        } else if(ny > BALL_MAX_Y) {
            ny = BALL_MAX_Y;
            ball->vy = (int8_t)-ball->vy;
            bounced = 1;
        }

        /* Leading edge of the visible ball, three samples across it. */
        {
            int16_t edge_x = (int16_t)(nx + (ball->vx > 0 ? 12 : 4));
            int16_t cy = (int16_t)(ball->y + 8);

            if(ball_hits_trail(edge_x, (int16_t)(cy - 3)) ||
               ball_hits_trail(edge_x, cy) ||
               ball_hits_trail(edge_x, (int16_t)(cy + 3))) {
                hit = 1;
            } else if(ball_hits_solid(edge_x, (int16_t)(cy - 3)) ||
                      ball_hits_solid(edge_x, cy) ||
                      ball_hits_solid(edge_x, (int16_t)(cy + 3))) {
                ball->vx = (int8_t)-ball->vx;
                nx = clamp_ball_x((int16_t)(ball->x + ball->vx * (int16_t)step_x));
                bounced = 1;
            }
        }

        {
            int16_t edge_y = (int16_t)(ny + (ball->vy > 0 ? 12 : 4));
            int16_t cx = (int16_t)(nx + 8);

            if(ball_hits_trail((int16_t)(cx - 3), edge_y) ||
               ball_hits_trail(cx, edge_y) ||
               ball_hits_trail((int16_t)(cx + 3), edge_y)) {
                hit = 1;
            } else if(ball_hits_solid((int16_t)(cx - 3), edge_y) ||
                      ball_hits_solid(cx, edge_y) ||
                      ball_hits_solid((int16_t)(cx + 3), edge_y)) {
                ball->vy = (int8_t)-ball->vy;
                ny = clamp_ball_y((int16_t)(ball->y + ball->vy * (int16_t)step_y));
                bounced = 1;
            }
        }

        ball->x = nx;
        ball->y = ny;
    }

    if(bounced) {
        xsfx_ball_bounce();
    }

    return hit;
}

/* ------------------------------------------------------------------------- */
/* HUD                                                                       */
/* ------------------------------------------------------------------------- */

static void hud_draw_all(gfx_context* ctx) {
    const uint8_t pct = field_captured_percent();

    field_draw_lives(ctx, lives);

    field_hud_tile(ctx, HUD_LEVEL_X, HUD_Y, TILE_HUD_FLAG);
    field_hud_number(ctx, HUD_LEVEL_X + 1, HUD_Y, level, 2);

    field_hud_tile(ctx, HUD_TIME_X, HUD_Y, TILE_HUD_CLOCK);
    field_hud_number(ctx, HUD_TIME_X + 1, HUD_Y, (uint16_t)(time_left / 60u), 2);
    field_hud_tile(ctx, HUD_TIME_X + 3, HUD_Y, TILE_HUD_COLON);
    field_hud_number(ctx, HUD_TIME_X + 4, HUD_Y, (uint16_t)(time_left % 60u), 2);

    field_hud_number(ctx, HUD_PCT_X, HUD_Y, pct, 3);
    field_hud_tile(ctx, HUD_PCT_X + 3, HUD_Y, TILE_HUD_PERCENT);

    field_hud_tile(ctx, HUD_SCORE_X, HUD_Y, TILE_BALL);
    field_hud_number(ctx, HUD_SCORE_X + 1, HUD_Y, score, 5);

    hud_level = level;
    hud_lives = lives;
    hud_pct = pct;
    hud_score = score;
    hud_time = time_left;
}

static void hud_update(gfx_context* ctx) {
    const uint8_t pct = field_captured_percent();

    if(hud_lives != lives) {
        field_draw_lives(ctx, lives);
        hud_lives = lives;
    }

    if(hud_level != level) {
        field_hud_tile(ctx, HUD_LEVEL_X, HUD_Y, TILE_HUD_FLAG);
        field_hud_number(ctx, HUD_LEVEL_X + 1, HUD_Y, level, 2);
        hud_level = level;
    }

    if(hud_time != time_left) {
        field_hud_tile(ctx, HUD_TIME_X, HUD_Y, TILE_HUD_CLOCK);
        field_hud_number(ctx, HUD_TIME_X + 1, HUD_Y, (uint16_t)(time_left / 60u), 2);
        field_hud_tile(ctx, HUD_TIME_X + 3, HUD_Y, TILE_HUD_COLON);
        field_hud_number(ctx, HUD_TIME_X + 4, HUD_Y, (uint16_t)(time_left % 60u), 2);
        hud_time = time_left;
    }

    if(hud_pct != pct) {
        field_hud_number(ctx, HUD_PCT_X, HUD_Y, pct, 3);
        field_hud_tile(ctx, HUD_PCT_X + 3, HUD_Y, TILE_HUD_PERCENT);
        hud_pct = pct;
    }

    if(hud_score != score) {
        field_hud_tile(ctx, HUD_SCORE_X, HUD_Y, TILE_BALL);
        field_hud_number(ctx, HUD_SCORE_X + 1, HUD_Y, score, 5);
        hud_score = score;
    }
}

static void hud_draw_wait(gfx_context* ctx) {
    const uint8_t x = (uint8_t)((FIELD_COLS - (sizeof(WAIT_TEXT) - 1)) / 2);

    field_hud_text(ctx, x, 1, WAIT_TEXT);
}

/* ------------------------------------------------------------------------- */
/* Level flow                                                                */
/* ------------------------------------------------------------------------- */

static void reset_level(gfx_context* ctx) {
    xsfx_stop_gameplay();
    if(level_spiders != 0) {
        xsfx_buzz_start();
    }

    field_reset(ctx);

    player_cx = PLAYER_START_CX;
    player_cy = PLAYER_START_CY;
    player_dir = DIR_NONE;
    turn_request = DIR_NONE;
    drawing = 0;

    time_left = level_time;
    time_frames = 0;

    reset_balls();
    spider_reset(level_spiders);

    field_hud_clear_row(ctx, 1);
    hud_draw_all(ctx);
    render_player(ctx);
    render_balls(ctx);
    render_spiders(ctx);
}

static void level_load(gfx_context* ctx, uint8_t n) {
    const level_t* cfg;

    if(n < 1) {
        n = 1;
    }
    if(n > LEVEL_COUNT) {
        n = LEVEL_COUNT;
    }

    level = n;
    cfg = &levels[n - 1];

    ball_count = cfg->balls;
    if(ball_count > BALL_MAX) {
        ball_count = BALL_MAX;
    }
    level_ball_speed = cfg->speed;
    level_spiders = cfg->spiders;
    if(level_spiders > SPIDER_COUNT) {
        level_spiders = SPIDER_COUNT;
    }
    level_time = cfg->time;
    time_left = level_time;
    time_frames = 0;

    reset_level(ctx);
}

static void level_complete(gfx_context* ctx) {
    const uint8_t pct = field_captured_percent();

    xsfx_buzz_stop();

    /* Points for the attempt are only awarded when the level is finished. */
    score = (uint16_t)(score + level_score(pct));

    if(level >= LEVEL_COUNT) {
        /* All 21 levels cleared: start again from the first one, keeping the
         * accumulated score and the remaining lives. */
        pending_level = 1;
    } else {
        pending_level = (uint8_t)(level + 1);
    }

    wait_for_key = 1;
    wait_armed = 0;
    wait_blink = 0;
    wait_visible = 1;
    wait_arm_frames = 0;
    wait_total = 0;
    field_hud_blink(ctx, 1);

    /* Show the earned points and percentage in the first HUD line, then the
     * prompt in the second one. */
    hud_update(ctx);
    hud_draw_wait(ctx);
}

static void lose_life(gfx_context* ctx) {
    if(lives > 0) {
        lives--;
    }

    if(lives == 0) {
        /* Out of lives: hide the playfield and run the high-score flow.
         * main() restarts the program (splash and all) afterwards. */
        xsfx_stop_gameplay();
        xsfx_game_over();
        game_shutdown(ctx);
        highscore_game_over(ctx, score);
        restart_requested = 1;
        return;
    }

    xsfx_life_lost();

    /* Current level restarts; its percentage and score for this attempt
     * are dropped, previous levels' score is kept. */
    reset_level(ctx);
    await_release = 1;
}

/* Returns 1 when the time limit expired and the level restarted. */
static uint8_t update_timer(gfx_context* ctx) {
    if(time_left == 0) {
        return 0;
    }

    time_frames++;
    if(time_frames < 60) {
        return 0;
    }

    time_frames = 0;
    time_left--;

    if(time_left == 0) {
        lose_life(ctx);
        return 1;
    }

    return 0;
}

void game_init(gfx_context* ctx, uint8_t start_level) {
    lives = FIELD_LIVES_MAX;
    score = 0;
    prev_keys = 0;
    await_release = 0;
    restart_requested = 0;
    wait_for_key = 0;
    wait_armed = 0;
    wait_arm_frames = 0;
    wait_total = 0;
    turn_request = DIR_NONE;

    level_load(ctx, start_level);
}

void game_update(gfx_context* ctx, uint16_t keys) {
    if(wait_for_key) {
        uint8_t advance = 0;

        if(wait_armed) {
            if(keys != 0) {
                advance = 1;
            }
        } else if(keys == 0) {
            /* Wait for the key that closed the level to be released. */
            wait_armed = 1;
        } else if(++wait_arm_frames >= WAIT_ARM_FRAMES) {
            /* A held or stuck key should not freeze the transition. */
            wait_armed = 1;
        }

        if(!advance && ++wait_total >= WAIT_AUTO_FRAMES) {
            advance = 1;
        }

        if(advance) {
            wait_for_key = 0;
            field_hud_blink(ctx, 1);
            level_load(ctx, pending_level);
            await_release = 1;
            return;
        }

        /* Blink the prompt by toggling its palette slot. */
        wait_blink++;
        if(wait_blink >= WAIT_BLINK_FRAMES) {
            wait_blink = 0;
            wait_visible = (uint8_t)(wait_visible ^ 1);
            field_hud_blink(ctx, wait_visible);
        }
        return;
    }

    if(update_timer(ctx)) {
        return;
    }

    if(update_balls(ctx)) {
        lose_life(ctx);
        return;
    }

    if(update_player(ctx, keys)) {
        return;
    }

    if(field_captured_percent() >= LEVEL_GOAL) {
        /* Draw the final captured field before showing the prompt. */
        field_render_dirty(ctx);
        render_player(ctx);
        render_balls(ctx);
        render_spiders(ctx);
        level_complete(ctx);
        return;
    }

    if(spider_update(player_cx, player_cy)) {
        lose_life(ctx);
        return;
    }

    field_render_dirty(ctx);
    render_player(ctx);
    render_balls(ctx);
    render_spiders(ctx);
    hud_update(ctx);
}

uint8_t game_restart_requested(void) {
    return restart_requested;
}

void game_shutdown(gfx_context* ctx) {
    uint8_t i;

    for(i = 0; i < (uint8_t)(SPIDER_SPRITE_BASE + SPIDER_COUNT); i++) {
        hide_sprite(ctx, i);
    }
}
