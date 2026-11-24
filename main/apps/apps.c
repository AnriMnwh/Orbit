/* App registry. The launcher lists these alphabetically, as Wear OS does,
 * with recently used apps on top.
 *
 *   Clock          Alarm, Stopwatch, Timer, World Clock, Calendar
 *   Health         Fitness, Workout
 *   Music          Music
 *   Photography    Camera, Photos, Sun Times, Exposure, Moon
 *   Tools          Calculator, Flashlight, Find Phone
 *   Connected      Assistant, Phone, Agenda, Keys, Presenter, Maps, Weather
 *   Fun            Pet
 */
#include <string.h>

#include "../ui/ui.h"

extern const app_t app_agenda, app_agent, app_alarm, app_calc, app_camera, app_exposure, app_findphone;
extern const app_t app_fitness, app_torch, app_keys, app_maps, app_music;
extern const app_t app_pet, app_phone, app_photos, app_presenter, app_settings;
extern const app_t app_stopwatch, app_sun, app_timer, app_weather, app_workout;
extern const app_t app_month, app_moon, app_world;

const app_t *const APPS[] = {
    &app_agenda,    &app_alarm,    &app_agent,     &app_calc,      &app_month,  &app_camera,  &app_exposure,
    &app_findphone, &app_fitness,  &app_torch,     &app_keys,      &app_maps,   &app_moon,    &app_music,
    &app_pet,       &app_phone,    &app_photos,    &app_presenter, &app_settings, &app_stopwatch, &app_sun,
    &app_timer,     &app_weather,  &app_workout,   &app_world,
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
