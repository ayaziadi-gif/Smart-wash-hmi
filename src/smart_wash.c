/* ============================================================================
 *  SMART WASH — Industrial HMI
 *  Interface de supervision d'une machine de lavage (LVGL v9)
 *
 *  Organisation du fichier
 *    1. Configuration, palette et typographie
 *    2. Données (machine, alarmes, historique, journal)
 *    3. Briques graphiques réutilisables (carte, jauge, puce, bouton, toast)
 *    4. Structure de l'écran (menu latéral, bandeau, zone de contenu)
 *    5. Écrans : Accueil, Mesures, Historique, Paramètres, Alarmes,
 *                Manuel, Système
 *    6. Authentification par code PIN (fenêtre modale)
 *    7. Logique : simulation, alarmes, cycles, journal
 *    8. Programme principal
 *
 *  Ce fichier ne dépend d'aucune cible : l'initialisation de LVGL et des
 *  pilotes (SDL2 sur PC, BSP sur ESP32) est faite dans platform/pc_sdl/main.c
 *  et esp32/main/main.c, qui appellent ensuite smart_wash_start().
 * ========================================================================== */


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "lvgl/lvgl.h"
#include "smart_wash.h"
#include "machine.h"

/* ============================================================================
 * 1. CONFIGURATION
 * ========================================================================== */
#define SCREEN_W            800
#define SCREEN_H            480
#define SIDEBAR_W           176
#define HEADER_H            56

#define ADMIN_PIN           "2002"
#define PIN_MAX_LEN         6
#define MAX_PIN_ATTEMPTS    3
#define PIN_LOCK_SECONDS    30

#define MAX_HISTORY         20
#define MAX_AUDIT           20
#define MAX_ALARMS          8

#define SIM_PERIOD_MS       250         /* Rafraîchissement des données */
#define CLOCK_PERIOD_MS     1000

/* Seuils d'alarme */
#define TEMP_MAX_C          80
#define PRESSURE_MAX_X10    60          /* 6,0 bar */
#define LEVEL_MIN_PCT       20
#define LEVEL_MAX_PCT       95

/* ---------------------------------------------------------------------------
 * Palette (style « IHM haute performance » : fond neutre, couleur réservée
 * aux états et aux actions)
 * ------------------------------------------------------------------------- */
#define C_SIDEBAR       lv_color_hex(0x15191E)
#define C_SIDEBAR_HOVER lv_color_hex(0x232A32)
#define C_HEADER        lv_color_hex(0xFFFFFF)
#define C_BG            lv_color_hex(0xEEF0F2)
#define C_CARD          lv_color_hex(0xFFFFFF)
#define C_BORDER        lv_color_hex(0xE1E4E8)
#define C_TEXT          lv_color_hex(0x1B2128)
#define C_TEXT_MUTED    lv_color_hex(0x6B7480)
#define C_TEXT_INV      lv_color_hex(0xF3F4F6)
#define C_TRACK         lv_color_hex(0xE7E9EC)
#define C_ACCENT        lv_color_hex(0xF2B705)
#define C_ACCENT_DARK   lv_color_hex(0x8A6A00)
#define C_OK            lv_color_hex(0x2E9E5B)
#define C_OK_BG         lv_color_hex(0xE4F4EA)
#define C_WARN          lv_color_hex(0xE08A00)
#define C_WARN_BG       lv_color_hex(0xFFF1DB)
#define C_ALARM         lv_color_hex(0xD64545)
#define C_ALARM_BG      lv_color_hex(0xFCE8E8)
#define C_INFO_BG       lv_color_hex(0xEEF1F5)

/* ---------------------------------------------------------------------------
 * Typographie : on utilise les tailles Montserrat activées dans lv_conf.h,
 * avec repli automatique sur la police par défaut.
 * ------------------------------------------------------------------------- */
#if LV_FONT_MONTSERRAT_12
#define FONT_XS (&lv_font_montserrat_12)
#else
#define FONT_XS LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_14
#define FONT_SM (&lv_font_montserrat_14)
#else
#define FONT_SM LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_16
#define FONT_MD (&lv_font_montserrat_16)
#else
#define FONT_MD LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_20
#define FONT_LG (&lv_font_montserrat_20)
#else
#define FONT_LG FONT_MD
#endif
#if LV_FONT_MONTSERRAT_28
#define FONT_XL (&lv_font_montserrat_28)
#else
#define FONT_XL FONT_LG
#endif

/* ============================================================================
 * 2. DONNÉES
 * ========================================================================== */
typedef enum
{
    PAGE_HOME = 0,
    PAGE_MESURES,
    PAGE_HISTORIQUE,
    PAGE_PARAMETRES,
    PAGE_ALARMES,
    PAGE_MANUEL,
    PAGE_SYSTEME,
    PAGE_COUNT
} page_id_t;

typedef struct
{
    const char *icon;
    const char *label;
    const char *title;
    const char *subtitle;
    bool        admin_only;
} page_info_t;

static const page_info_t PAGES[PAGE_COUNT] =
{
    { LV_SYMBOL_HOME,     "Accueil",    "Accueil",    "Vue d'ensemble de la machine",          false },
    { LV_SYMBOL_TINT,     "Mesures",    "Mesures",    "Grandeurs du procede en temps reel",    false },
    { LV_SYMBOL_LIST,     "Historique", "Historique", "Derniers cycles et journal de securite", false },
    { LV_SYMBOL_SETTINGS, "Parametres", "Parametres", "Consignes de fonctionnement",           true  },
    { LV_SYMBOL_BELL,     "Alarmes",    "Alarmes",    "Alarmes actives et acquittement",       false },
    { LV_SYMBOL_POWER,    "Manuel",     "Manuel",     "Pilotage direct hors cycle",            true  },
    { LV_SYMBOL_DRIVE,    "Systeme",    "Systeme",    "Etat et limites du prototype",          false },
};

/* --- Alarmes (cycle de vie ISA-18.2 : active -> acquittée -> résolue) ---- */
typedef enum { ALARM_INFO = 0, ALARM_WARNING, ALARM_CRITICAL } alarm_level_t;
typedef enum { ALARM_RESOLVED = 0, ALARM_ACTIVE, ALARM_ACKNOWLEDGED } alarm_state_t;

typedef struct
{
    char          code[12];
    char          name[40];
    char          detail[80];
    alarm_level_t level;
    alarm_state_t state;
    time_t        timestamp;
} alarm_t;

/* --- Historique des cycles ------------------------------------------------ */
typedef struct
{
    char date[11];
    char heure[9];
    char duree[16];
    char cause[32];
    bool ok;
} history_entry_t;

/* --- Journal de sécurité -------------------------------------------------- */
typedef struct
{
    char date[11];
    char heure[9];
    char action[24];
    char utilisateur[20];
    char detail[64];
} audit_entry_t;

/* --- État global ---------------------------------------------------------- */
static bool            admin_mode = false;
static page_id_t       current_page = PAGE_HOME;
static page_id_t       pending_page = PAGE_HOME;   /* Écran demandé avant PIN */

static int             temperature_value = 20;     /* °C                      */
static int             pressure_x10      = 10;     /* bar x 10                */
static int             pump_setpoint     = 80;     /* %                       */
static int             level_value       = 0;      /* %                       */

static alarm_t         alarms[MAX_ALARMS];
static uint32_t        alarm_signature = 0;

static history_entry_t history[MAX_HISTORY];
static int             history_count = 0;
static audit_entry_t   audit_log[MAX_AUDIT];
static int             audit_count = 0;

static bool            alarm_test_active = false;  /* Test d'alarme (écran Système) */

static machine_state_t previous_state = MACHINE_READY;
static time_t          cycle_start_time = 0;
static bool            cycle_active = false;

static char            pin_buffer[PIN_MAX_LEN + 1];
static int             pin_length = 0;
static int             pin_failed_attempts = 0;
static time_t          pin_lock_until = 0;

/* ============================================================================
 * Objets graphiques
 * ========================================================================== */
/* Structure fixe */
static lv_obj_t *nav_btn[PAGE_COUNT];
static lv_obj_t *nav_badge_alarm;
static lv_obj_t *header_title;
static lv_obj_t *header_subtitle;
static lv_obj_t *header_clock;
static lv_obj_t *access_btn;
static lv_obj_t *access_label;
static lv_obj_t *content;

/* Accueil */
static lv_obj_t *home_state_value;
static lv_obj_t *home_step_label;
static lv_obj_t *home_progress_bar;
static lv_obj_t *home_progress_pct;
static lv_obj_t *home_temp_arc;
static lv_obj_t *home_temp_value;
static lv_obj_t *home_pump_arc;
static lv_obj_t *home_pump_value;
static lv_obj_t *home_pressure_value;
static lv_obj_t *home_pressure_bar;
static lv_obj_t *home_level_value;
static lv_obj_t *home_level_bar;
static lv_obj_t *home_alarm_strip;
static lv_obj_t *home_alarm_icon;
static lv_obj_t *home_alarm_text;

