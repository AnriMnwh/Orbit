/* Shared between watchface.c and face_picker.c; nothing else includes this. */
#pragma once

#include "ui.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *hours, *minutes, *time, *date;
    lv_obj_t *comp_box[2], *comp_arc[2], *comp_text[2], *comp_icon[2];
    lv_obj_t *mod_title, *mod_sub;
    lv_obj_t *status, *status_icon, *status_lbl;
    lv_obj_t *analog, *charge_arc, *charge_text;
    uint8_t face;
    uint8_t style;
    bool ambient;
    bool edit_comps;
    int photo_index;
    const app_t *last_ongoing_app;
    bool has_ongoing;
} face_t;

/* True while a face is built only to be looked at (picker and edit previews):
 * live widgets such as the status row are then left out. */
extern bool wf_previewing;

/* Builds `face` into f/root; wf_build uses the saved style for that face. */
void wf_build(face_t *f, lv_obj_t *root, uint8_t face, bool ambient);
void wf_build_opts(face_t *f, lv_obj_t *root, uint8_t face, uint8_t style, bool ambient, bool edit_comps);
int wf_visible_face_count(void);
int wf_photo_index(void); /* which photo the live face is showing */
