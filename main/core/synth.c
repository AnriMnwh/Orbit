#include <math.h>
#include <string.h>

#include "synth.h"

#define VOICES 6
#define QUEUE 32
#define TWO_PI 6.2831853f

const char *const SYNTH_PRESET_NAMES[SYNTH_PRESETS] = {"Lead", "Pad", "Bass", "Pluck", "Keys"};

enum { W_SAW, W_SQUARE, W_SINE, W_TRIANGLE };
enum { ENV_ATTACK, ENV_DECAY, ENV_RELEASE, ENV_OFF };

typedef struct {
    uint8_t wave;
    float detune;          /* second oscillator, as a frequency ratio (0 = none) */
    float attack, decay;   /* seconds */
    float sustain;         /* 0..1 */
    float release;         /* seconds */
    float cutoff;          /* base filter opening 0..1 */
    float sweep;           /* how far the envelope opens the filter */
    float gain;
    int octave;            /* shift applied to incoming notes */
} preset_t;

static const preset_t PRESETS[SYNTH_PRESETS] = {
    [SYNTH_LEAD] = {W_SAW, 1.006f, 0.005f, 0.20f, 0.70f, 0.18f, 0.45f, 0.40f, 0.34f, 0},
    [SYNTH_PAD] = {W_SAW, 0.994f, 0.35f, 0.60f, 0.85f, 0.90f, 0.30f, 0.25f, 0.28f, 0},
    [SYNTH_BASS] = {W_SQUARE, 0.0f, 0.004f, 0.18f, 0.55f, 0.12f, 0.25f, 0.50f, 0.42f, -1},
    [SYNTH_PLUCK] = {W_TRIANGLE, 2.0f, 0.002f, 0.22f, 0.0f, 0.20f, 0.55f, 0.45f, 0.44f, 0},
    [SYNTH_KEYS] = {W_SINE, 2.0f, 0.004f, 0.50f, 0.35f, 0.30f, 0.90f, 0.0f, 0.40f, 0},
};

typedef struct {
    int note; /* -1 when free */
    uint8_t stage;
    float phase, phase2, inc;
    float env, velocity;
    float lp1, lp2;
    uint32_t started;
} voice_t;

typedef struct {
    uint8_t kind; /* 0 note on, 1 note off, 2 all off */
    uint8_t note, velocity;
} cmd_t;

static voice_t voices[VOICES];
static cmd_t queue[QUEUE];
static volatile uint32_t q_write, q_read;
static volatile int preset_index;
static volatile float cutoff_knob = 0.5f;
static uint32_t clock_frames;

/* Metronome state. `met_gap` is frames between beats; 0 means stopped. */
static volatile uint32_t met_gap, met_bar = 4, met_beats;
static uint32_t met_wait;
static float click_env, click_phase, click_inc;

static void push(cmd_t c)
{
    uint32_t w = q_write;
    if (w - q_read >= QUEUE) return; /* full: drop rather than block the UI */
    queue[w % QUEUE] = c;
    q_write = w + 1;
}

void synth_preset(int preset)
{
    if (preset >= 0 && preset < SYNTH_PRESETS) preset_index = preset;
}

void synth_note_on(int note, int velocity)
{
    if (note < 0 || note > 127) return;
    push((cmd_t){0, (uint8_t)note, (uint8_t)(velocity < 1 ? 1 : (velocity > 127 ? 127 : velocity))});
}

void synth_note_off(int note)
{
    if (note >= 0 && note <= 127) push((cmd_t){1, (uint8_t)note, 0});
}

void synth_all_off(void)
{
    push((cmd_t){2, 0, 0});
}

void synth_cutoff(float amount)
{
    cutoff_knob = amount < 0 ? 0 : (amount > 1 ? 1 : amount);
}

void synth_metronome(int bpm, int beats_per_bar)
{
    met_bar = beats_per_bar > 0 ? (uint32_t)beats_per_bar : 4;
    if (bpm <= 0) {
        met_gap = 0;
        return;
    }
    if (!met_gap) met_beats = 0, met_wait = 0;
    met_gap = (uint32_t)(SYNTH_RATE * 60 / bpm);
}

uint32_t synth_beat(void)
{
    return met_beats;
}

static void start_voice(int note, int velocity)
{
    const preset_t *p = &PRESETS[preset_index];
    /* Reuse the same note, else a free voice, else steal the oldest. */
    voice_t *v = NULL;
    for (int i = 0; i < VOICES; i++) {
        if (voices[i].note == note && voices[i].stage != ENV_OFF) v = &voices[i];
    }
    for (int i = 0; i < VOICES && !v; i++) {
        if (voices[i].stage == ENV_OFF) v = &voices[i];
    }
    if (!v) {
        v = &voices[0];
        for (int i = 1; i < VOICES; i++) {
            if (voices[i].started < v->started) v = &voices[i];
        }
    }
    float hz = 440.0f * powf(2.0f, (note + p->octave * 12 - 69) / 12.0f);
    v->note = note;
    v->stage = ENV_ATTACK;
    v->inc = hz / SYNTH_RATE;
    v->velocity = velocity / 127.0f;
    v->started = clock_frames;
    if (v->env < 0.001f) v->phase = v->phase2 = 0, v->lp1 = v->lp2 = 0;
}

