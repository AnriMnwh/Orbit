/* App registry. The launcher lists these alphabetically, as Wear OS does,
 * with recently used apps on top.
 *
 *   Clock          Alarm, Stopwatch, Timer, Focus, World Clock, Calendar
 *   Health         Fitness, Workout, Water, Relax
 *   Music          Music, Synth, Metronome, Tuner
 *   Photography    Camera, Photos, Sun Times, Exposure, Moon
 *   Tools          Calculator, Counter, Dice, Flashlight, Level, Find Phone
 *   Connected      Assistant, Phone, Agenda, Keys, Presenter, Maps, Weather
 *   Fun            Pet, Reflex, Marble, Fluid, Vision, ZZZ
 */
#include <string.h>

#include "../ui/ui.h"

extern const app_t app_agenda, app_agent, app_alarm, app_calc, app_camera, app_counter, app_dice, app_exposure, app_findphone;
extern const app_t app_fitness, app_torch, app_focus, app_keys, app_level, app_maps, app_metronome, app_music;
extern const app_t app_pet, app_phone, app_photos, app_presenter, app_reflex, app_relax, app_settings;
extern const app_t app_stopwatch, app_sun, app_synth, app_timer, app_tuner, app_water, app_weather, app_workout;
extern const app_t app_fluid, app_marble, app_month, app_moon, app_vision, app_world, app_zzz;

const app_t *const APPS[] = {
    &app_agenda,    &app_alarm,    &app_agent,     &app_calc,      &app_month,     &app_camera,  &app_counter,
    &app_dice,      &app_exposure, &app_findphone, &app_fitness,   &app_torch,     &app_fluid,   &app_focus,
    &app_keys,      &app_level,    &app_maps,      &app_marble,    &app_metronome, &app_moon,    &app_music,
    &app_pet,       &app_phone,    &app_photos,    &app_presenter, &app_reflex,    &app_relax,   &app_settings,
    &app_stopwatch, &app_sun,      &app_synth,     &app_timer,     &app_tuner,     &app_vision,  &app_water,
    &app_weather,   &app_workout,  &app_world,     &app_zzz,
};
const int APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);

/* Reachable by id, but not listed in the launcher. */
static const app_t *const HIDDEN[] = {&app_launcher, &app_faces, &app_welcome};

const app_t *app_find(const char *id)
{
    for (int i = 0; i < APP_COUNT; i++) {
        if (strcmp(APPS[i]->id, id) == 0) return APPS[i];
    }
    for (size_t i = 0; i < sizeof(HIDDEN) / sizeof(HIDDEN[0]); i++) {
        if (strcmp(HIDDEN[i]->id, id) == 0) return HIDDEN[i];
    }
    return NULL;
}
