/* ap_pitch.c - does the nose go down in turns?  Mean pitch (signed, pitchHi units: negative = nose down) while banked (|bank| > 1.0) and while level, the pitch commands the canyon
   autopilot gives while banked, the pitch rms and how often the climb / dive command changes per minute. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../../src/flight_core.c"
int main(void) {
	int ticks = 15000, ter, t, lastcmd = 0;
	double sb = 0, sl = 0, sbd = 0, sbc = 0, sbn = 0, sq = 0;
	long nb = 0, nl = 0, nbd = 0, nbc = 0, nbn = 0, trans = 0, ntot = 0;
	for (ter = 0; ter < 6; ter++) {
		TFlight f;
		FSeed(1000 + ter); fRndState = 1 + ter; InitGridMap(); StartFlight(&f);
#ifdef BASE
		f.speedCfg = BASE;
#endif
		for (t = 0; t < ticks; t++) {
			int porta = FAutopilot(&f), bank, pitch, cmd;
			if (t % 3 == 0) RenderPrepare(&f);
			FlightTick(&f, porta); GroundCollision(&f);
			bank = (short)(f.bankLo | f.bankHi << 8); pitch = (short)(f.pitchLo | f.pitchHi << 8);
			cmd = !(porta & F_UP) ? 1 : !(porta & F_DOWN) ? 2 : 0;
			if (cmd && cmd != lastcmd) trans++;
			if (cmd) lastcmd = cmd;
			ntot++; sq += (pitch / 256.0) * (pitch / 256.0);
			if (abs(bank) > 256) {
				sb += pitch / 256.0; nb++;
				if (!(porta & F_UP)) { sbd += pitch / 256.0; nbd++; } else if (!(porta & F_DOWN)) { sbc += pitch / 256.0; nbc++; } else { sbn += pitch / 256.0; nbn++; }
			} else { sl += pitch / 256.0; nl++; }
		}
	}
	printf("mean pitch while banked (|bank| > 1): %.2f (%ld ticks)  level: %.2f (%ld ticks)\n", sb / nb, nb, sl / nl, nl);
	printf("banked and dive command: %.2f (%ld) | climb command: %.2f (%ld) | no pitch command: %.2f (%ld)\n", sbd / nbd, nbd, sbc / nbc, nbc, sbn / nbn, nbn);
	printf("pitch rms %.2f | dive/climb command changes %.1f per minute\n", sqrt(sq / ntot), trans * 1500.0 / ntot);
	return 0;
}
