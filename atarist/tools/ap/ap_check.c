/* ap_check.c - the C autopilot (flight_core.c: ApScan + FAutopilot) on the cases of tools/ap/ap_check.py.  usage: ap_check cases.txt > results.txt
   case line (hex bytes): altHi m061D thrust m065F apPh apT, then 55 x (vflag vh vzh vxh);  result line: stick(low nibble) apL apR apT apPh thrust */
#include <stdio.h>
#include "../../src/flight_core.c"
int main(int argc, char **argv) {
	FILE *f = fopen(argv[1], "r");
	unsigned v[6 + 4 * F_NV];
	int i, n = 0;
	for (;;) {
		TFlight t;
		int porta, ph, tt, l, r;
		for (i = 0; i < 6 + 4 * F_NV; i++) if (fscanf(f, "%x", &v[i]) != 1) goto done;
		memset(&t, 0, sizeof t);
		t.altHi = v[0]; t.m061D = v[1]; t.speedCfg = v[2]; t.m065F = v[3];
		apPh = v[4]; apT = v[5];
		for (i = 0; i < F_NV; i++) {
			vflag[i] = v[6 + 4 * i]; vh[i] = v[7 + 4 * i];
			vz[i] = (int)(short)((v[8 + 4 * i] << 8) | 0x37);       /* the low bytes do not matter */
			vx[i] = (int)(short)((v[9 + 4 * i] << 8) | 0xA5);
		}
		ApScan();
		porta = FAutopilotAtari(&t);
		FAutopilotState(&ph, &tt, &l, &r);
		printf("%02x %02x %02x %02x %02x %02x\n", porta & 0x0F, l, r, tt, ph, t.speedCfg);
		n++;
	}
done:
	fprintf(stderr, "%d cases\n", n);
	return 0;
}
