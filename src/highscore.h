#ifndef XONIX_HIGHSCORE_H
#define XONIX_HIGHSCORE_H

#include <stdint.h>
#include <zvb_gfx.h>

#define HIGHSCORE_MAX 10
#define HIGHSCORE_NAME_LEN 8
#define HIGHSCORE_FILE "HISCORE.DAT"

/*
 * Show the end-of-game high-score flow: load HISCORE.DAT from the current
 * directory, ask for the player's name when the score qualifies for the top
 * ten, save the updated table and display it.  The function blocks until a key
 * is pressed or 25 seconds elapse.  The caller is responsible for restarting
 * the game afterwards.
 *
 * The table holds at most HIGHSCORE_MAX records.  A name is at most
 * HIGHSCORE_NAME_LEN characters (A-Z, 0-9 and space).  The on-disk format is
 * protected by an "XHS1" magic, XOR obfuscation and a 16-bit checksum.
 */
void highscore_game_over(gfx_context* ctx, uint16_t score);

#endif
