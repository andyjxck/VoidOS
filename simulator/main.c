#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voidos/app.h"

int hal_save_screenshot(const char *path);

static int parse_positive(const char *s, int *out)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v <= 0 || v > 1000000)
        return 0;
    *out = (int)v;
    return 1;
}

int main(int argc, char **argv)
{
    int max_frames = -1;
    app_screen_t screen = SCR_FACE;
    const char *screenshot = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            if (!parse_positive(argv[++i], &max_frames)) {
                fprintf(stderr, "invalid --frames value\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshot = argv[++i];
        } else if (strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
            const char *s = argv[++i];
            if (strcmp(s, "face") == 0) screen = SCR_FACE;
            else if (strcmp(s, "launcher") == 0) screen = SCR_LAUNCHER;
            else if (strcmp(s, "stats") == 0) screen = SCR_STATS;
            else if (strcmp(s, "controls") == 0) screen = SCR_QUICK;
            else if (strcmp(s, "notifications") == 0) screen = SCR_NOTIF;
            else if (strcmp(s, "nutrition") == 0) screen = SCR_NUTRITION;
            else if (strcmp(s, "reply") == 0) screen = SCR_REPLY;
            else if (strcmp(s, "stat") == 0) screen = SCR_STAT_DETAIL;
            else if (strcmp(s, "scan") == 0) screen = SCR_SCAN;
            else if (strcmp(s, "workout") == 0) screen = SCR_WORKOUT;
            else if (strcmp(s, "sleep") == 0) screen = SCR_SLEEP;
            else if (strcmp(s, "water") == 0) screen = SCR_WATER;
            else if (strcmp(s, "settings") == 0) screen = SCR_SETTINGS;
            else if (strcmp(s, "clock") == 0) screen = SCR_CLOCK;
            else {
                fprintf(stderr, "invalid --screen value\n");
                return 1;
            }
        } else {
            fprintf(stderr,
                    "usage: %s [--frames N] [--screenshot PATH] [--screen S]\n",
                    argv[0]);
            return 1;
        }
    }
    if (!hal_init())
        return 1;
    app_t app;
    app_init(&app);
    app.screen = screen;
    app_run(&app, max_frames);
    int ok = 1;
    if (screenshot) {
        app_render(&app);
        if (!hal_save_screenshot(screenshot))
            ok = 0;
    }
    hal_shutdown();
    return ok ? 0 : 1;
}
