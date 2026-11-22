/* ap_wide.c - host check of the F_WIDE projection (Proj88 / ProjectVertex) against floating point maths, and of the 24 bit velocity against the heading */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../../src/flight_core.c"
int main(void) {
	TFlight f; int n, i, cnt = 0; double mx = 0, my = 0, sx = 0, sy = 0, mt = 0;
	FSeed(1000); fRndState = 1; InitGridMap(); StartFlight(&f); f.speedCfg = 50;
	for (n = 0; n < 150; n++) {
		int kk; for (kk = 0; kk < 7; kk++) { FlightTick(&f, FAutopilot(&f)); GroundCollision(&f); }
		RenderPrepare(&f);
		for (i = 0; i < F_NV; i++) {
			if (vflag[i] & 0xE0) continue;
			ProjectVertex(i);
			{
				double X = vx16[i + 1], Z = vz16[i + 1], dy = vdy16[i];
				double xe = 128 + 96 * X / Z;
				double r = 6 - (short)(f.pitchLo | f.pitchHi << 8) / 64.0 + (xe - 128) * (short)(f.rollLo | f.rollHi << 8) / 4096.0;
				double ye = 128 + 96 * dy / Z + r;
				if (xe < 0 || xe > 255 || fabs(96 * dy / Z) > 4000) continue;
				double ex = fabs(px[i] + 0.0 - floor(xe)), ey = fabs(py[i] - floor(ye));
				if (ex > mx) mx = ex; if (ey > my) my = ey; sx += ex; sy += ey; cnt++;
			}
		}
	}
	printf("%d vertices: |px - floor(exact)| max %.0f mean %.3f, |py - floor(exact)| max %.0f mean %.3f\n", cnt, mx, sx / cnt, my, sy / cnt);
	for (n = 0; n < 16384; n += 7) {
		double th = n * 2 * M_PI / 16384, vx_, vz_, e;
		f.headLo = n & 255; f.headHi = n >> 8; f.speedHi = 12; f.speedLo = 0; f.posXHi = f.posXLo = f.posZHi = f.posZLo = 0; posXfr = posZfr = 0;
		Velocity16(&f);
		vx_ = ((f.posXHi << 16) | (f.posXLo << 8) | posXfr); if (vx_ > 8388607) vx_ -= 16777216;
		vz_ = ((f.posZHi << 16) | (f.posZLo << 8) | posZfr); if (vz_ > 8388607) vz_ -= 16777216;
		e = fabs(asin((vx_ * cos(th) - vz_ * sin(th)) / sqrt(vx_ * vx_ + vz_ * vz_ + 1e-9)));
		if (e > mt) mt = e;
	}
	printf("ground track vs heading, speed 12: max %.4f deg\n", mt * 180 / M_PI);
	{ double me = 0; int h; for (h = 0; h < 16384; h++) { double th = h * 2 * M_PI / 16384, ss = SinHead(h), cc = SinHead(h + 0x1000), e = fabs(asin((ss * cos(th) - cc * sin(th)) / sqrt(ss * ss + cc * cc))); if (e > me) me = e; } printf("camera direction (SinHead) vs heading: max %.4f deg\n", me * 180 / M_PI); }
	return 0;
}
