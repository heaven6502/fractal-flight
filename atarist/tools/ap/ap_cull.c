/* ap_cull.c - host test of the piece cull in Line51C (-DFLINE_CULL=n): flies the autopilot, renders every 7th tick, prints a checksum of all ridge planes and the number of loop steps / culled pieces / scanned columns.
   usage: build without and with -DFLINE_CULL=4 (and -DFLINE_STATS) and compare: the checksums must be equal. */
#include <stdio.h>
#include <string.h>
#define PLOT_PLANAR
#ifndef SEED
#define SEED 1000
#endif
#include "../../src/flight_core.c"
static u8 work[FSTRIDE * F_ROWS + 16];
int main(void) {
	TFlight f; int n, kk; unsigned long sum = 0;
	FSeed(SEED); fRndState = 1; InitGridMap(); StartFlight(&f); f.speedCfg = 50;
	for (n = 0; n < 300; n++) {
		int i;
		for (kk = 0; kk < 7; kk++) { FlightTick(&f, FAutopilot(&f)); GroundCollision(&f); }
		fPlane = work; memset(work, 0, sizeof work); RenderPrepare(&f); RenderPlot();
		for (i = 0; i < FSTRIDE * F_ROWS; i++) sum = (sum << 1 | sum >> 31) ^ work[i] ^ (unsigned long)(i * 2654435761u);
	}
#ifdef FLINE_STATS
	printf("checksum %lu   steps %lu   culled %lu   scanned columns %lu\n", sum, fLineSteps, fLineCulled, fLineScan);
#else
	printf("checksum %lu\n", sum);
#endif
	return 0;
}
