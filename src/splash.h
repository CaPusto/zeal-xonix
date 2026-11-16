#ifndef SPLASH_H
#define SPLASH_H

#include <stdint.h>
#include <zvb_gfx.h>

/*
 * Check for assets/splash.ztp, assets/splash.zts and assets/splash.ztm in the
 * current directory.  Prints a warning for every missing file and returns 0
 * when the splash cannot be shown.
 */
uint8_t splash_check(void);

/*
 * Load and display the splash, fade it in, wait for a key or 25 seconds and
 * fade it out.  The screen is left black/disabled by the caller afterwards.
 * Returns 1 on success, 0 when a file could not be read.
 */
uint8_t splash_show(gfx_context* ctx);

#endif
