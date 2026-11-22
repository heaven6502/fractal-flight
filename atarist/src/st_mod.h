/* st_mod.h - the music of the flight: on an STE the MOD (src/st_hackl.s, Hacking Lance, 12.5 kHz DMA sound), on an ST the SNDH tune */
#ifndef ST_MOD_H
#define ST_MOD_H
#include "st_sys.h"

#ifndef MOD_DRUMS_FRAMES
#define MOD_DRUMS_FRAMES 380        /* vertical blanks of the MOD before its drums come in (the flight is shown then): a PLACEHOLDER, see the analysis of DESERTD4.MOD */
#endif

/* Starts the music: an STE (the _MCH cookie) plays the MOD, anything else the SNDH tune (or the YM stream if that is not an SNDH).  Returns 1 if the MOD plays. */
int  MusicStart(const u8 *sndh, int subtune, const u8 *ym);
void MusicVbl(void);                 /* from the vertical blank hook, first thing (the MOD mixer; the SNDH player runs from its own timer) */
u32  MusicPlays(void);               /* the clock of the music: PLAY calls of the SNDH tune / vertical blanks of the MOD (both 50 per second) */
u32  MusicDrums(void);               /* the value of MusicPlays() at which the drums come in */
int  MusicIsMod(void);
extern u32 modArenaFree;             /* bytes of the MOD arena left after the replay was prepared (diagnostics, shown next to the picture rate) */
void MusicStop(void);
#endif
