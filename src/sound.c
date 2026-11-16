#include <stdint.h>
#include <zvb_sound.h>

#include "sound.h"

/*
 * Four hardware voices:
 *   VOICE0 - ball bounce
 *   VOICE1 - captured-territory bell
 *   VOICE2 - continuous spider buzz
 *   VOICE3 - game-over / victory jingle
 */
#define SND_BOUNCE      VOICE0
#define SND_CAPTURE     VOICE1
#define SND_BUZZ        VOICE2
#define SND_JINGLE      VOICE3
#define SND_ALL         (sound_voice_t)(VOICE0 | VOICE1 | VOICE2 | VOICE3)
#define SND_GAMEPLAY    (sound_voice_t)(VOICE0 | VOICE1 | VOICE2)

#define SND_SQUARE      (sound_waveform_t)(WAV_SQUARE | DUTY_CYCLE_12_5)
#define SND_FANFARE     (sound_waveform_t)(WAV_SQUARE | DUTY_CYCLE_50_0)

typedef struct {
    uint16_t divider;
    uint8_t frames;
} sound_note_t;

/* Short "uh-oh" for losing one life, a longer "oops" for game over, and a
 * C-major victory fanfare. */
static const sound_note_t life_notes[] = {
    { SOUND_FREQ_TO_DIV(440), 5 },   /* A4 */
    { SOUND_FREQ_TO_DIV(349), 5 },   /* F4 */
    { SOUND_FREQ_TO_DIV(262), 14 },  /* C4 */
};

static const sound_note_t sad_notes[] = {
    { SOUND_FREQ_TO_DIV(330), 12 },  /* E4 */
    { SOUND_FREQ_TO_DIV(294), 12 },  /* D4 */
    { SOUND_FREQ_TO_DIV(262), 24 },  /* C4 */
};

static const sound_note_t win_notes[] = {
    { SOUND_FREQ_TO_DIV(523),  8 },  /* C5 */
    { SOUND_FREQ_TO_DIV(659),  8 },  /* E5 */
    { SOUND_FREQ_TO_DIV(784),  8 },  /* G5 */
    { SOUND_FREQ_TO_DIV(1047), 12 }, /* C6 */
    { SOUND_FREQ_TO_DIV(784),  8 },  /* G5 */
    { SOUND_FREQ_TO_DIV(1047), 24 }, /* C6 */
};

static const sound_note_t* melody;
static uint8_t melody_length;
static uint8_t melody_position;
static uint8_t melody_frames;
static uint8_t melody_wave;

static uint8_t bounce_timer;
static uint8_t bounce_alt;
static uint8_t capture_timer;
static uint8_t capture_stage;
static uint8_t buzz_on;
static uint8_t buzz_phase;
static uint8_t saved_page;

static void sound_begin(void) {
    saved_page = zvb_config_dev_idx;
}

static void sound_end(void) {
    zvb_config_dev_idx = saved_page;
}

static void sound_melody_next(void) {
    const sound_note_t* note;

    if(melody == 0 || melody_position >= melody_length) {
        melody = 0;
        zvb_sound_set_hold(SND_JINGLE, 1);
        return;
    }

    note = &melody[melody_position++];
    zvb_sound_set_voices(SND_JINGLE, note->divider, melody_wave);
    melody_frames = note->frames;
}

static void sound_melody_start(const sound_note_t* notes, uint8_t length,
                               uint8_t waveform, sound_volume_t volume) {
    melody = notes;
    melody_length = length;
    melody_position = 0;
    melody_frames = 0;
    melody_wave = waveform;

    zvb_sound_set_voices_vol(SND_JINGLE, volume);
    zvb_sound_set_hold(SND_JINGLE, 1);
    sound_melody_next();
}

