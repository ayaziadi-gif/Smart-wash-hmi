/**
 * @file main.c
 * Cible ESP32-S3 (ESP-IDF + BSP esp32_s3_lcd_ev_board, LVGL v9).
 *
 * Le BSP / esp_lvgl_port fournit la couche basse : ecran RGB 800x480,
 * tactile, tick LVGL et tache FreeRTOS qui appelle lv_timer_handler().
 * Ce fichier remplace donc simplement lv_init() + sdl_hal_init() du simulateur.
 */
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "smart_wash.h"

static const char *TAG = "smart_wash";

void app_main(void)
{
    /* Ecran + tactile + tache LVGL */
    bsp_display_start();
    bsp_display_backlight_on();

    /* LVGL n'est pas thread-safe : verrou de la tache d'affichage */
    bsp_display_lock(0);
    smart_wash_start();          /* meme interface que sur PC */
    bsp_display_unlock();

    ESP_LOGI(TAG, "Smart Wash HMI demarree");
}
