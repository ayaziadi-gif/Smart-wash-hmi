/**
 * @file smart_wash.h
 * Point d'entree de l'IHM Smart Wash, identique sur PC (SDL2) et ESP32.
 */
#ifndef SMART_WASH_H
#define SMART_WASH_H

#ifdef __cplusplus
extern "C" {
#endif

/** Construit l'interface et lance les minuteurs LVGL.
 *  A appeler une seule fois, apres lv_init() et l'initialisation des pilotes. */
void smart_wash_start(void);

#ifdef __cplusplus
}
#endif

#endif
