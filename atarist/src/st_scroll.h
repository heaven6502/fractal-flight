/* st_scroll.h - a tiny text scroller in a bar of the low resolution screen (8 x 8 characters of the Atari 8 bit flight font gfx/Antic.chr, plane 1 only) */
#ifndef ST_SCROLL_H
#define ST_SCROLL_H

#include "st_sys.h"

#define SCROLL_Y 183        /* first screen line of the scroller: the bar below the 150 picture lines is 175 .. 199, the 8 lines are centred in it */

/* Builds the strip of the text (0 = the default text, max 400 characters; ASCII, lower case shows as capitals, the font has no other characters).  The text scrolls in from the right and wraps. */
void ScrollInit(const char *text);
/* Draws the scroller (prerotated: a plain word copy, about 2500 cycles) into the lines y0 .. y0 + 7 of `screen` (plane 1 of the bar lines, plane 0 is the bar colour) and moves it `speed` pixels on (1 = single pixel steps).
   Call it from the vertical blank hook with StShown(), before the beam reaches the bar. */
void ScrollTick(u8 *screen, int y0, int speed);
/* the variant with the text in plane 3 (index 8 instead of 3) over a plane 0 bar (index 1): flight320 */
void ScrollTickP3(u8 *screen, int y0, int speed);

#endif
