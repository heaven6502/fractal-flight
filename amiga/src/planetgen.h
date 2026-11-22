/* planetgen.h - procedural planet sprites for the sun's four attached hardware sprites (flight320_main.c).  PlanetGen renders a planet of one of PG_N families from a seed into pgWords / pgPal,
   in the same format as the gyWords / gyPal of gyruss_planets.h: per line 8 words = {plane 0 .. 3} of the left 16 pixels, then of the right 16 pixels; the pixel code 1 .. 8 selects the colour register 17 .. 24
   (pgPal[code - 1]), 0 = transparent.  No floating point; about 0.2 s on a 68000. */
#ifndef PLANETGEN_H
#define PLANETGEN_H
#define PG_N 6                                    /* the families: Earth-like, gas giant, cratered rock, lava, ice giant, ringed gas giant */
#define PG_ROWS 42
#ifdef PG_ST                                      /* the Atari ST build (make passes -DPG_ST): 6 colours in the palette entries 2 .. 7, 3 planes: 6 words a line */
#define PG_NC 6
#define PG_WPR 6
#else                                             /* the Amiga: 8 colours (the sprite colour registers 17 .. 24), 4 planes: 8 words a line */
#define PG_NC 8
#define PG_WPR 8
#endif
extern unsigned short pgWords[PG_ROWS][PG_WPR];
extern unsigned short pgPal[PG_NC];
void PlanetGen(int family, unsigned long seed);          /* all at once */
void PlanetGenBegin(int family, unsigned long seed);     /* or in steps: Begin, Rows (count lines each time, returns 1 when done), Commit (the new planet replaces pgWords / pgPal) */
int PlanetGenRows(int count);
void PlanetGenCommit(void);
#endif
