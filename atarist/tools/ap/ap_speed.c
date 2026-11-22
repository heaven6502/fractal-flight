#include <stdlib.h>
#include <stdio.h>
#include "../../src/flight_core.c"
int main(void) {
	int thr, t;
	for (thr = 30; thr <= 90; thr += 10) {
		TFlight f; FSeed(5); fRndState = 7; InitGridMap(); StartFlight(&f); f.speedCfg = thr;
		for (t = 0; t < 400; t++) { FlightTick(&f, 0x0F); GroundCollision(&f); }
		{ int s0x = f.posXLo | f.posXHi << 8, s0z = f.posZLo | f.posZHi << 8, hx0 = f.headLo | f.headHi << 8, i;
		  long dist = 0;
		  for (i = 0; i < 100; i++) { int ax = f.posXLo | f.posXHi << 8, az = f.posZLo | f.posZHi << 8; FlightTick(&f, 0x0F); GroundCollision(&f);
		    { int dx = (short)((f.posXLo | f.posXHi << 8) - ax), dz = (short)((f.posZLo | f.posZHi << 8) - az); dist += abs(dx) > abs(dz) ? abs(dx) : abs(dz); } }
		  printf("thrust %2d: speedHi %3d, about %ld position units per second (a cell = 4096)\n", thr, f.speedHi, dist * 25 / 100); (void)s0x; (void)s0z; (void)hx0; }
	}
	return 0;
}
