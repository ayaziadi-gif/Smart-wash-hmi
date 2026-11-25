/**
 * @file hal.h
 * Couche materielle du simulateur PC (comme dans lv_port_pc_vscode).
 */
#ifndef HAL_H
#define HAL_H

#include "lvgl/lvgl.h"

/** Cree la fenetre SDL2 (ecran) et la souris (tactile simule). */
lv_display_t *sdl_hal_init(int32_t w, int32_t h);

#endif