static void drain(void)
{
    while (q_read != q_write) {
        cmd_t c = queue[q_read % QUEUE];
        q_read = q_read + 1;
        if (c.kind == 0) {
            start_voice(c.note, c.velocity);
        } else {
            for (int i = 0; i < VOICES; i++) {
                if ((c.kind == 2 || voices[i].note == c.note) && voices[i].stage < ENV_RELEASE) voices[i].stage = ENV_RELEASE;
            }
        }
    }
}

/* Smooths the step in a saw or square wave so it does not alias harshly. */
static float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static float osc(uint8_t wave, float t, float dt)
{
    switch (wave) {
    case W_SAW: return 2.0f * t - 1.0f - blep(t, dt);
    case W_SQUARE: {
        float t2 = t + 0.5f;
        if (t2 >= 1.0f) t2 -= 1.0f;
        return (t < 0.5f ? 1.0f : -1.0f) + blep(t, dt) - blep(t2, dt);
    }
    case W_TRIANGLE: return 4.0f * fabsf(t - 0.5f) - 1.0f;
    default: return sinf(TWO_PI * t);
    }
}

void synth_render(int16_t *out, int frames)
{
    static bool ready;
    if (!ready) {
        for (int i = 0; i < VOICES; i++) voices[i].note = -1, voices[i].stage = ENV_OFF;
        ready = true;
    }
    drain();
    const preset_t *p = &PRESETS[preset_index];
    float attack = 1.0f / (p->attack * SYNTH_RATE + 1.0f);
    float decay = 1.0f / (p->decay * SYNTH_RATE + 1.0f);
    float release = 1.0f / (p->release * SYNTH_RATE + 1.0f);
    float knob = cutoff_knob;

    for (int n = 0; n < frames; n++) {
        float mix = 0;
        for (int i = 0; i < VOICES; i++) {
            voice_t *v = &voices[i];
            if (v->stage == ENV_OFF) continue;

            if (v->stage == ENV_ATTACK) {
                v->env += attack;
                if (v->env >= 1.0f) v->env = 1.0f, v->stage = ENV_DECAY;
            } else if (v->stage == ENV_DECAY) {
                v->env += (p->sustain - v->env) * decay;
                if (p->sustain <= 0.0f && v->env < 0.002f) v->stage = ENV_OFF;
            } else {
                v->env -= v->env * release + 0.00002f;
                if (v->env <= 0.0f) v->env = 0, v->stage = ENV_OFF, v->note = -1;
            }

            float s = osc(p->wave, v->phase, v->inc);
            v->phase += v->inc;
            if (v->phase >= 1.0f) v->phase -= 1.0f;
            if (p->detune > 0.0f) {
                float inc2 = v->inc * p->detune;
                s = 0.6f * s + 0.4f * osc(p->wave, v->phase2, inc2);
                v->phase2 += inc2;
                if (v->phase2 >= 1.0f) v->phase2 -= 1.0f;
            }

            /* Two one-pole low-pass stages; the envelope and the tilt knob open them. */
            float open = p->cutoff * (0.35f + 1.3f * knob) + p->sweep * v->env;
            float k = open * open;
            if (k > 0.95f) k = 0.95f;
            if (k < 0.002f) k = 0.002f;
            v->lp1 += (s - v->lp1) * k;
            v->lp2 += (v->lp1 - v->lp2) * k;
            mix += v->lp2 * v->env * v->velocity * p->gain;
        }

        /* Metronome click: a short pitched blip, higher on the first beat. */
        uint32_t gap = met_gap;
        if (gap) {
            if (met_wait == 0) {
                bool accent = met_beats % met_bar == 0;
                click_env = accent ? 0.9f : 0.6f;
                click_inc = (accent ? 2100.0f : 1400.0f) / SYNTH_RATE;
                click_phase = 0;
                met_beats = met_beats + 1;
                met_wait = gap;
            }
            met_wait--;
        }
        if (click_env > 0.001f) {
            mix += sinf(TWO_PI * click_phase) * click_env;
            click_phase += click_inc;
            if (click_phase >= 1.0f) click_phase -= 1.0f;
            click_env *= 0.9965f;
        }

        /* Soft clip, so stacked notes saturate instead of crackling. */
        mix = mix / (1.0f + fabsf(mix));
        out[n] = (int16_t)(mix * 30000.0f);
        clock_frames++;
    }
}
