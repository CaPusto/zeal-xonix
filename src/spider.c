#include <stdint.h>

#include "spider.h"
#include "field.h"

/*
 * Spiders walk on the starting frame and on every captured LAND cell.  They
 * start at the bottom of the field so the player, who starts on the top frame,
 * has time to move.
 */
#define SPIDER_SPAWN_Y      116
#define SPIDER_SPAWN_X0     60
#define SPIDER_SPAWN_X1     100
#define SPIDER_SPEED_DIV    2

spider_t spiders[SPIDER_COUNT];

/*
 * A spider may only stand on LAND strictly inside the playfield.  SEA, the
 * active trail and the 2-cell dead zone outside the frame are all walls.
 */
static uint8_t spider_blocked(uint8_t x, uint8_t y) {
    if(x < FIELD_PLAY_X0 || x > FIELD_PLAY_X1 ||
       y < FIELD_PLAY_Y0 || y > FIELD_PLAY_Y1) {
        return 1;
    }

    return (uint8_t)(field_get_cell(x, y) != CELL_LAND);
}

void spider_reset(uint8_t count) {
    uint8_t i;

    for(i = 0; i < SPIDER_COUNT; i++) {
        spiders[i].active = 0;
    }

    if(count > SPIDER_COUNT) {
        count = SPIDER_COUNT;
    }
    if(count == 0) {
        return;
    }

    spiders[0].sx = SPIDER_SPAWN_X0;
    spiders[0].sy = SPIDER_SPAWN_Y;
    spiders[0].dx = 1;
    spiders[0].dy = -1;
    spiders[0].tick_timer = 0;
    spiders[0].speed_div = SPIDER_SPEED_DIV;
    spiders[0].active = 1;

    if(count > 1) {
        spiders[1].sx = SPIDER_SPAWN_X1;
        spiders[1].sy = SPIDER_SPAWN_Y;
        spiders[1].dx = -1;
        spiders[1].dy = -1;
        spiders[1].tick_timer = 0;
        spiders[1].speed_div = SPIDER_SPEED_DIV;
        spiders[1].active = 1;
    }
}

/*
 * One diagonal step with reflection.
 *
 * The orthogonal probes decide which axis hits a wall.  A reflected diagonal
 * is checked as well: near a concave corner the one-axis bounce can still end
 * up inside the sea.  In that case the spider reverses completely and goes
 * back to the cell it came from, which is always LAND (land only grows).
 */
static void spider_step(spider_t* sp) {
    int8_t dx = sp->dx;
    int8_t dy = sp->dy;
    int16_t nx;
    int16_t ny;

    if(spider_blocked((uint8_t)(sp->sx + dx), sp->sy)) {
        dx = (int8_t)-dx;
    }
    if(spider_blocked(sp->sx, (uint8_t)(sp->sy + dy))) {
        dy = (int8_t)-dy;
    }

    nx = (int16_t)sp->sx + (int16_t)dx;
    ny = (int16_t)sp->sy + (int16_t)dy;

    if(spider_blocked((uint8_t)nx, (uint8_t)ny)) {
        dx = (int8_t)-sp->dx;
        dy = (int8_t)-sp->dy;
        nx = (int16_t)sp->sx + (int16_t)dx;
        ny = (int16_t)sp->sy + (int16_t)dy;
    }

    if(spider_blocked((uint8_t)nx, (uint8_t)ny)) {
        /* Safety net: never step into a wall. */
        return;
    }

    sp->sx = (uint8_t)nx;
    sp->sy = (uint8_t)ny;
    sp->dx = dx;
    sp->dy = dy;
}

uint8_t spider_hits_player(uint8_t px, uint8_t py) {
    uint8_t i;

    for(i = 0; i < SPIDER_COUNT; i++) {
        int16_t dx;
        int16_t dy;

        if(!spiders[i].active) {
            continue;
        }

        dx = (int16_t)spiders[i].sx - (int16_t)px;
        dy = (int16_t)spiders[i].sy - (int16_t)py;
        if(dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
            return 1;
        }
    }

    return 0;
}

uint8_t spider_update(uint8_t px, uint8_t py) {
    uint8_t i;

    /* The player may have just stepped into a spider; check before moving so a
     * simultaneous spider step cannot hide the hit. */
    if(spider_hits_player(px, py)) {
        return 1;
    }

    for(i = 0; i < SPIDER_COUNT; i++) {
        spider_t* sp = &spiders[i];

        if(!sp->active) {
            continue;
        }

        sp->tick_timer++;
        if(sp->tick_timer >= sp->speed_div) {
            sp->tick_timer = 0;
            spider_step(sp);
        }
    }

    return spider_hits_player(px, py);
}
