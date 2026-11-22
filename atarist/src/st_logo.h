/* st_logo.h - the intro picture (src/logo_data.c from tools/gfx/mklogo.py): 320 x 200, 16 colours, faded in and out instead of the black start screen */
#ifndef ST_LOGO_H
#define ST_LOGO_H
#include "st_sys.h"

void LogoDraw(u8 *screen);              /* copies the picture into a screen (32000 bytes, 256 byte aligned); show it with a black palette first (StInit) */
/* Fade in (8 steps of 5 vertical blanks), hold until the SNDH tune has made `fadeOutAtPlay` PLAY calls, fade out (8 steps).  Fire skips the hold; a 12 second limit is for tunes without PLAY calls.  Returns with a black palette. */
void LogoIntro(u32 fadeOutAtPlay);
#endif
