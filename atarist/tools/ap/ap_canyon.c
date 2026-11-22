/* ap_canyon.c - how well does the canyon autopilot follow the canyons?  Flights over several terrains: ground hits (seconds with clearance 0), share of the time over canyon cells (height < $80),
   turn rate left / right (heading units per minute, 16384 = full turn), mountain flights (mode changes) per minute, mean |bank|, mean altitude above the ground.  usage: ap_canyon [ticks] [terrains] [atari] */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/flight_core.c"
int main(int argc, char **argv) {
	int ticks = argc > 1 ? atoi(argv[1]) : 15000, nter = argc > 2 ? atoi(argv[2]) : 6, atari = argc > 3, ter;
	double right = 0, left = 0, bank = 0, clr = 0; long hitm0 = 0, hitm1 = 0, hits = 0, samples = 0, canyon = 0, n = 0, mt = 0, pl = 0, pr = 0; int prevmode = 0;
	for (ter = 0; ter < nter; ter++) {
		TFlight f; int t;
		FSeed(1000 + ter); fRndState = 1 + ter; InitGridMap(); StartFlight(&f);
#ifdef BASE
		f.speedCfg = BASE;
#endif
		for (t = 0; t < ticks; t++) {
			int h0 = f.headLo | f.headHi << 8, h1, dh, porta, px, pz, mode, want;
			porta = atari ? FAutopilotAtari(&f) : FAutopilot(&f);
			if (t % 3 == 0) RenderPrepare(&f);
			if (!(porta & F_LEFT)) pl++; if (!(porta & F_RIGHT)) pr++;
			FlightTick(&f, porta); GroundCollision(&f);
			h1 = f.headLo | f.headHi << 8; dh = ((h1 - h0 + 0x2000) & 0x3FFF) - 0x2000;
			if (dh > 0) right += dh; else left -= dh;
			{ int b = (signed char)f.bankHi; bank += b < 0 ? -b : b; }
			px = f.posXLo | f.posXHi << 8; pz = f.posZLo | f.posZHi << 8;
			if (ApCellH(px, pz) < 0x80) canyon++;
			FAutopilotModeState(&mode, &want); if (mode && !prevmode) mt++; prevmode = mode;
			clr += f.m061D; n++;
			if (t % 25 == 0) { samples++; if (f.m061D == 0) { hits++; if (mode) hitm1++; else hitm0++; } }
		}
	}
	printf("%s: ground hits %.2f %% | over canyon cells %.0f %% | turn/min right %.0f left %.0f | mountain flights/min %.2f | stick L %.0f %% R %.0f %% | mean |bank| %.2f | mean clearance %.0f\n",
		atari ? "atari  " : "canyon ", 100.0 * hits / samples, 100.0 * canyon / n, right * 1500.0 / n, left * 1500.0 / n, mt * 1500.0 / n, 100.0 * pl / n, 100.0 * pr / n, bank / n, clr / n, hitm0, hitm1);
	return 0;
}
