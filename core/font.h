#ifndef VOIDOS_FONT_H
#define VOIDOS_FONT_H

#include <stdint.h>

const uint8_t *font_glyph(char c);
void font_draw_text(int x, int y, const char *s, uint16_t color, int scale);
int font_text_width(const char *s, int scale);

#endif