/* Mesures */
static lv_obj_t *mes_arc[4];
static lv_obj_t *mes_value[4];
static lv_obj_t *mes_chip[4];

/* Paramètres */
static lv_obj_t *par_temp_value;
static lv_obj_t *par_pump_value;

/* Alarmes */
static lv_obj_t *alm_list;
static lv_obj_t *alm_count_label;
static lv_obj_t *alm_ack_btn;

/* Manuel */
static lv_obj_t *man_state_chip;

/* Système */
static lv_obj_t *sys_test_btn;

/* Fenêtre PIN */
static lv_obj_t *pin_overlay;
static lv_obj_t *pin_dots;
static lv_obj_t *pin_message;
static lv_obj_t *pin_info;
static lv_obj_t *pin_keypad;

/* Notification */
static lv_obj_t   *toast_obj;
static lv_timer_t *toast_timer;

/* ============================================================================
 * Prototypes
 * ========================================================================== */
static void show_page(page_id_t page);
static void build_home(void);
static void build_mesures(void);
static void build_historique(void);
static void build_parametres(void);
static void build_alarmes(void);
static void build_manuel(void);
static void build_systeme(void);

static void open_pin_modal(page_id_t target);
static void close_pin_modal(void);
static void update_pin_display(void);
static bool pin_is_locked(void);

static void update_access_badge(void);
static void update_live_widgets(void);
static void update_alarm_widgets(void);
static void rebuild_alarm_list(void);

static void add_audit_entry(const char *action, const char *user, const char *detail);
static void add_history_entry(bool ok, const char *cause);
static void toast_show(const char *text, lv_color_t color);

/* ============================================================================
 * Outils
 * ========================================================================== */
static const char *current_user(void)
{
    return admin_mode ? "ADMINISTRATEUR" : "OPERATEUR";
}

static void copy_str(char *dst, const char *src, size_t size)
{
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static void now_strings(char *date, size_t dsize, char *heure, size_t hsize)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t != NULL)
    {
        strftime(date, dsize, "%d/%m/%Y", t);
        strftime(heure, hsize, "%H:%M:%S", t);
    }
    else
    {
        date[0] = '\0';
        heure[0] = '\0';
    }
}

static bool state_is_running(machine_state_t s)
{
    return s == MACHINE_FILLING || s == MACHINE_HEATING || s == MACHINE_WASHING ||
           s == MACHINE_RINSING || s == MACHINE_SPINNING;
}

typedef struct { const char *text; lv_color_t color; lv_color_t bg; int step; } state_view_t;

static state_view_t state_view(machine_state_t s)
{
    state_view_t v = { "PRETE", C_OK, C_OK_BG, 0 };
    switch (s)
    {
        case MACHINE_READY:     v = (state_view_t){ "PRETE",         C_OK,    C_OK_BG,    0 }; break;
        case MACHINE_FILLING:   v = (state_view_t){ "REMPLISSAGE",   C_WARN,  C_WARN_BG,  1 }; break;
        case MACHINE_HEATING:   v = (state_view_t){ "CHAUFFAGE",     C_WARN,  C_WARN_BG,  2 }; break;
        case MACHINE_WASHING:   v = (state_view_t){ "LAVAGE",        C_WARN,  C_WARN_BG,  3 }; break;
        case MACHINE_RINSING:   v = (state_view_t){ "RINCAGE",       C_WARN,  C_WARN_BG,  4 }; break;
        case MACHINE_SPINNING:  v = (state_view_t){ "ESSORAGE",      C_WARN,  C_WARN_BG,  5 }; break;
        case MACHINE_FINISHED:  v = (state_view_t){ "TERMINE",       C_OK,    C_OK_BG,    6 }; break;
        case MACHINE_STOPPED:   v = (state_view_t){ "ARRETEE",       C_ALARM, C_ALARM_BG, 0 }; break;
        case MACHINE_EMERGENCY: v = (state_view_t){ "ARRET URGENCE", C_ALARM, C_ALARM_BG, 0 }; break;
        default: break;
    }
    return v;
}

static int cycle_progress(machine_state_t s)
{
    static const int pct[] = { 0, 15, 35, 55, 75, 90, 100 };
    state_view_t v = state_view(s);
    return pct[v.step];
}

/* ============================================================================
 * 3. BRIQUES GRAPHIQUES
 * ========================================================================== */
static lv_obj_t *box_create(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *card_create(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(c, C_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, C_BORDER, 0);
    lv_obj_set_style_shadow_width(c, 16, 0);
    lv_obj_set_style_shadow_offset_y(c, 2, 0);
    lv_obj_set_style_shadow_color(c, lv_color_hex(0x1B2128), 0);
    lv_obj_set_style_shadow_opa(c, LV_OPA_10, 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    return c;
}

static lv_obj_t *label_create(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

/* Petit titre de carte en capitales espacées */
static lv_obj_t *caption_create(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = label_create(parent, text, FONT_XS, C_TEXT_MUTED);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    return l;
}

/* Puce d'état (fond pâle + texte coloré) */
static lv_obj_t *chip_create(lv_obj_t *parent, const char *text,
                             lv_color_t fg, lv_color_t bg)
{
    lv_obj_t *l = label_create(parent, text, FONT_XS, fg);
    lv_obj_set_style_bg_color(l, bg, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(l, 10, 0);
    lv_obj_set_style_pad_ver(l, 4, 0);
    return l;
}

static void chip_set(lv_obj_t *chip, const char *text, lv_color_t fg, lv_color_t bg)
{
    if (chip == NULL) return;
    lv_label_set_text(chip, text);
    lv_obj_set_style_text_color(chip, fg, 0);
    lv_obj_set_style_bg_color(chip, bg, 0);
}

typedef enum { BTN_PRIMARY, BTN_DANGER, BTN_GHOST } btn_kind_t;

static lv_obj_t *button_create(lv_obj_t *parent, int32_t w, int32_t h,
                               const char *text, btn_kind_t kind,
                               lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_text_font(b, FONT_MD, 0);

    switch (kind)
    {
        case BTN_PRIMARY:
            lv_obj_set_style_bg_color(b, C_ACCENT, 0);
            lv_obj_set_style_text_color(b, C_TEXT, 0);
            lv_obj_set_style_border_width(b, 0, 0);
            break;
        case BTN_DANGER:
            lv_obj_set_style_bg_color(b, C_CARD, 0);
            lv_obj_set_style_text_color(b, C_ALARM, 0);
            lv_obj_set_style_border_width(b, 2, 0);
            lv_obj_set_style_border_color(b, C_ALARM, 0);
            break;
        case BTN_GHOST:
        default:
            lv_obj_set_style_bg_color(b, C_INFO_BG, 0);
            lv_obj_set_style_text_color(b, C_TEXT, 0);
            lv_obj_set_style_border_width(b, 0, 0);
            break;
    }
    /* Retour visuel à l'appui */
    lv_obj_set_style_transform_scale(b, 245, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(b, C_TRACK, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, C_TEXT_MUTED, LV_STATE_DISABLED);
    lv_obj_set_style_border_color(b, C_BORDER, LV_STATE_DISABLED);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);

    if (cb != NULL)
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);
    return b;
}

/* Jauge circulaire (arc de 270°) avec valeur au centre */
static lv_obj_t *gauge_create(lv_obj_t *parent, int32_t size, int32_t min,
                              int32_t max, lv_color_t color, lv_obj_t **value_label)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_range(arc, min, max);
    lv_arc_set_value(arc, min);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, C_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(arc, 0, 0);

    lv_obj_t *v = label_create(arc, "--", FONT_LG, C_TEXT);
    lv_obj_center(v);
    if (value_label != NULL)
        *value_label = v;
    return arc;
}

static lv_obj_t *bar_create(lv_obj_t *parent, int32_t w, lv_color_t color)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, w, 8);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, C_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    return bar;
}

/* --- Notification éphémère (toast) --------------------------------------- */
static void toast_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (toast_obj != NULL)
    {
        lv_obj_delete(toast_obj);
        toast_obj = NULL;
    }
    toast_timer = NULL;
}

