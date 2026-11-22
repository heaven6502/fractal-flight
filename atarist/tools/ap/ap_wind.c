/* ap_wind.c - statistics of the wind: the added roll (1/256 bank) over 2000 ticks: mean, rms, extremes, direction changes per second */
#include <stdio.h>
#include <math.h>
#include "../../src/flight_core.c"
int main(void) {
	TFlight f; int t, mn = 0, mx = 0, flips = 0, last = 0, prev = 0; double s = 0, s2 = 0;
	memset(&f, 0, sizeof f);
	for (t = 0; t < 2000; t++) {
		int r;
		f.rollLo = f.rollHi = 0;
		FWind(&f);
		r = (short)(f.rollLo | f.rollHi << 8);
		s += r; s2 += (double)r * r; if (r < mn) mn = r; if (r > mx) mx = r;
		if (r - prev > 8) { if (last < 0) flips++; last = 1; } else if (r - prev < -8) { if (last > 0) flips++; last = -1; }
		prev = r;
	}
	printf("added roll: mean %.1f, rms %.1f, min %d, max %d (256 = 1.0 bank), direction changes %.1f per second\n", s / 2000, sqrt(s2 / 2000), mn, mx, flips / 80.0);
	return 0;
}
