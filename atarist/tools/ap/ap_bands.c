/* ap_bands.c - host check of the depth bands of flightz: flies the autopilot, and every 7 ticks renders one picture (the C line routine, one plane per depth class) and counts how many
   ground pixels fall into each band (the band = the nearest depth class whose ridge lies above the pixel in its column, as FinishFrameZ computes it).  usage: ap_bands */
#include <stdio.h>
#include <string.h>
#define FLIGHT_BANDS
#define PLOT_PLANAR
#include "../../src/flight_core.c"
typedef unsigned int u32;
static u8 work[5 * 3200];
int main(void) {
	TFlight f; int n, kk;
	FSeed(1000); fRndState = 1; InitGridMap(); StartFlight(&f); f.speedCfg = 50;
	for (n = 0; n < 120; n++) {
		u32 cnt[6] = {0};
		int w, r, c, i;
		for (kk = 0; kk < 7; kk++) { FlightTick(&f, FAutopilot(&f)); GroundCollision(&f); }
		fPlane = work; memset(work, 0, sizeof work); RenderPrepare(&f); RenderPlot();
		for (w = 0; w < 20; w++) {
			u32 T[5] = {0};
			for (r = 0; r < 76; r++) {
				u32 U = 0;
				for (c = 0; c < 5; c++) { U |= (work[c * 3200 + r * 40 + w * 2] << 8) | work[c * 3200 + r * 40 + w * 2 + 1]; T[c] |= U; }
				if (r >= 1 && r <= 75)
					for (i = 0; i < 16; i++) {
						u32 m = 0x8000 >> i; int band = -1;
						if (!(T[4] & m)) { cnt[5]++; continue; }
						for (c = 0; c < 5; c++) if (T[c] & m) { band = c; break; }
						cnt[band]++;
					}
			}
		}
		if (n % 6 == 0) printf("picture %3d  ground pixels per band 0 (near) .. 4 (far): %5u %5u %5u %5u %5u | sky %u\n", n, cnt[0], cnt[1], cnt[2], cnt[3], cnt[4], cnt[5]);
	}
	return 0;
}
