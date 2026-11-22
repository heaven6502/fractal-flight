/* descent.h - the descent from orbit to the planet (the intro sequence of Behind Jaggi Lines!), between the logo and the flight: the same as on the Atari ST (st_descent.c).
   Black space with a few stars, then the limb of the planet rises and fills the window: a smooth dome (the 22 column heights of the game, Catmull Rom between them) in three colours
   (rust edge, mid orange, light orange = the sky colour of the flight).  Planar: the picture is bitplanes 0 and 1 (two buffers), plane 2 is set in the picture lines (the colours 4 .. 7),
   plane 3 holds the stars (colour 12; the planet's pixels in the group 12 .. 15 have the colours of 4 .. 7, so it covers them). */
#ifndef DESCENT_H
#define DESCENT_H
#include <exec/types.h>

#define DESC_HOLD   220         /* vertical blanks of black space before the descent starts */
#define DESC_END    (220 + 412 + 40)       /* the descent takes 206 steps of 2 vertical blanks, then a short hold on the full orange */
#define DESC_LINES  150         /* picture lines */
#define DESC_STRIDE 40          /* bytes per line of a plane */

extern const UWORD descPalette[16];
void DescentInit(UBYTE *starPlane);                                 /* the stars into plane 3 (the first picture line) */
void DescentFrame(UBYTE *p0, UBYTE *p1, int slot, int t);           /* draw the picture of the vertical blank t (from the start) into planes 0 and 1 (first picture line); slot 0 / 1: which of the two buffers */

#endif
