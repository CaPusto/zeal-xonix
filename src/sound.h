#ifndef XONIX_SOUND_H
#define XONIX_SOUND_H

#include <stdint.h>

void xsfx_init(void);
void xsfx_update(void);

/* Stop gameplay effects (bounce, capture bell and spider buzz). */
void xsfx_stop_gameplay(void);
/* Stop every voice, including a high-score jingle. */
void xsfx_stop_all(void);

void xsfx_ball_bounce(void);
void xsfx_capture(void);
void xsfx_buzz_start(void);
void xsfx_buzz_stop(void);

void xsfx_life_lost(void);
void xsfx_game_over(void);
void xsfx_victory(void);

#endif
