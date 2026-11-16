#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <zvb_gfx.h>

void init(uint8_t start_level, uint8_t want_splash);
void deinit(void);
uint16_t input(void);

extern gfx_context vctx;

#endif
