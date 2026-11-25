/**
 * @file main.c
 * Simulateur PC : LVGL v9 + SDL2.
 * Seule cette partie depend de la cible ; l'interface est dans src/smart_wash.c.
 */
#define _DEFAULT_SOURCE
#define SDL_MAIN_HANDLED
#include <unistd.h>

#include "lvgl/lvgl.h"
#include "hal/hal.h"
#include "smart_wash.h"

#define SCREEN_W 800
#define SCREEN_H 480

int main(void)
{
    lv_init();
    sdl_hal_init(SCREEN_W, SCREEN_H);   /* Couche materielle (PC : SDL2) */

    smart_wash_start();                 /* Interface, identique sur ESP32 */

    while (1)
    {
        uint32_t delay = lv_timer_handler();
        if (delay == LV_NO_TIMER_READY || delay > 20)
            delay = 20;
        usleep(delay * 1000);
    }
    return 0;
}
