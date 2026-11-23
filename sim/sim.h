#pragma once

#include "../main/ui/ui.h"

extern uint32_t sim_now_ms;
extern const char *sim_data_dir;

void sim_run(uint32_t ms);
void sim_tap(int x, int y);
void sim_swipe(int x0, int y0, int x1, int y1, uint32_t ms);
void sim_services_init(void);
