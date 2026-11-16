#ifndef LEVELS_H
#define LEVELS_H

#include <stdint.h>

#define LEVEL_COUNT         21
#define LEVEL_GOAL          75
#define LEVEL_TIME_STANDARD 300

/* Ball speed grades from the GDD. */
#define BALL_SPEED_SLOW     0
#define BALL_SPEED_MEDIUM   1
#define BALL_SPEED_FAST     2

typedef struct {
    uint8_t  balls;     /* number of balls, 1..5 */
    uint8_t  speed;     /* BALL_SPEED_* */
    uint8_t  spiders;   /* number of spiders, 0..2 */
    uint16_t time;      /* level time limit, seconds */
} level_t;

extern const level_t levels[LEVEL_COUNT];

/* Fractional part of the ball speed, in 1/5 pixel per frame. */
uint8_t level_speed_frac(uint8_t speed);

/* Score awarded for finishing a level with the given captured percentage. */
uint16_t level_score(uint8_t percent);

/*
 * Parse the whole ZOS parameter string (argc/argv[0] is one space-separated
 * string on Zeal 8-bit OS).  Supports "--level N" and "--level=N".
 * Returns 0 when no valid level was requested, else 1..LEVEL_COUNT.
 */
uint8_t level_parse_arg(const char* arg);

#endif
