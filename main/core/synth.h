/* A small polyphonic synth and a sample-accurate metronome.
 *
 * Notes are queued from the LVGL thread and consumed by whoever calls
 * synth_render() (the watch's audio task), so no locks are needed.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SYNTH_RATE 22050

enum { SYNTH_LEAD, SYNTH_PAD, SYNTH_BASS, SYNTH_PLUCK, SYNTH_KEYS, SYNTH_PRESETS };
extern const char *const SYNTH_PRESET_NAMES[SYNTH_PRESETS];

void synth_preset(int preset);
void synth_note_on(int note, int velocity); /* MIDI note number, velocity 1..127 */
void synth_note_off(int note);
void synth_all_off(void);
void synth_cutoff(float amount);            /* 0 dark .. 1 bright */

/* Metronome: bpm 0 stops it. The first beat of each bar is accented. */
void synth_metronome(int bpm, int beats_per_bar);
uint32_t synth_beat(void);                  /* beats played since it started */

/* Fills `frames` mono samples. Call from one thread only. */
void synth_render(int16_t *out, int frames);
