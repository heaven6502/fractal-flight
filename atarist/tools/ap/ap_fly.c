/* ap_fly.c - long host flights with the C autopilot: share of one second samples with clearance 0 (a ground hit): the autopilot, no stick at all, and the autopilot without the wall steering (the comparison of the Atari figures), several terrains.
   The Atari 800 port measured 1.5 % (autopilot) against 3.7 % (stick released) over 120 s in Altirra.  usage: ap_fly [ticks] [terrains] */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/flight_core.c"
int main(int argc, char **argv) {
	int ticks = argc > 1 ? atoi(argv[1]) : 20000, nter = argc > 2 ? atoi(argv[2]) : 8, ter, mode;
	for (mode = 0; mode < 3; mode++) {
		long hits = 0, samples = 0, land = 0, thrmax = 0;
		for (ter = 0; ter < nter; ter++) {
			TFlight f;
			int t;
			FSeed(1000 + ter); fRndState = 1 + ter;
			InitGridMap();
			StartFlight(&f);
			for (t = 0; t < ticks; t++) {
				if (mode == 2) apL = apR = 0;                              /* the wall steering switched off, altitude and speed as they are */
				int porta = mode != 1 ? FAutopilotAtari(&f) : 0x0F;
				if (t % 3 == 0) RenderPrepare(&f);                      /* a picture every third tick (about 8 pictures a second) */
				FlightTick(&f, porta);
				GroundCollision(&f);
				if (t % 25 == 0) { samples++; if (f.m061D == 0) hits++; if (f.speedCfg > thrmax) thrmax = f.speedCfg; }
			}
		}
		printf("%s: %ld samples, clearance 0 in %.2f %%, highest thrust %ld\n", mode == 0 ? "autopilot  " : "no stick   ", samples, 100.0 * hits / samples, thrmax);
	}
	return 0;
}
