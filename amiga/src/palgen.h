/* palgen.h - procedural palettes for flight320_main.c: PalGen fills one palette entry (the same fields as the table of the handmade palettes) from a seed. */
#ifndef PALGEN_H
#define PALGEN_H
struct PalEnt { unsigned short sky, nearC, mid, farC, bottom, halo, body, core, star0, star1, star2, bar; };       /* (bar: only the Atari ST uses it, the colour of the scroller bar) */
void PalGen(struct PalEnt *p, unsigned long seed);
#endif