static void toast_show(const char *text, lv_color_t color)
{
    if (toast_obj != NULL)
    {
        lv_obj_delete(toast_obj);
        toast_obj = NULL;
    }
    if (toast_timer != NULL)
    {
        lv_timer_delete(toast_timer);
        toast_timer = NULL;
    }

    toast_obj = lv_obj_create(lv_layer_top());
    lv_obj_remove_flag(toast_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(toast_obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(toast_obj, C_SIDEBAR, 0);
    lv_obj_set_style_radius(toast_obj, 10, 0);
    lv_obj_set_style_border_width(toast_obj, 0, 0);
    lv_obj_set_style_border_side(toast_obj, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(toast_obj, 4, 0);
    lv_obj_set_style_border_color(toast_obj, color, 0);
    lv_obj_set_style_pad_hor(toast_obj, 16, 0);
    lv_obj_set_style_pad_ver(toast_obj, 10, 0);
    lv_obj_set_style_shadow_width(toast_obj, 24, 0);
    lv_obj_set_style_shadow_opa(toast_obj, LV_OPA_30, 0);

    label_create(toast_obj, text, FONT_SM, C_TEXT_INV);
    lv_obj_align(toast_obj, LV_ALIGN_BOTTOM_RIGHT, -16, -16);

    toast_timer = lv_timer_create(toast_timer_cb, 2200, NULL);
    lv_timer_set_repeat_count(toast_timer, 1);
}

/* ============================================================================
 * 4. STRUCTURE DE L'ÉCRAN
 * ========================================================================== */
static void nav_event_cb(lv_event_t *e)
{
    page_id_t page = (page_id_t)(intptr_t)lv_event_get_user_data(e);
    show_page(page);
}

static void create_sidebar(void)
{
    lv_obj_t *sb = box_create(lv_screen_active());
    lv_obj_set_size(sb, SIDEBAR_W, SCREEN_H);
    lv_obj_set_style_bg_color(sb, C_SIDEBAR, 0);
    lv_obj_set_style_bg_opa(sb, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(sb, 10, 0);
    lv_obj_set_flex_flow(sb, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(sb, 4, 0);

    /* Marque */
    lv_obj_t *brand = box_create(sb);
    lv_obj_set_size(brand, LV_PCT(100), 48);
    lv_obj_t *logo = label_create(brand, "SMART WASH", FONT_MD, C_ACCENT);
    lv_obj_set_style_text_letter_space(logo, 1, 0);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_t *tag = label_create(brand, "Industrial HMI", FONT_XS, lv_color_hex(0x8C96A3));
    lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 6, 28);

    /* Menu */
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        lv_obj_t *b = lv_button_create(sb);
        nav_btn[i] = b;
        lv_obj_set_size(b, LV_PCT(100), 50);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, C_SIDEBAR, 0);
        lv_obj_set_style_text_color(b, lv_color_hex(0xC5CCD4), 0);
        lv_obj_set_style_bg_color(b, C_SIDEBAR_HOVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(b, C_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(b, C_TEXT, LV_STATE_CHECKED);
        lv_obj_set_style_text_font(b, FONT_SM, 0);
        lv_obj_set_style_pad_left(b, 10, 0);
        lv_obj_set_style_pad_right(b, 4, 0);

        char txt[48];
        snprintf(txt, sizeof(txt), "%s  %s", PAGES[i].icon, PAGES[i].label);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

        if (PAGES[i].admin_only)
        {
            lv_obj_t *pin = label_create(b, "PIN", FONT_XS, lv_color_hex(0x8C96A3));
            lv_obj_align(pin, LV_ALIGN_RIGHT_MID, 0, 0);
        }
        if (i == PAGE_ALARMES)
        {
            nav_badge_alarm = label_create(b, "0", FONT_XS, lv_color_white());
            lv_obj_set_style_bg_color(nav_badge_alarm, C_ALARM, 0);
            lv_obj_set_style_bg_opa(nav_badge_alarm, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(nav_badge_alarm, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_pad_hor(nav_badge_alarm, 7, 0);
            lv_obj_set_style_pad_ver(nav_badge_alarm, 2, 0);
            lv_obj_align(nav_badge_alarm, LV_ALIGN_RIGHT_MID, -6, 0);
            lv_obj_add_flag(nav_badge_alarm, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_event_cb(b, nav_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void access_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (admin_mode)
    {
        admin_mode = false;
        add_audit_entry("DECONNEXION", "ADMINISTRATEUR", "Fin de session administrateur");
        update_access_badge();
        toast_show("Session administrateur fermee", C_TEXT_MUTED);
        if (PAGES[current_page].admin_only)
            show_page(PAGE_HOME);
    }
    else
    {
        open_pin_modal(current_page);
    }
}

static void create_header(void)
{
    lv_obj_t *hd = box_create(lv_screen_active());
    lv_obj_set_size(hd, SCREEN_W - SIDEBAR_W, HEADER_H);
    lv_obj_set_pos(hd, SIDEBAR_W, 0);
    lv_obj_set_style_bg_color(hd, C_HEADER, 0);
    lv_obj_set_style_bg_opa(hd, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(hd, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(hd, 1, 0);
    lv_obj_set_style_border_color(hd, C_BORDER, 0);

    header_title = label_create(hd, "", FONT_LG, C_TEXT);
    lv_obj_align(header_title, LV_ALIGN_TOP_LEFT, 18, 8);
    header_subtitle = label_create(hd, "", FONT_XS, C_TEXT_MUTED);
    lv_obj_align(header_subtitle, LV_ALIGN_TOP_LEFT, 18, 34);

    header_clock = label_create(hd, "--:--:--", FONT_SM, C_TEXT_MUTED);
    lv_obj_align(header_clock, LV_ALIGN_RIGHT_MID, -196, 0);

    access_btn = lv_button_create(hd);
    lv_obj_set_size(access_btn, 172, 36);
    lv_obj_align(access_btn, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_set_style_radius(access_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(access_btn, 0, 0);
    lv_obj_set_style_text_font(access_btn, FONT_XS, 0);
    access_label = lv_label_create(access_btn);
    lv_obj_center(access_label);
    lv_obj_add_event_cb(access_btn, access_event_cb, LV_EVENT_CLICKED, NULL);

    update_access_badge();
}

static void create_content(void)
{
    content = lv_obj_create(lv_screen_active());
    lv_obj_set_size(content, SCREEN_W - SIDEBAR_W, SCREEN_H - HEADER_H);
    lv_obj_set_pos(content, SIDEBAR_W, HEADER_H);
    lv_obj_set_style_bg_color(content, C_BG, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 12, 0);
}

static void update_access_badge(void)
{
    if (access_btn == NULL) return;
    if (admin_mode)
    {
        lv_label_set_text(access_label, LV_SYMBOL_EDIT "  ADMINISTRATEUR");
        lv_obj_set_style_bg_color(access_btn, C_ACCENT, 0);
        lv_obj_set_style_text_color(access_btn, C_TEXT, 0);
    }
    else
    {
        lv_label_set_text(access_label, LV_SYMBOL_EYE_OPEN "  OPERATEUR");
        lv_obj_set_style_bg_color(access_btn, C_INFO_BG, 0);
        lv_obj_set_style_text_color(access_btn, C_TEXT, 0);
    }
}

/* Remise à zéro des pointeurs des widgets de page (évite tout accès à un
 * objet supprimé lors du changement d'écran) */
static void clear_content(void)
{
    lv_obj_clean(content);
    lv_obj_set_layout(content, LV_LAYOUT_NONE);     /* Annule un flex éventuel */
    lv_obj_set_style_pad_row(content, 0, 0);
    lv_obj_set_style_pad_column(content, 0, 0);
    lv_obj_scroll_to_y(content, 0, LV_ANIM_OFF);

    home_state_value = home_step_label = home_progress_bar = home_progress_pct = NULL;
    home_temp_arc = home_temp_value = home_pump_arc = home_pump_value = NULL;
    home_pressure_value = home_pressure_bar = home_level_value = home_level_bar = NULL;
    home_alarm_strip = home_alarm_icon = home_alarm_text = NULL;
    for (int i = 0; i < 4; i++) mes_arc[i] = mes_value[i] = mes_chip[i] = NULL;
    par_temp_value = par_pump_value = NULL;
    alm_list = alm_count_label = alm_ack_btn = NULL;
    man_state_chip = NULL;
    sys_test_btn = NULL;
}

/* Point d'entrée unique de la navigation, avec contrôle d'accès */
static void show_page(page_id_t page)
{
    if (PAGES[page].admin_only && !admin_mode)
    {
        open_pin_modal(page);       /* Écran critique : code PIN exigé */
        return;
    }

    current_page = page;
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        if (i == (int)page) lv_obj_add_state(nav_btn[i], LV_STATE_CHECKED);
        else                lv_obj_remove_state(nav_btn[i], LV_STATE_CHECKED);
    }
    lv_label_set_text(header_title, PAGES[page].title);
    lv_label_set_text(header_subtitle, PAGES[page].subtitle);

    clear_content();
    switch (page)
    {
        case PAGE_HOME:       build_home();       break;
        case PAGE_MESURES:    build_mesures();    break;
        case PAGE_HISTORIQUE: build_historique(); break;
        case PAGE_PARAMETRES: build_parametres(); break;
        case PAGE_ALARMES:    build_alarmes();    break;
        case PAGE_MANUEL:     build_manuel();     break;
        case PAGE_SYSTEME:    build_systeme();    break;
        default: break;
    }
    update_live_widgets();
    update_alarm_widgets();
}

/* ============================================================================
 * 5. ÉCRANS
 * ========================================================================== */

/* --- Commandes de cycle (accessibles aux deux profils depuis l'accueil) --- */
static void start_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    machine_start();
    cycle_start_time = time(NULL);
    cycle_active = true;
    add_audit_entry("CYCLE", current_user(), "Demarrage du cycle");
    toast_show(LV_SYMBOL_PLAY "  Cycle demarre", C_OK);
    update_live_widgets();
}

static void stop_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    machine_stop();
    add_audit_entry("CYCLE", current_user(), "Arret manuel du cycle");
    toast_show(LV_SYMBOL_STOP "  Cycle arrete", C_ALARM);
    update_live_widgets();
}

static void goto_alarms_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    show_page(PAGE_ALARMES);
}

/* --- ACCUEIL ------------------------------------------------------------- */
static void build_home(void)
{
    /* État machine + progression */
    lv_obj_t *st = card_create(content, 380, 140);
    lv_obj_set_pos(st, 0, 0);
    caption_create(st, "ETAT MACHINE");
    home_state_value = label_create(st, "PRETE", FONT_XL, C_OK);
    lv_obj_align(home_state_value, LV_ALIGN_TOP_LEFT, 0, 18);
    home_step_label = label_create(st, "En attente de demarrage", FONT_XS, C_TEXT_MUTED);
    lv_obj_align(home_step_label, LV_ALIGN_TOP_LEFT, 0, 58);
    home_progress_bar = bar_create(st, 300, C_ACCENT);
    lv_obj_align(home_progress_bar, LV_ALIGN_BOTTOM_LEFT, 0, -4);
    home_progress_pct = label_create(st, "0 %", FONT_SM, C_TEXT);
    lv_obj_align(home_progress_pct, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* Commandes */
    lv_obj_t *cmd = card_create(content, 208, 140);
    lv_obj_set_pos(cmd, 392, 0);
    caption_create(cmd, "COMMANDES");
    lv_obj_t *b1 = button_create(cmd, 182, 44, LV_SYMBOL_PLAY "  DEMARRER",
                                 BTN_PRIMARY, start_event_cb, NULL);
    lv_obj_align(b1, LV_ALIGN_TOP_MID, 0, 22);
    lv_obj_t *b2 = button_create(cmd, 182, 44, LV_SYMBOL_STOP "  STOP",
                                 BTN_DANGER, stop_event_cb, NULL);
    lv_obj_align(b2, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* Température */
    lv_obj_t *tc = card_create(content, 188, 180);
    lv_obj_set_pos(tc, 0, 152);
    caption_create(tc, "TEMPERATURE (C)");
    home_temp_arc = gauge_create(tc, 124, 0, 100, C_ACCENT, &home_temp_value);
    lv_obj_align(home_temp_arc, LV_ALIGN_BOTTOM_MID, 0, 8);

    /* Pompe */
    lv_obj_t *pc = card_create(content, 188, 180);
    lv_obj_set_pos(pc, 198, 152);
    caption_create(pc, "POMPE (%)");
    home_pump_arc = gauge_create(pc, 124, 0, 100, C_ACCENT, &home_pump_value);
    lv_obj_align(home_pump_arc, LV_ALIGN_BOTTOM_MID, 0, 8);

    /* Pression et niveau */
    lv_obj_t *pl = card_create(content, 208, 180);
    lv_obj_set_pos(pl, 392, 152);
    caption_create(pl, "PRESSION");
    home_pressure_value = label_create(pl, "-- bar", FONT_LG, C_TEXT);
    lv_obj_align(home_pressure_value, LV_ALIGN_TOP_LEFT, 0, 18);
    home_pressure_bar = bar_create(pl, 182, C_ACCENT);
    lv_obj_align(home_pressure_bar, LV_ALIGN_TOP_LEFT, 0, 50);
    lv_obj_t *lv_cap = caption_create(pl, "NIVEAU D'EAU");
    lv_obj_align(lv_cap, LV_ALIGN_TOP_LEFT, 0, 80);
    home_level_value = label_create(pl, "-- %", FONT_LG, C_TEXT);
    lv_obj_align(home_level_value, LV_ALIGN_TOP_LEFT, 0, 98);
    home_level_bar = bar_create(pl, 182, C_ACCENT);
    lv_obj_align(home_level_bar, LV_ALIGN_TOP_LEFT, 0, 130);

    /* Bandeau d'alarmes (cliquable) */
    home_alarm_strip = lv_obj_create(content);
    lv_obj_set_size(home_alarm_strip, 600, 48);
    lv_obj_set_pos(home_alarm_strip, 0, 344);
    lv_obj_remove_flag(home_alarm_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(home_alarm_strip, 12, 0);
    lv_obj_set_style_border_width(home_alarm_strip, 0, 0);
    lv_obj_set_style_pad_hor(home_alarm_strip, 16, 0);
    lv_obj_add_flag(home_alarm_strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_alarm_strip, goto_alarms_cb, LV_EVENT_CLICKED, NULL);
    home_alarm_icon = label_create(home_alarm_strip, LV_SYMBOL_OK, FONT_MD, C_OK);
    lv_obj_align(home_alarm_icon, LV_ALIGN_LEFT_MID, 0, 0);
    home_alarm_text = label_create(home_alarm_strip, "Aucune alarme active", FONT_SM, C_OK);
    lv_obj_align(home_alarm_text, LV_ALIGN_LEFT_MID, 30, 0);
    lv_obj_t *more = label_create(home_alarm_strip, "Voir " LV_SYMBOL_RIGHT, FONT_XS, C_TEXT_MUTED);
    lv_obj_align(more, LV_ALIGN_RIGHT_MID, 0, 0);
}

/* --- MESURES ------------------------------------------------------------- */
static void build_mesures(void)
{
    static const char *titles[4] = { "TEMPERATURE", "PRESSION", "POMPE", "NIVEAU D'EAU" };
    static const char *ranges[4] =
    {
        "Plage 0 - 100 C\nAlarme > 80 C",
        "Plage 0 - 8,0 bar\nAlarme > 6,0 bar",
        "Plage 0 - 100 %\nConsigne reglable",
        "Plage 0 - 100 %\nAlarme < 20 % en lavage"
    };
    static const int32_t max_v[4] = { 100, 80, 100, 100 };

    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(content, 12, 0);
    lv_obj_set_style_pad_column(content, 12, 0);

    for (int i = 0; i < 4; i++)
    {
        lv_obj_t *c = card_create(content, 294, 188);
        caption_create(c, titles[i]);
        mes_chip[i] = chip_create(c, "NORMAL", C_OK, C_OK_BG);
        lv_obj_align(mes_chip[i], LV_ALIGN_TOP_RIGHT, 0, -2);

        mes_arc[i] = gauge_create(c, 132, 0, max_v[i], C_ACCENT, &mes_value[i]);
        lv_obj_align(mes_arc[i], LV_ALIGN_BOTTOM_LEFT, -4, 10);

        lv_obj_t *r = label_create(c, ranges[i], FONT_XS, C_TEXT_MUTED);
        lv_obj_set_style_text_line_space(r, 6, 0);
        lv_obj_align(r, LV_ALIGN_BOTTOM_RIGHT, 0, -24);
    }
}

/* --- HISTORIQUE ---------------------------------------------------------- */
static lv_obj_t *table_row(lv_obj_t *parent, int32_t y, bool header, bool striped)
{
    lv_obj_t *row = box_create(parent);
    lv_obj_set_size(row, LV_PCT(100), header ? 22 : 28);
    lv_obj_set_pos(row, 0, y);
    if (striped)
    {
        lv_obj_set_style_bg_color(row, lv_color_hex(0xF6F7F8), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 6, 0);
    }
    return row;
}

static void table_cell(lv_obj_t *row, int32_t x, int32_t w, const char *text,
                       const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = label_create(row, text, font, color);
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, x, 0);
}

static void build_historique(void)
{
    /* Cycles */
    lv_obj_t *cc = card_create(content, 600, 206);
    lv_obj_set_pos(cc, 0, 0);
    caption_create(cc, "5 DERNIERS CYCLES");

    lv_obj_t *tbl = box_create(cc);
    lv_obj_set_size(tbl, LV_PCT(100), 172);
    lv_obj_align(tbl, LV_ALIGN_TOP_LEFT, 0, 20);

    lv_obj_t *h = table_row(tbl, 0, true, false);
    table_cell(h, 8,   170, "DATE / HEURE", FONT_XS, C_TEXT_MUTED);
    table_cell(h, 180, 100, "DUREE",        FONT_XS, C_TEXT_MUTED);
    table_cell(h, 288, 100, "RESULTAT",     FONT_XS, C_TEXT_MUTED);
    table_cell(h, 412, 160, "CAUSE",        FONT_XS, C_TEXT_MUTED);

    if (history_count == 0)
    {
        lv_obj_t *e = label_create(tbl, "Aucun cycle enregistre", FONT_SM, C_TEXT_MUTED);
        lv_obj_center(e);
    }
    for (int i = 0; i < history_count && i < 5; i++)
    {
        lv_obj_t *r = table_row(tbl, 24 + i * 29, false, (i % 2) == 0);
        char dt[40];
        snprintf(dt, sizeof(dt), "%s  %s", history[i].date, history[i].heure);
        table_cell(r, 8,   170, dt,                FONT_SM, C_TEXT);
        table_cell(r, 180, 100, history[i].duree,  FONT_SM, C_TEXT);
        lv_obj_t *chip = history[i].ok ? chip_create(r, "OK", C_OK, C_OK_BG)
                                       : chip_create(r, "INTERROMPU", C_ALARM, C_ALARM_BG);
        lv_obj_align(chip, LV_ALIGN_LEFT_MID, 284, 0);
        table_cell(r, 412, 160, history[i].cause,  FONT_SM, C_TEXT_MUTED);
    }

    /* Journal de sécurité */
    lv_obj_t *jc = card_create(content, 600, 182);
    lv_obj_set_pos(jc, 0, 218);
    caption_create(jc, "JOURNAL DES ACTIONS ET DE SECURITE");

    lv_obj_t *jt = box_create(jc);
    lv_obj_set_size(jt, LV_PCT(100), 148);
    lv_obj_align(jt, LV_ALIGN_TOP_LEFT, 0, 20);

    lv_obj_t *jh = table_row(jt, 0, true, false);
    table_cell(jh, 8,   150, "DATE / HEURE", FONT_XS, C_TEXT_MUTED);
    table_cell(jh, 165, 110, "TYPE",         FONT_XS, C_TEXT_MUTED);
    table_cell(jh, 280, 120, "UTILISATEUR",  FONT_XS, C_TEXT_MUTED);
    table_cell(jh, 405, 175, "DETAIL",       FONT_XS, C_TEXT_MUTED);

    if (audit_count == 0)
    {
        lv_obj_t *e = label_create(jt, "Aucune action enregistree", FONT_SM, C_TEXT_MUTED);
        lv_obj_center(e);
    }
    for (int i = 0; i < audit_count && i < 4; i++)
    {
        lv_obj_t *r = table_row(jt, 24 + i * 30, false, (i % 2) == 0);
        char dt[40];
        snprintf(dt, sizeof(dt), "%s  %s", audit_log[i].date, audit_log[i].heure);
        table_cell(r, 8,   150, dt,                       FONT_XS, C_TEXT);
        table_cell(r, 165, 110, audit_log[i].action,      FONT_XS, C_TEXT);
        table_cell(r, 280, 120, audit_log[i].utilisateur, FONT_XS, C_TEXT);
        table_cell(r, 405, 175, audit_log[i].detail,      FONT_XS, C_TEXT_MUTED);
    }
}

/* --- PARAMÈTRES (administrateur) ----------------------------------------- */
static void temp_slider_cb(lv_event_t *e)
{
    lv_obj_t *s = (lv_obj_t *)lv_event_get_target(e);
    int v = (int)lv_slider_get_value(s);

    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        machine_set_temperature(v);
        if (par_temp_value) lv_label_set_text_fmt(par_temp_value, "%d C", v);
    }
    else if (lv_event_get_code(e) == LV_EVENT_RELEASED)
    {
        char d[96];
        snprintf(d, sizeof(d), "Consigne temperature : %d C", v);
        add_audit_entry("PARAMETRE", current_user(), d);
        toast_show(LV_SYMBOL_SAVE "  Consigne enregistree", C_OK);
    }
}

static void pump_slider_cb(lv_event_t *e)
{
    lv_obj_t *s = (lv_obj_t *)lv_event_get_target(e);
    int v = (int)lv_slider_get_value(s);

    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        pump_setpoint = v;
        if (par_pump_value) lv_label_set_text_fmt(par_pump_value, "%d %%", v);
    }
    else if (lv_event_get_code(e) == LV_EVENT_RELEASED)
    {
        char d[96];
        snprintf(d, sizeof(d), "Consigne pompe : %d %%", v);
        add_audit_entry("PARAMETRE", current_user(), d);
        toast_show(LV_SYMBOL_SAVE "  Consigne enregistree", C_OK);
    }
}

static lv_obj_t *setpoint_card(lv_obj_t *parent, int32_t y, const char *title,
                               const char *hint, int32_t min, int32_t max,
                               int32_t value, lv_event_cb_t cb, lv_obj_t **value_label)
{
    lv_obj_t *c = card_create(parent, 600, 132);
    lv_obj_set_pos(c, 0, y);
    caption_create(c, title);

    lv_obj_t *h = label_create(c, hint, FONT_XS, C_TEXT_MUTED);
    lv_obj_align(h, LV_ALIGN_TOP_LEFT, 0, 18);

    *value_label = label_create(c, "", FONT_XL, C_TEXT);
    lv_obj_align(*value_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *s = lv_slider_create(c);
    lv_obj_set_size(s, 532, 10);
    lv_obj_align(s, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s, C_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, C_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s, cb, LV_EVENT_RELEASED, NULL);

    char lo[16], hi[16];
    snprintf(lo, sizeof(lo), "%d", (int)min);
    snprintf(hi, sizeof(hi), "%d", (int)max);
    lv_obj_t *l1 = label_create(c, lo, FONT_XS, C_TEXT_MUTED);
    lv_obj_align(l1, LV_ALIGN_BOTTOM_LEFT, 22, 0);
    lv_obj_t *l2 = label_create(c, hi, FONT_XS, C_TEXT_MUTED);
    lv_obj_align(l2, LV_ALIGN_BOTTOM_RIGHT, -22, 0);
    return c;
}

static void build_parametres(void)
{
    setpoint_card(content, 0, "TEMPERATURE CIBLE", "Consigne de chauffage de l'eau (C)",
                  20, 90, temperature_value, temp_slider_cb, &par_temp_value);
    lv_label_set_text_fmt(par_temp_value, "%d C", temperature_value);

    setpoint_card(content, 144, "VITESSE DE POMPE", "Debit de brassage pendant le lavage (%)",
                  0, 100, pump_setpoint, pump_slider_cb, &par_pump_value);
    lv_label_set_text_fmt(par_pump_value, "%d %%", pump_setpoint);

    lv_obj_t *note = box_create(content);
    lv_obj_set_size(note, 600, 44);
    lv_obj_set_pos(note, 0, 288);
    lv_obj_set_style_bg_color(note, C_WARN_BG, 0);
    lv_obj_set_style_bg_opa(note, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(note, 10, 0);
    lv_obj_t *t = label_create(note, LV_SYMBOL_EDIT "  Mode administrateur : chaque modification est "
                               "inscrite dans le journal de securite.", FONT_XS, C_ACCENT_DARK);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 14, 0);
}

/* --- ALARMES ------------------------------------------------------------- */
static void ack_all_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    int n = 0;
    for (int i = 0; i < MAX_ALARMS; i++)
    {
        if (alarms[i].state == ALARM_ACTIVE)
        {
            char d[96];
            snprintf(d, sizeof(d), "%s acquittee", alarms[i].code);
            alarms[i].state = ALARM_ACKNOWLEDGED;
            add_audit_entry("ALARME", current_user(), d);   /* Opérateur ou admin */
            n++;
        }
    }
    if (n > 0)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), LV_SYMBOL_OK "  %d alarme(s) acquittee(s)", n);
        toast_show(msg, C_OK);
    }
    update_alarm_widgets();
}

static void build_alarmes(void)
{
    alm_count_label = label_create(content, "", FONT_MD, C_TEXT);
    lv_obj_set_pos(alm_count_label, 4, 10);

    alm_ack_btn = button_create(content, 204, 40, LV_SYMBOL_OK "  TOUT ACQUITTER",
                                BTN_PRIMARY, ack_all_cb, NULL);
    lv_obj_align(alm_ack_btn, LV_ALIGN_TOP_RIGHT, 0, 0);

    alm_list = box_create(content);
    lv_obj_set_size(alm_list, 600, 340);
    lv_obj_set_pos(alm_list, 0, 52);
    lv_obj_add_flag(alm_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(alm_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(alm_list, 8, 0);

    alarm_signature = 0xFFFFFFFFu;   /* Force la reconstruction */
}

static void rebuild_alarm_list(void)
{
    if (alm_list == NULL) return;
    lv_obj_clean(alm_list);

    int shown = 0;
    for (int i = 0; i < MAX_ALARMS; i++)
    {
        alarm_t *a = &alarms[i];
        if (a->state == ALARM_RESOLVED) continue;
        shown++;

        bool crit = (a->level == ALARM_CRITICAL);
        lv_color_t col = crit ? C_ALARM : C_WARN;
        lv_color_t bg  = crit ? C_ALARM_BG : C_WARN_BG;

        lv_obj_t *row = card_create(alm_list, 600, 72);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(row, 6, 0);
        lv_obj_set_style_border_color(row, col, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);

        lv_obj_t *code = chip_create(row, a->code, col, bg);
        lv_obj_align(code, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_t *name = label_create(row, a->name, FONT_MD, C_TEXT);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 92, 0);
        lv_obj_t *det = label_create(row, a->detail, FONT_XS, C_TEXT_MUTED);
        lv_obj_align(det, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        char hhmm[16] = "";
        struct tm *t = localtime(&a->timestamp);
        if (t != NULL) strftime(hhmm, sizeof(hhmm), "%H:%M:%S", t);
        lv_obj_t *tm = label_create(row, hhmm, FONT_XS, C_TEXT_MUTED);
        lv_obj_align(tm, LV_ALIGN_TOP_RIGHT, 0, 2);

        if (a->state == ALARM_ACTIVE)
        {
            lv_obj_t *s = chip_create(row, crit ? "CRITIQUE - ACTIVE" : "AVERTISSEMENT - ACTIVE", col, bg);
            lv_obj_align(s, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        }
        else
        {
            lv_obj_t *s = chip_create(row, "ACQUITTEE", C_TEXT_MUTED, C_INFO_BG);
            lv_obj_align(s, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        }
    }

    if (shown == 0)
    {
        lv_obj_t *empty = card_create(alm_list, 600, 200);
        lv_obj_t *ic = label_create(empty, LV_SYMBOL_OK, FONT_XL, C_OK);
        lv_obj_align(ic, LV_ALIGN_CENTER, 0, -30);
        lv_obj_t *t1 = label_create(empty, "Aucune alarme active", FONT_LG, C_TEXT);
        lv_obj_align(t1, LV_ALIGN_CENTER, 0, 10);
        lv_obj_t *t2 = label_create(empty, "Tous les parametres sont dans leur plage normale.",
                                    FONT_XS, C_TEXT_MUTED);
        lv_obj_align(t2, LV_ALIGN_CENTER, 0, 40);
    }
}

/* --- MANUEL (administrateur) --------------------------------------------- */
static void build_manuel(void)
{
    lv_obj_t *c = card_create(content, 600, 300);
    lv_obj_set_pos(c, 0, 0);
    caption_create(c, "COMMANDE MANUELLE");

    lv_obj_t *l = label_create(c, "Etat actuel", FONT_SM, C_TEXT_MUTED);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 36);
    man_state_chip = chip_create(c, "PRETE", C_OK, C_OK_BG);
    lv_obj_set_style_text_font(man_state_chip, FONT_LG, 0);
    lv_obj_set_style_pad_hor(man_state_chip, 22, 0);
    lv_obj_set_style_pad_ver(man_state_chip, 8, 0);
    lv_obj_align(man_state_chip, LV_ALIGN_TOP_MID, 0, 64);

    lv_obj_t *b1 = button_create(c, 230, 72, LV_SYMBOL_PLAY "  DEMARRER", BTN_PRIMARY,
                                 start_event_cb, NULL);
    lv_obj_set_style_text_font(b1, FONT_LG, 0);
    lv_obj_align(b1, LV_ALIGN_BOTTOM_LEFT, 40, -24);
    lv_obj_t *b2 = button_create(c, 230, 72, LV_SYMBOL_STOP "  STOP", BTN_DANGER,
                                 stop_event_cb, NULL);
    lv_obj_set_style_text_font(b2, FONT_LG, 0);
    lv_obj_align(b2, LV_ALIGN_BOTTOM_RIGHT, -40, -24);

    lv_obj_t *note = box_create(content);
    lv_obj_set_size(note, 600, 44);
    lv_obj_set_pos(note, 0, 312);
    lv_obj_set_style_bg_color(note, C_WARN_BG, 0);
    lv_obj_set_style_bg_opa(note, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(note, 10, 0);
    lv_obj_t *t = label_create(note, LV_SYMBOL_WARNING "  Pilotage direct hors cycle automatique : "
                               "reserve a l'administrateur.", FONT_XS, C_ACCENT_DARK);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 14, 0);
}

/* --- SYSTÈME ------------------------------------------------------------- */
static void status_card(lv_obj_t *parent, const char *title, const char *chip,
                        lv_color_t fg, lv_color_t bg, const char *line1, const char *line2)
{
    lv_obj_t *c = card_create(parent, 600, 96);
    caption_create(c, title);
    lv_obj_t *ch = chip_create(c, chip, fg, bg);
    lv_obj_align(ch, LV_ALIGN_TOP_RIGHT, 0, -2);
    lv_obj_t *l1 = label_create(c, line1, FONT_SM, C_TEXT);
    lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_obj_t *l2 = label_create(c, line2, FONT_XS, C_TEXT_MUTED);
    lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 0, 50);
}

static void update_test_button(void)
{
    if (sys_test_btn == NULL) return;
    lv_obj_t *l = lv_obj_get_child(sys_test_btn, 0);
    lv_label_set_text(l, alarm_test_active ? LV_SYMBOL_STOP "  ARRETER LE TEST"
                                           : LV_SYMBOL_WARNING "  LANCER LE TEST");
}

static void alarm_test_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    alarm_test_active = !alarm_test_active;
    add_audit_entry("TEST", current_user(),
                    alarm_test_active ? "Debut du test d'alarme" : "Fin du test d'alarme");
    if (!alarm_test_active)
        toast_show(LV_SYMBOL_OK "  Test d'alarme termine", C_OK);
    update_test_button();
}

static void build_systeme(void)
{
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 10, 0);

    status_card(content, "1. PERSISTANCE DES PARAMETRES", "NON DISPONIBLE", C_ALARM, C_ALARM_BG,
                "Les reglages sont conserves uniquement pendant la session.",
                "Evolution prevue : sauvegarde en memoire non volatile (NVS) sur ESP32.");
    status_card(content, "2. AUTHENTIFICATION", "PIN LOCAL", C_WARN, C_WARN_BG,
                "Code PIN : 3 essais, puis blocage de 30 s.",
                "Serveur d'authentification : non  -  Second facteur (2FA) : non.");
    status_card(content, "3. ACQUITTEMENT DES ALARMES", "HISTORISE", C_OK, C_OK_BG,
                "Date, heure et utilisateur sont traces dans le journal.",
                "Journal conserve uniquement pendant l'execution du prototype.");

    /* Test de la chaîne d'alarme : simule une surpression de 6,8 bar */
    lv_obj_t *tc = card_create(content, 600, 96);
    caption_create(tc, "4. TEST DES ALARMES");
    lv_obj_t *tl1 = label_create(tc, "Simule une surpression (6,8 bar) pour verifier", FONT_SM, C_TEXT);
    lv_obj_align(tl1, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_obj_t *tl2 = label_create(tc, "l'affichage, l'acquittement et la tracabilite.", FONT_XS, C_TEXT_MUTED);
    lv_obj_align(tl2, LV_ALIGN_TOP_LEFT, 0, 50);
    sys_test_btn = button_create(tc, 190, 44, "", BTN_DANGER, alarm_test_cb, NULL);
    lv_obj_align(sys_test_btn, LV_ALIGN_RIGHT_MID, 0, 8);
    update_test_button();

    char info[96];
    snprintf(info, sizeof(info), "Prototype  -  LVGL v%d.%d.%d  -  Ecran %dx%d",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, SCREEN_W, SCREEN_H);
    label_create(content, info, FONT_XS, C_TEXT_MUTED);
}

/* ============================================================================
 * 6. AUTHENTIFICATION PAR CODE PIN
 * ========================================================================== */
static bool pin_is_locked(void)
{
    if (pin_lock_until == 0)
        return false;
    if (time(NULL) >= pin_lock_until)       /* Fin du blocage */
    {
        pin_lock_until = 0;
        pin_failed_attempts = 0;
        return false;
    }
    return true;
}

static void pin_reset_input(void)
{
    memset(pin_buffer, 0, sizeof(pin_buffer));
    pin_length = 0;
}

static void update_pin_display(void)
{
    if (pin_overlay == NULL) return;

    /* Points de saisie : un point plein par chiffre saisi */
    char dots[64] = "";
    int slots = (pin_length > 4) ? pin_length : 4;
    for (int i = 0; i < slots; i++)
    {
        strcat(dots, i < pin_length ? LV_SYMBOL_BULLET : "-");
        if (i < slots - 1) strcat(dots, "   ");
    }
    lv_label_set_text(pin_dots, dots);

    bool locked = pin_is_locked();
    uint32_t n = lv_obj_get_child_count(pin_keypad);
    for (uint32_t i = 0; i < n; i++)
    {
        lv_obj_t *k = lv_obj_get_child(pin_keypad, (int32_t)i);
        if (locked) lv_obj_add_state(k, LV_STATE_DISABLED);
        else        lv_obj_remove_state(k, LV_STATE_DISABLED);
    }

    if (locked)
    {
        long r = (long)(pin_lock_until - time(NULL));
        lv_label_set_text_fmt(pin_info, LV_SYMBOL_WARNING "  Clavier bloque - reessayez dans %ld s", r);
        lv_obj_set_style_text_color(pin_info, C_ALARM, 0);
    }
    else
    {
        lv_label_set_text_fmt(pin_info, "Tentatives restantes : %d / %d",
                              MAX_PIN_ATTEMPTS - pin_failed_attempts, MAX_PIN_ATTEMPTS);
        lv_obj_set_style_text_color(pin_info, C_TEXT_MUTED, 0);
    }
}

static void pin_validate(void)
{
    if (strcmp(pin_buffer, ADMIN_PIN) == 0)             /* Code correct */
    {
        admin_mode = true;
        pin_failed_attempts = 0;
        pin_lock_until = 0;
        add_audit_entry("CONNEXION", "ADMINISTRATEUR", "Authentification reussie");
        close_pin_modal();
        update_access_badge();
        toast_show(LV_SYMBOL_EDIT "  Session administrateur ouverte", C_ACCENT);
        show_page(pending_page);
        return;
    }

    /* Code incorrect */
    pin_failed_attempts++;
    add_audit_entry("AUTHENTIF.", "OPERATEUR", "Code PIN incorrect");
    pin_reset_input();

    if (pin_failed_attempts >= MAX_PIN_ATTEMPTS)
    {
        pin_lock_until = time(NULL) + PIN_LOCK_SECONDS;
        lv_label_set_text(pin_message, "3 tentatives incorrectes");
    }
    else
    {
        lv_label_set_text(pin_message, "Code PIN incorrect");
    }
    update_pin_display();
}

static void pin_key_cb(lv_event_t *e)
{
    if (pin_is_locked()) { update_pin_display(); return; }

    const char *key = (const char *)lv_event_get_user_data(e);
    if (strcmp(key, "DEL") == 0)
    {
        if (pin_length > 0) pin_buffer[--pin_length] = '\0';
    }
    else if (strcmp(key, "OK") == 0)
    {
        pin_validate();
        return;
    }
    else if (pin_length < PIN_MAX_LEN)
    {
        pin_buffer[pin_length++] = key[0];
        pin_buffer[pin_length] = '\0';
        lv_label_set_text(pin_message, "");
    }
    update_pin_display();
}

static void pin_cancel_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    close_pin_modal();
}

static void open_pin_modal(page_id_t target)
{
    static const char *keys[12] = { "1", "2", "3", "4", "5", "6",
                                    "7", "8", "9", "DEL", "0", "OK" };
    close_pin_modal();
    pending_page = target;
    pin_reset_input();

    /* Voile sombre sur tout l'écran */
    pin_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(pin_overlay);
    lv_obj_set_size(pin_overlay, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(pin_overlay, lv_color_hex(0x0B0E12), 0);
    lv_obj_set_style_bg_opa(pin_overlay, LV_OPA_60, 0);
    lv_obj_add_flag(pin_overlay, LV_OBJ_FLAG_CLICKABLE);   /* Bloque l'arrière-plan */

    lv_obj_t *card = card_create(pin_overlay, 340, 452);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, 18, 0);

    lv_obj_t *close = button_create(card, 36, 36, LV_SYMBOL_CLOSE, BTN_GHOST, pin_cancel_cb, NULL);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 6, -6);

    lv_obj_t *t = label_create(card, "Acces administrateur", FONT_LG, C_TEXT);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *s = label_create(card, "Saisissez votre code PIN", FONT_XS, C_TEXT_MUTED);
    lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 28);

    pin_dots = label_create(card, "", FONT_LG, C_TEXT);
    lv_obj_align(pin_dots, LV_ALIGN_TOP_MID, 0, 54);

    pin_message = label_create(card, "", FONT_SM, C_ALARM);
    lv_obj_align(pin_message, LV_ALIGN_TOP_MID, 0, 88);
    pin_info = label_create(card, "", FONT_XS, C_TEXT_MUTED);
    lv_obj_align(pin_info, LV_ALIGN_TOP_MID, 0, 110);

    /* Clavier numérique 3 x 4 */
    pin_keypad = box_create(card);
    lv_obj_set_size(pin_keypad, 300, 4 * 56 + 3 * 8);
    lv_obj_align(pin_keypad, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(pin_keypad, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(pin_keypad, 8, 0);
    lv_obj_set_style_pad_column(pin_keypad, 8, 0);

    for (int i = 0; i < 12; i++)
    {
        const char *txt = keys[i];
        btn_kind_t kind = BTN_GHOST;
        if (strcmp(keys[i], "DEL") == 0) txt = LV_SYMBOL_BACKSPACE;
        if (strcmp(keys[i], "OK") == 0)  { txt = LV_SYMBOL_OK; kind = BTN_PRIMARY; }
        lv_obj_t *k = button_create(pin_keypad, 94, 56, txt, kind, pin_key_cb, (void *)keys[i]);
        lv_obj_set_style_text_font(k, FONT_LG, 0);
    }

    update_pin_display();
}

static void close_pin_modal(void)
{
    if (pin_overlay != NULL)
    {
        lv_obj_delete_async(pin_overlay);
        pin_overlay = pin_dots = pin_message = pin_info = pin_keypad = NULL;
    }
    pin_reset_input();
}

/* ============================================================================
 * 7. LOGIQUE
 * ========================================================================== */
static void add_audit_entry(const char *action, const char *user, const char *detail)
{
    int last = (audit_count < MAX_AUDIT) ? audit_count : MAX_AUDIT - 1;
    for (int i = last; i > 0; i--)          /* Le plus récent en tête */
        audit_log[i] = audit_log[i - 1];
    if (audit_count < MAX_AUDIT)
        audit_count++;

    now_strings(audit_log[0].date, sizeof(audit_log[0].date),
                audit_log[0].heure, sizeof(audit_log[0].heure));
    copy_str(audit_log[0].action, action, sizeof(audit_log[0].action));
    copy_str(audit_log[0].utilisateur, user, sizeof(audit_log[0].utilisateur));
    copy_str(audit_log[0].detail, detail, sizeof(audit_log[0].detail));
}

static void add_history_entry(bool ok, const char *cause)
{
    int last = (history_count < MAX_HISTORY) ? history_count : MAX_HISTORY - 1;
    for (int i = last; i > 0; i--)
        history[i] = history[i - 1];
    if (history_count < MAX_HISTORY)
        history_count++;

    now_strings(history[0].date, sizeof(history[0].date),
                history[0].heure, sizeof(history[0].heure));
    time_t d = (cycle_start_time != 0) ? time(NULL) - cycle_start_time : 0;
    snprintf(history[0].duree, sizeof(history[0].duree), "%02d min %02d s",
             (int)(d / 60), (int)(d % 60));
    copy_str(history[0].cause, cause, sizeof(history[0].cause));
    history[0].ok = ok;

    cycle_start_time = 0;
    cycle_active = false;
}

static void seed_history_demo(void)
{
    /* Cycles de démonstration (pendant la période de stage) */
    static const history_entry_t demo[] =
    {
        { "28/08/2026", "14:32:10", "08 min 25 s", "Cycle normal",  true  },
        { "28/08/2026", "10:05:44", "07 min 58 s", "Cycle normal",  true  },
        { "27/08/2026", "16:47:02", "03 min 12 s", "Arret urgence", false },
        { "27/08/2026", "09:15:30", "08 min 10 s", "Cycle normal",  true  },
        { "26/08/2026", "15:58:12", "02 min 45 s", "Stop manuel",   false },
    };
    history_count = (int)(sizeof(demo) / sizeof(demo[0]));
    memcpy(history, demo, sizeof(demo));
}

static void set_alarm(int index, const char *code, const char *name,
                      const char *detail, alarm_level_t level, bool condition)
{
    alarm_t *a = &alarms[index];
    if (condition)
    {
        if (a->state == ALARM_RESOLVED)          /* Nouvelle apparition */
        {
            copy_str(a->code, code, sizeof(a->code));
            copy_str(a->name, name, sizeof(a->name));
            copy_str(a->detail, detail, sizeof(a->detail));
            a->level = level;
            a->state = ALARM_ACTIVE;
            a->timestamp = time(NULL);
            if (level == ALARM_CRITICAL)
                toast_show(LV_SYMBOL_WARNING "  Nouvelle alarme critique", C_ALARM);
        }
    }
    else
    {
        a->state = ALARM_RESOLVED;               /* Retour à la normale */
    }
}

static void evaluate_alarms(machine_state_t s)
{
    bool needs_water = (s == MACHINE_HEATING || s == MACHINE_WASHING);
    bool pump_needed = (s == MACHINE_FILLING || s == MACHINE_WASHING);

    set_alarm(0, "ALM-001", "Surchauffe", "Temperature superieure a 80 C",
              ALARM_CRITICAL, temperature_value > TEMP_MAX_C);
    set_alarm(1, "ALM-002", "Surpression", "Pression superieure a 6,0 bar",
              ALARM_CRITICAL, pressure_x10 > PRESSURE_MAX_X10);
    set_alarm(2, "ALM-003", "Niveau bas", "Niveau d'eau inferieur a 20 % pendant le lavage",
              ALARM_CRITICAL, needs_water && level_value < LEVEL_MIN_PCT);
    set_alarm(3, "ALM-004", "Niveau eleve", "Niveau d'eau superieur a 95 %",
              ALARM_WARNING, level_value > LEVEL_MAX_PCT);
    set_alarm(4, "ALM-005", "Arret d'urgence", "Arret d'urgence actif",
              ALARM_CRITICAL, s == MACHINE_EMERGENCY);
    set_alarm(5, "ALM-006", "Pompe arretee", "Pompe demandee mais consigne nulle",
              ALARM_WARNING, pump_needed && pump_setpoint == 0);
}

/* Simulation simple des grandeurs non fournies par machine.c */
static int approach(int value, int target, int step)
{
    if (value < target) return (value + step > target) ? target : value + step;
    if (value > target) return (value - step < target) ? target : value - step;
    return value;
}

static void simulate_process(machine_state_t s)
{
    int level_target = 0;
    if (s == MACHINE_FILLING) level_target = 80;
    else if (s == MACHINE_HEATING || s == MACHINE_WASHING || s == MACHINE_RINSING) level_target = 80;
    level_value = approach(level_value, level_target, 3);

    int p_target = 10;                                   /* 1,0 bar au repos */
    if (s == MACHINE_WASHING || s == MACHINE_RINSING)
        p_target = 25 + pump_setpoint / 10;              /* 2,5 à 3,5 bar   */
    pressure_x10 = approach(pressure_x10, p_target, 1);
    if (p_target > 10)
        pressure_x10 += (rand() % 3) - 1;                /* Bruit de mesure */

    if (alarm_test_active)                               /* Défaut simulé   */
        pressure_x10 = 68;                               /* 6,8 bar > seuil */
}

static void handle_cycle_transitions(machine_state_t s)
{
    if (s == MACHINE_FINISHED && previous_state != MACHINE_FINISHED && cycle_active)
        add_history_entry(true, "Cycle normal");
    else if ((s == MACHINE_STOPPED || s == MACHINE_EMERGENCY) &&
             state_is_running(previous_state) && cycle_active)
        add_history_entry(false, s == MACHINE_EMERGENCY ? "Arret urgence" : "Stop manuel");
    previous_state = s;
}

/* Minuteur LVGL : acquisition (simulée), alarmes, rafraîchissement */
static void sim_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    machine_update();
    machine_state_t s = machine_get_state();

    temperature_value = machine_get_temperature();
    simulate_process(s);
    handle_cycle_transitions(s);
    evaluate_alarms(s);

    update_live_widgets();
    update_alarm_widgets();
    if (pin_overlay != NULL)
        update_pin_display();
}

static void clock_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    char buf[16];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm != NULL && header_clock != NULL)
    {
        strftime(buf, sizeof(buf), "%H:%M:%S", tm);
        lv_label_set_text(header_clock, buf);
    }
}

/* Couleur d'une valeur selon son seuil */
static lv_color_t level_color(bool alarm) { return alarm ? C_ALARM : C_ACCENT; }

static void update_live_widgets(void)
{
    machine_state_t s = machine_get_state();
    state_view_t v = state_view(s);
    int progress = cycle_progress(s);
    int pump_actual = state_is_running(s) ? pump_setpoint : 0;
    bool t_alarm = temperature_value > TEMP_MAX_C;
    bool p_alarm = pressure_x10 > PRESSURE_MAX_X10;
    bool l_alarm = (s == MACHINE_HEATING || s == MACHINE_WASHING) && level_value < LEVEL_MIN_PCT;

    /* Accueil */
    if (home_state_value != NULL)
    {
        static const char *steps[] = { "Remplissage", "Chauffage", "Lavage",
                                       "Rincage", "Essorage", "Fin de cycle" };
        lv_label_set_text(home_state_value, v.text);
        lv_obj_set_style_text_color(home_state_value, v.color, 0);
        if (v.step >= 1 && v.step <= 6)
            lv_label_set_text_fmt(home_step_label, "Etape %d / 6  -  %s", v.step, steps[v.step - 1]);
        else if (s == MACHINE_STOPPED || s == MACHINE_EMERGENCY)
            lv_label_set_text(home_step_label, "Cycle interrompu");
        else
            lv_label_set_text(home_step_label, "En attente de demarrage");
        lv_bar_set_value(home_progress_bar, progress, LV_ANIM_ON);
        lv_label_set_text_fmt(home_progress_pct, "%d %%", progress);

        lv_arc_set_value(home_temp_arc, temperature_value);
        lv_obj_set_style_arc_color(home_temp_arc, level_color(t_alarm), LV_PART_INDICATOR);
        lv_label_set_text_fmt(home_temp_value, "%d", temperature_value);

        lv_arc_set_value(home_pump_arc, pump_actual);
        lv_label_set_text_fmt(home_pump_value, "%d", pump_actual);

        lv_label_set_text_fmt(home_pressure_value, "%d,%d bar", pressure_x10 / 10, pressure_x10 % 10);
        lv_bar_set_value(home_pressure_bar, pressure_x10 * 100 / 80, LV_ANIM_ON);
        lv_obj_set_style_bg_color(home_pressure_bar, level_color(p_alarm), LV_PART_INDICATOR);
        lv_label_set_text_fmt(home_level_value, "%d %%", level_value);
        lv_bar_set_value(home_level_bar, level_value, LV_ANIM_ON);
        lv_obj_set_style_bg_color(home_level_bar, level_color(l_alarm), LV_PART_INDICATOR);
    }

    /* Mesures */
    if (mes_arc[0] != NULL)
    {
        int vals[4] = { temperature_value, pressure_x10, pump_actual, level_value };
        bool al[4]  = { t_alarm, p_alarm, false, l_alarm };
        for (int i = 0; i < 4; i++)
        {
            lv_arc_set_value(mes_arc[i], vals[i]);
            lv_obj_set_style_arc_color(mes_arc[i], level_color(al[i]), LV_PART_INDICATOR);
            chip_set(mes_chip[i], al[i] ? "ALERTE" : "NORMAL",
                     al[i] ? C_ALARM : C_OK, al[i] ? C_ALARM_BG : C_OK_BG);
        }
        lv_label_set_text_fmt(mes_value[0], "%d C", temperature_value);
        lv_label_set_text_fmt(mes_value[1], "%d,%d bar", pressure_x10 / 10, pressure_x10 % 10);
        lv_label_set_text_fmt(mes_value[2], "%d %%", pump_actual);
        lv_label_set_text_fmt(mes_value[3], "%d %%", level_value);
    }

    /* Manuel */
    if (man_state_chip != NULL)
        chip_set(man_state_chip, v.text, v.color, v.bg);
}

static void update_alarm_widgets(void)
{
    int active = 0, unacked = 0, critical = 0;
    uint32_t sig = 0;
    for (int i = 0; i < MAX_ALARMS; i++)
    {
        sig = sig * 3u + (uint32_t)alarms[i].state;
        if (alarms[i].state == ALARM_RESOLVED) continue;
        active++;
        if (alarms[i].state == ALARM_ACTIVE) unacked++;
        if (alarms[i].level == ALARM_CRITICAL) critical++;
    }

    /* Pastille du menu */
    if (nav_badge_alarm != NULL)
    {
        if (unacked > 0)
        {
            lv_label_set_text_fmt(nav_badge_alarm, "%d", unacked);
            lv_obj_remove_flag(nav_badge_alarm, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(nav_badge_alarm, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Bandeau de l'accueil */
    if (home_alarm_strip != NULL)
    {
        lv_color_t fg = C_OK, bg = C_OK_BG;
        const char *icon = LV_SYMBOL_OK;
        char txt[64] = "Aucune alarme active";
        if (active > 0)
        {
            fg   = critical > 0 ? C_ALARM : C_WARN;
            bg   = critical > 0 ? C_ALARM_BG : C_WARN_BG;
            icon = LV_SYMBOL_WARNING;
            snprintf(txt, sizeof(txt), "%d alarme(s) active(s)  -  %d non acquittee(s)", active, unacked);
        }
        lv_obj_set_style_bg_color(home_alarm_strip, bg, 0);
        lv_label_set_text(home_alarm_icon, icon);
        lv_obj_set_style_text_color(home_alarm_icon, fg, 0);
        lv_label_set_text(home_alarm_text, txt);
        lv_obj_set_style_text_color(home_alarm_text, fg, 0);
    }

    /* Écran Alarmes */
    if (alm_count_label != NULL)
    {
        lv_label_set_text_fmt(alm_count_label, "%d alarme(s) active(s)", active);
        if (unacked > 0) lv_obj_remove_state(alm_ack_btn, LV_STATE_DISABLED);
        else             lv_obj_add_state(alm_ack_btn, LV_STATE_DISABLED);
        if (sig != alarm_signature)
        {
            alarm_signature = sig;
            rebuild_alarm_list();
        }
    }
}

/* ============================================================================
 * 8. DÉMARRAGE DE L'INTERFACE (appelé après lv_init() et les pilotes)
 * ========================================================================== */
void smart_wash_start(void)
{
    machine_init();
    srand((unsigned)time(NULL));

    memset(alarms, 0, sizeof(alarms));
    memset(audit_log, 0, sizeof(audit_log));
    seed_history_demo();

    lv_obj_set_style_bg_color(lv_screen_active(), C_BG, 0);
    create_sidebar();
    create_header();
    create_content();
    show_page(PAGE_HOME);

    /* Minuteurs LVGL : données toutes les 250 ms, horloge chaque seconde */
    lv_timer_create(sim_timer_cb, SIM_PERIOD_MS, NULL);
    lv_timer_create(clock_timer_cb, CLOCK_PERIOD_MS, NULL);
    clock_timer_cb(NULL);
}
