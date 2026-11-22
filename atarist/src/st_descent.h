/* st_descent.h - the descent from orbit to the planet (the intro sequence of Behind Jaggi Lines!), between the logo and the flight.
   Black space with a few stars, then the limb of the planet rises and fills the window: a smooth dome (the 22 column heights of the game, Catmull Rom between them)
   in three colours (rust edge, mid orange, light orange = the sky colour of the flight).  Prototype: jaggie_descent.pb (C:\coding\Atari\rof). */
#ifndef ST_DESCENT_H
#define ST_DESCENT_H
#include "st_sys.h"

#define DESC_HOLD   220         /* vertical blanks of black space before the descent starts */
#define DESC_END    (220 + 412 + 40)       /* the descent takes 206 steps of 2 vertical blanks (the game's 190 and the last strip of the limb), then a short hold on the full orange */

extern const u16 descPalette[16];
void DescentInit(u8 *scrA, u8 *scrB);                        /* the model and the stars (plane 3) in both screens; the picture lines must be black (InitScreen) */
void DescentFrame(u8 *scr, int slot, int t);                 /* draw the picture of the vertical blank t (counted from the start) into the screen scr (slot 0 or 1: which of the two) */
void DescentEnd(u8 *scrA, u8 *scrB);                         /* take the stars out of plane 3 again (the flight uses plane 3 for the sun) */

#endif
