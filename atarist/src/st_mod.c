/* st_mod.c - the music of the flight: MOD on an STE, SNDH on an ST (see st_mod.h) */
#include "st_mod.h"

#ifndef USE_MOD
/* no MOD (-DUSE_MOD and st_mod_data.s + st_hackl.s in the link bring it back, see the Makefile): its 420 KB arena leaves too little memory on a 1 MB ST for the sun and the light mask; the SNDH tune plays on every machine */
#define modOn 0
u32 modArenaFree;
int MusicStart(const u8 *sndh, int subtune, const u8 *ym) {
#else
extern const u8 modTune[], modTuneEnd[];
extern int  ModInit(const u8 *mod, u32 modlen, u8 *arena, u32 arenasize);
extern void ModVbl(void);
extern void ModStop(void);
extern u32  modFrames;
extern u32  zfreememsize;

#define MOD_ARENA (420 * 1024)                       /* the module with its loop space, the generated mixer code and the tables of the replay */
static u8 arena[MOD_ARENA] __attribute__((aligned(4)));
static int modOn;
u32 modArenaFree;                                    /* bytes of the arena left after ModInit (diagnostics) */

int MusicStart(const u8 *sndh, int subtune, const u8 *ym) {
	if (StMachine() == 1 && ModInit(modTune, (u32)(modTuneEnd - modTune), arena, MOD_ARENA) == 0) {
		modArenaFree = zfreememsize;
		modOn = 1;
		return 1;
	}
#endif
	if (StSndhStart(sndh, subtune)) StYmStart(ym);
	return 0;
}

#ifndef USE_MOD
void MusicVbl(void) {}
u32  MusicPlays(void) { return StSndhPlays(); }
u32  MusicDrums(void) { return 380; }
int  MusicIsMod(void) { return 0; }
void MusicStop(void) {}
#else
void MusicVbl(void) {
#ifndef MOD_NOVBL
	if (modOn) ModVbl();
#endif
}
u32  MusicPlays(void) { return modOn ? modFrames : StSndhPlays(); }
u32  MusicDrums(void) { return modOn ? MOD_DRUMS_FRAMES : 380; }
int  MusicIsMod(void) { return modOn; }
void MusicStop(void) { if (modOn) { ModStop(); modOn = 0; } }
#endif