void xsfx_init(void) {
    sound_begin();
    zvb_sound_initialize(1);
    zvb_sound_set_channels(SND_ALL, SND_ALL);
    zvb_sound_set_volume(VOL_100);

    zvb_sound_set_voices_vol(SND_BOUNCE, VOL_50);
    zvb_sound_set_voices_vol(SND_CAPTURE, VOL_75);
    zvb_sound_set_voices_vol(SND_BUZZ, VOL_25);
    zvb_sound_set_voices_vol(SND_JINGLE, VOL_75);

    zvb_sound_set_hold(SND_ALL, 1);
    bounce_timer = 0;
    capture_timer = 0;
    capture_stage = 0;
    buzz_on = 0;
    melody = 0;
    melody_frames = 0;
    sound_end();
}

void xsfx_update(void) {
    if(bounce_timer != 0) {
        bounce_timer--;
        if(bounce_timer == 0) {
            zvb_sound_set_hold(SND_BOUNCE, 1);
        }
    }

    if(capture_timer != 0) {
        capture_timer--;
        if(capture_timer == 0) {
            if(capture_stage == 0) {
                capture_stage = 1;
                capture_timer = 10;
                zvb_sound_set_voices(SND_CAPTURE,
                                     SOUND_FREQ_TO_DIV(1047), WAV_TRIANGLE);
            } else {
                zvb_sound_set_hold(SND_CAPTURE, 1);
            }
        }
    }

    if(buzz_on != 0) {
        buzz_phase++;
        if((buzz_phase & 15) == 0) {
            const uint16_t divider = (buzz_phase & 16)
                                   ? SOUND_FREQ_TO_DIV(105)
                                   : SOUND_FREQ_TO_DIV(85);

            zvb_sound_set_voices(SND_BUZZ, divider, WAV_SAWTOOTH);
        }
    }

    if(melody != 0 && melody_frames != 0) {
        melody_frames--;
        if(melody_frames == 0) {
            sound_melody_next();
        }
    }
    sound_end();
}

void xsfx_stop_gameplay(void) {
    sound_begin();
    zvb_sound_set_hold(SND_GAMEPLAY, 1);

    bounce_timer = 0;
    capture_timer = 0;
    capture_stage = 0;
    buzz_on = 0;
    sound_end();
}

void xsfx_stop_all(void) {
    sound_begin();
    zvb_sound_set_hold(SND_ALL, 1);

    bounce_timer = 0;
    capture_timer = 0;
    capture_stage = 0;
    buzz_on = 0;
    melody = 0;
    melody_frames = 0;
    sound_end();
}

void xsfx_ball_bounce(void) {
    const uint16_t divider = bounce_alt
                           ? SOUND_FREQ_TO_DIV(1200)
                           : SOUND_FREQ_TO_DIV(1500);

    bounce_alt ^= 1;
    sound_begin();
    zvb_sound_set_voices(SND_BOUNCE, divider, SND_SQUARE);
    sound_end();
    bounce_timer = 3;
}

void xsfx_capture(void) {
    capture_stage = 0;
    capture_timer = 4;
    sound_begin();
    zvb_sound_set_voices(SND_CAPTURE, SOUND_FREQ_TO_DIV(1319), WAV_TRIANGLE);
    sound_end();
}

void xsfx_buzz_start(void) {
    buzz_on = 1;
    buzz_phase = 0;
    sound_begin();
    zvb_sound_set_voices(SND_BUZZ, SOUND_FREQ_TO_DIV(85), WAV_SAWTOOTH);
    sound_end();
}

void xsfx_buzz_stop(void) {
    if(buzz_on != 0) {
        buzz_on = 0;
        sound_begin();
        zvb_sound_set_hold(SND_BUZZ, 1);
        sound_end();
    }
}

void xsfx_life_lost(void) {
    sound_begin();
    sound_melody_start(life_notes, sizeof(life_notes) / sizeof(life_notes[0]),
                       SND_FANFARE, VOL_50);
    sound_end();
}

void xsfx_game_over(void) {
    sound_begin();
    sound_melody_start(sad_notes, sizeof(sad_notes) / sizeof(sad_notes[0]),
                       WAV_TRIANGLE, VOL_75);
    sound_end();
}

void xsfx_victory(void) {
    sound_begin();
    sound_melody_start(win_notes, sizeof(win_notes) / sizeof(win_notes[0]),
                       SND_FANFARE, VOL_75);
    sound_end();
}
