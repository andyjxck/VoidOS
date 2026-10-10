#ifndef VOIDOS_APP_H
#define VOIDOS_APP_H

#include <stdint.h>
#include "voidos/hal.h"

#define APP_TAP_MAX_DIST 12
#define APP_SWIPE_MIN_DIST 40
#define APP_WORKOUT_SETS 3
#define APP_REST_MS 90000u

typedef enum {
    SCR_FACE = 0,
    SCR_LAUNCHER,
    SCR_QUICK,
    SCR_NOTIF,
    SCR_STATS,
    SCR_NUTRITION,
    SCR_WORKOUT,
    SCR_WORKOUT_RUN,
    SCR_WORKOUT_DONE,
    SCR_SLEEP,
    SCR_SLEEP_DONE,
    SCR_WATER,
    SCR_SETTINGS,
    SCR_CLOCK,
    SCR_REPLY,
    SCR_STAT_DETAIL,
    SCR_SCAN
} app_screen_t;

typedef enum {
    SWIPE_NONE = 0,
    SWIPE_UP,
    SWIPE_DOWN,
    SWIPE_LEFT,
    SWIPE_RIGHT
} app_swipe_t;

typedef struct {
    app_screen_t screen;
    int quit;

    int touch_active;
    int touch_moved;
    int touch_start_x;
    int touch_start_y;
    int touch_last_x;
    int touch_last_y;
    uint32_t touch_down_ms;

    int workout_active;
    int workout_paused;
    int sets_done;
    uint32_t workout_start_ms;
    uint32_t pause_start_ms;
    uint32_t paused_total_ms;
    uint32_t rest_end_ms;
    uint32_t last_workout_ms;

    int in_bed;
    uint32_t bed_start_ms;
    uint32_t last_in_bed_ms;

    int water_ml;
    int settings_parent;

    /* control centre */
    int dnd_on;
    int airplane_on;
    int bt_on;
    int torch_on;
    int water_lock_on;
    uint32_t ping_t0;      /* find-phone ping start time */
    int brightness;   /* 0-100 */
    int slider_drag;

    /* page features */
    int sleep_track;          /* sleep tracking enabled */
    unsigned cc_hide;         /* hide CC buttons: 1=torch 2=water 4=phone */
    int workout_routine;      /* -1 = quick session, else routine index */
    int clock_tab;            /* 0 stopwatch, 1 timer, 2 alarm */
    int sw_running;
    uint32_t sw_t0;           /* stopwatch start epoch */
    uint32_t sw_acc;          /* stopwatch accumulated while stopped */
    uint32_t timer_end_ms;    /* countdown end, 0 = idle */
    int timer_done;           /* countdown fired, awaiting dismissal */
    int alarm_on;

    int notif_cleared;     /* notifications page: card dismissed */
    char reply_buf[32];    /* reply composer text */
    int reply_len;

    int stat_sel;          /* which stat card was tapped (0-5) */
    int stat_range;        /* 0 = last 24h, 1 = last 7 days */
    int stat_pt;           /* selected chart point, -1 = none */

    /* nutrition log (simulated, watch-local) */
    int kcal;
    int protein_g;
    int carbs_g;
    int fat_g;
    uint32_t scan_t0;      /* barcode-entry result shown since */
    int scan_idx;          /* matched product index, -1 = not found */
    int scan_logged;       /* result applied to totals */
    char barcode[14];      /* typed barcode digits */
    int barcode_len;
    char last_food[24];

    int analog_face;       /* 0 = digital face, 1 = analogue hands */
} app_t;

void app_init(app_t *app);
void app_handle_event(app_t *app, const hal_event_t *ev);
void app_render(app_t *app);
void app_run(app_t *app, int max_frames);

int app_launcher_tile_at(int x, int y);
void app_dial_point(int pos, int radius, int *x, int *y);
void app_dial_point_f(float pos, int radius, int *x, int *y);
void app_squircle_point(float pos, float radius, int *x, int *y);
float app_sq_pos_at(float f);

#endif
