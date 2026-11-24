/* What Settings and the first-boot setup (app_wizard.c) share. */
#pragma once

#include "../ui/ui.h"

typedef void (*page_fn)(void);

/* From app_settings.c. */
lv_obj_t *st_root(void);          /* the screen the current page fills */
void st_run_page(page_fn page);   /* replace the page stack with this one page */
void st_refresh(void);            /* rebuild the top page */
const char *st_pick_ssid(void);   /* the network being joined */
lv_obj_t *st_free_title(const char *text, int y);
lv_obj_t *st_free_text(const char *text, const lv_font_t *font, lv_color_t color, int y);
void st_zone_controls(int y);
void st_toggle(lv_event_t *e);
void st_open_page(lv_event_t *e);
void st_choose_network(lv_event_t *e);
void st_phone_page(void);

/* From app_wizard.c. */
void wizard_go(int step);
void wizard_reset(void);          /* not in setup */
bool wizard_back(void);           /* step back; false on the first step or outside setup */
