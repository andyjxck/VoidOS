#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voidos/hal.h"

int hal_save_screenshot(const char *path);

static int get_pixel(const unsigned char *data, int w, int h, int pitch,
                     int x, int y, unsigned char *rgb)
{
    if (x < 0 || x >= w || y < 0 || y >= h)
        return 0;
    const unsigned char *p = data + (size_t)(h - 1 - y) * pitch + (size_t)x * 3;
    rgb[0] = p[2];
    rgb[1] = p[1];
    rgb[2] = p[0];
    return 1;
}

int main(void)
{
    const char *path = "build/test_shot.bmp";
    if (!hal_init()) {
        printf("FAIL hal_init\n");
        return 1;
    }
    hal_draw_rect(0, 0, VOIDOS_WIDTH, VOIDOS_HEIGHT, HAL_COLOR_BLACK);
    hal_draw_rect(0, 60, VOIDOS_WIDTH, 2, HAL_COLOR_ACCENT);
    hal_draw_rect(0, 224, VOIDOS_WIDTH, 16, HAL_COLOR_WARN);
    hal_present();
    int ok = hal_save_screenshot(path);
    hal_shutdown();
    if (!ok) {
        printf("FAIL hal_save_screenshot\n");
        return 1;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("FAIL open %s\n", path);
        return 1;
    }
    unsigned char hdr[54];
    if (fread(hdr, 1, 54, f) != 54 || hdr[0] != 'B' || hdr[1] != 'M') {
        printf("FAIL bmp header\n");
        return 1;
    }
    int w = hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | (hdr[21] << 24);
    int h = hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24);
    int bpp = hdr[28] | (hdr[29] << 8);
    if (w != VOIDOS_WIDTH || h != VOIDOS_HEIGHT || bpp != 24) {
        printf("FAIL bmp geometry %dx%d %dbpp\n", w, h, bpp);
        return 1;
    }
    int pitch = ((w * 3) + 3) & ~3;
    unsigned char *data = malloc((size_t)pitch * h);
    if (!data || fread(data, 1, (size_t)pitch * h, f) != (size_t)pitch * h) {
        printf("FAIL bmp pixels\n");
        return 1;
    }
    fclose(f);
    unsigned char rgb[3];
    int fails = 0;
    get_pixel(data, w, h, pitch, 10, 60, rgb);
    if (!(rgb[0] < 60 && rgb[1] > 150 && rgb[2] > 150)) {
        printf("FAIL rule pixel %d,%d,%d\n", rgb[0], rgb[1], rgb[2]);
        fails++;
    }
    get_pixel(data, w, h, pitch, 10, 120, rgb);
    if (rgb[0] > 40 || rgb[1] > 40 || rgb[2] > 40) {
        printf("FAIL mid pixel %d,%d,%d (quarter-capture regression?)\n",
               rgb[0], rgb[1], rgb[2]);
        fails++;
    }
    get_pixel(data, w, h, pitch, 10, 230, rgb);
    if (!(rgb[0] > 150 && rgb[1] > 60 && rgb[1] < 160 && rgb[2] < 80)) {
        printf("FAIL footer pixel %d,%d,%d\n", rgb[0], rgb[1], rgb[2]);
        fails++;
    }
    free(data);
    if (fails)
        return 1;
    printf("screenshot test passed\n");
    return 0;
}
