/* ap_cmp.c - the C flight (flight_core.c with the autopilot) from the state of the Atari 800 demo flight (tools/ap/atari_fly.py): same terrain, same state, same autopilot phase.
   Prints per second the Atari state and the C state side by side (clearance, altitude, heading, position X, Z, thrust).  The tick alignment is +- 1 tick and a picture is assumed
   every 3rd tick, so small differences are expected; the flights should stay together. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/flight_core.c"
int main(int argc, char **argv) {
	FILE *fp = fopen(argc > 1 ? argv[1] : "atari_fly.txt", "r");
	char hex[600];
	TFlight f;
	unsigned st[45];
	int i, t, sec, a[8];
	int renderEvery = argc > 2 ? atoi(argv[2]) : 3, shift = argc > 3 ? atoi(argv[3]) : 0;
	fscanf(fp, "%s", hex);
	for (i = 0; i < 256; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); heightmap[i] = (u8)v; }
	for (i = 0; i < 45; i++) fscanf(fp, "%u", &st[i]);
	FInit();
	memset(&f, 0, sizeof f);
	{ u8 *p = (u8 *)&f; for (i = 0; i < 41; i++) p[i] = (u8)st[i]; }
	apPh = st[41]; apT = st[42]; apL = st[43]; apR = st[44];
	for (t = 0; t < shift; t++) { FlightTick(&f, FAutopilot(&f)); GroundCollision(&f); }
	for (sec = 0; fscanf(fp, "%d %d %d %d %d %d %d %d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7]) == 8; sec++) {
		int posX = f.posXLo | f.posXHi << 8, posZ = f.posZLo | f.posZHi << 8, alt = f.altLo | f.altHi << 8, head = f.headLo | f.headHi << 8;
		printf("%2ds  atari clr %3d alt %5d head %5d x %5d z %5d thr %3d | C clr %3d alt %5d head %5d x %5d z %5d thr %3d\n", sec, a[1], a[2], a[5], a[6], a[7], a[4], f.m061D, alt, head, posX, posZ, f.speedCfg);
		for (t = 0; t < 25; t++) {
			int porta = FAutopilot(&f);
			if (renderEvery && t % renderEvery == 0) RenderPrepare(&f);
			FlightTick(&f, porta);
			GroundCollision(&f);
		}
	}
	return 0;
}
