#ifndef SPIDER_H
#define SPIDER_H

#include <stdint.h>

/* Land enemies.  They live on the 4x4 cell grid and move diagonally on LAND. */
#define SPIDER_COUNT    2

typedef struct {
    uint8_t sx;             /* logical cell X, 2..157 */
    uint8_t sy;             /* logical cell Y, 10..117 */
    int8_t  dx;             /* -1 or +1 */
    int8_t  dy;             /* -1 or +1 */
    uint8_t tick_timer;     /* frames until the next cell step */
    uint8_t speed_div;      /* frames per 4-pixel cell */
    uint8_t active;
} spider_t;

extern spider_t spiders[SPIDER_COUNT];

/* Put the requested number of spiders (0..SPIDER_COUNT) on the bottom frame. */
void spider_reset(uint8_t count);

/*
 * Advance the spiders by one frame and return 1 when one of them touches the
 * player.  px/py are the player's logical cell coordinates.
 */
uint8_t spider_update(uint8_t px, uint8_t py);

/* 1 when any active spider is inside the player's 8x8-pixel hit box. */
uint8_t spider_hits_player(uint8_t px, uint8_t py);

#endif
