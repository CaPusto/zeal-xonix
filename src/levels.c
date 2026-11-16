#include <stdint.h>

#include "levels.h"

/*
 * Balance table from Xonix_Remake_GDD_Levels.md.
 * Standard time is 5 minutes (300 s); the reduced entries are 15, 25, 20,
 * 20, 35 and 45 percent shorter respectively.
 */
const level_t levels[LEVEL_COUNT] = {
    { 1, BALL_SPEED_SLOW,   0, LEVEL_TIME_STANDARD },   /*  1 */
    { 1, BALL_SPEED_MEDIUM, 0, LEVEL_TIME_STANDARD },   /*  2 */
    { 2, BALL_SPEED_SLOW,   0, LEVEL_TIME_STANDARD },   /*  3 */
    { 2, BALL_SPEED_MEDIUM, 0, LEVEL_TIME_STANDARD },   /*  4 */
    { 2, BALL_SPEED_MEDIUM, 1, LEVEL_TIME_STANDARD },   /*  5 */
    { 3, BALL_SPEED_SLOW,   1, LEVEL_TIME_STANDARD },   /*  6 */
    { 3, BALL_SPEED_MEDIUM, 1, LEVEL_TIME_STANDARD },   /*  7 */
    { 3, BALL_SPEED_FAST,   1, LEVEL_TIME_STANDARD },   /*  8 */
    { 3, BALL_SPEED_FAST,   1, 255 },   /*  9: -15% */
    { 4, BALL_SPEED_SLOW,   1, LEVEL_TIME_STANDARD },   /* 10 */
    { 4, BALL_SPEED_MEDIUM, 1, LEVEL_TIME_STANDARD },   /* 11 */
    { 4, BALL_SPEED_MEDIUM, 2, LEVEL_TIME_STANDARD },   /* 12 */
    { 4, BALL_SPEED_FAST,   2, LEVEL_TIME_STANDARD },   /* 13 */
    { 4, BALL_SPEED_FAST,   2, 225 },   /* 14: -25% */
    { 5, BALL_SPEED_SLOW,   2, LEVEL_TIME_STANDARD },   /* 15 */
    { 5, BALL_SPEED_MEDIUM, 2, LEVEL_TIME_STANDARD },   /* 16 */
    { 5, BALL_SPEED_MEDIUM, 2, 240 },   /* 17: -20% */
    { 5, BALL_SPEED_FAST,   2, LEVEL_TIME_STANDARD },   /* 18 */
    { 5, BALL_SPEED_FAST,   2, 240 },   /* 19: -20% */
    { 5, BALL_SPEED_FAST,   2, 195 },   /* 20: -35% */
    { 5, BALL_SPEED_FAST,   1, 165 },   /* 21: -45% */
};

uint8_t level_speed_frac(uint8_t speed) {
    if(speed == BALL_SPEED_MEDIUM) {
        return 2;   /* 2 + 2/5 px per frame = +20% over slow (2 px) */
    }
    if(speed == BALL_SPEED_FAST) {
        return 4;   /* 2 + 4/5 px per frame = +40% over slow */
    }
    return 0;
}

uint16_t level_score(uint8_t percent) {
    uint16_t extra;

    if(percent < LEVEL_GOAL) {
        return 0;
    }
    if(percent > 100) {
        percent = 100;
    }

    /* 750 points for the first 75%, then all extra points are multiplied by
     * 2 at 85% and by 3 at 91%, as in the GDD examples. */
    extra = (uint16_t)((uint16_t)(percent - LEVEL_GOAL) * 10u);

    if(percent >= 91) {
        return (uint16_t)(750u + extra * 3u);
    }
    if(percent >= 85) {
        return (uint16_t)(750u + extra * 2u);
    }
    return (uint16_t)(750u + extra);
}

uint8_t level_parse_arg(const char* arg) {
    const char* p = arg;

    if(p == 0) {
        return 0;
    }

    while(*p) {
        if(p[0] == '-' && p[1] == '-' && p[2] == 'l' && p[3] == 'e' &&
           p[4] == 'v' && p[5] == 'e' && p[6] == 'l') {
            uint16_t value = 0;
            const char* q = p + 7;

            if(*q == '=') {
                q++;
            }
            while(*q == ' ') {
                q++;
            }
            while(*q >= '0' && *q <= '9') {
                value = (uint16_t)(value * 10u + (uint16_t)(*q - '0'));
                q++;
            }

            if(value >= 1 && value <= LEVEL_COUNT) {
                return (uint8_t)value;
            }
            p = q;
            continue;
        }
        p++;
    }

    return 0;
}
