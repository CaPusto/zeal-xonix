#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <zvb_gfx.h>

void game_init(gfx_context* ctx, uint8_t start_level);
void game_update(gfx_context* ctx, uint16_t keys);
void game_shutdown(gfx_context* ctx);

/* Set when the player ran out of lives and the high-score table was shown. */
uint8_t game_restart_requested(void);

#endif
