/* ap_tune.c - how calm is the autopilot?  Flights over several terrains: ground hits, turn rate (heading change per second, right and left), direction reversals per minute,
   share of the ticks with the stick pressed left / right, mean |bank|.   usage: ap_tune [ticks] [terrains]    (compile with -DAP_DEAD=n -DAP_PULSE=n) */
#include <stdio.h>
#include <stdlib.h>
#include "../../src/flight_core.c"
int main(int argc, char **argv) {
	int ticks = argc > 1 ? atoi(argv[1]) : 15000, nter = argc > 2 ? atoi(argv[2]) : 6, ter;
	double right = 0, left = 0, bankSum = 0; long hits = 0, samples = 0, rev = 0, pl = 0, pr = 0, n = 0;
	for (ter = 0; ter < nter; ter++) {
		TFlight f; int t, last = 0, prevsign = 0; long acc = 0;
		FSeed(1000 + ter); fRndState = 1 + ter; InitGridMap(); StartFlight(&f);
		for (t = 0; t < ticks; t++) {
			int h0 = f.headLo | f.headHi << 8, h1, dh, porta = FAutopilotAtari(&f);
			if (t % 3 == 0) RenderPrepare(&f);
			if (!(porta & F_LEFT)) pl++;
			if (!(porta & F_RIGHT)) pr++;
			FlightTick(&f, porta); GroundCollision(&f);
			h1 = f.headLo | f.headHi << 8;
			dh = ((h1 - h0 + 0x2000) & 0x3FFF) - 0x2000;
			if (dh > 0) right += dh; else left -= dh;
			acc += dh; n++;
			{ int bank = (signed char)f.bankHi; bankSum += bank < 0 ? -bank : bank; }
			if (t % 25 == 0) { int sign = acc > 20 ? 1 : acc < -20 ? -1 : 0; samples++; if (f.m061D == 0) hits++; if (sign && prevsign && sign != prevsign) rev++; if (sign) prevsign = sign; acc = 0; }
		}
	}
	printf("ground hits %.2f %% | turned right %.0f, left %.0f (units/min: %.0f / %.0f; full turn = 16384) | direction reversals %.1f per minute | stick left %.0f %% right %.0f %% | mean |bank| %.2f\n",
		100.0 * hits / samples, right, left, right * 1500.0 / n, left * 1500.0 / n, rev * 60.0 * 25.0 * nter / n, 100.0 * pl / n, 100.0 * pr / n, bankSum / n);
	return 0;
}
