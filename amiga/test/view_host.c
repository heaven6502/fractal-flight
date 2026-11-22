/* view_host.c - prints the terrain and an ASCII picture of the flight view (host debugging aid) */
#include <stdio.h>
#include <stdlib.h>
#include "flight_core.h"
int main(int argc, char **argv) {
    TFlight f; int r, c, i, n = 0, ticks = argc > 2 ? atoi(argv[2]) : 0;
    FSeed(argc > 1 ? (unsigned)atoi(argv[1]) : 20261106u);
    InitGridMap();
    for (r = 0; r < 16; r++) { for (c = 0; c < 16; c++) putchar((heightmap[r * 16 + c] & 0x80) ? '#' : '.'); putchar('\n'); }
    StartFlight(&f);
    for (i = 0; i < ticks; i++) { FlightTick(&f, 0x0F); GroundCollision(&f); }
    RenderScene(&f);
    for (r = 0; r < VIEW_H; r++) for (c = 0; c < VIEW_W; c++) n += scr[r * VIEW_W + c];
    printf("alt %02X%02X clearance %d pixels %d\n", f.altHi, f.altLo, f.m061D, n);
    for (r = 0; r < VIEW_H; r += 2) { for (c = 0; c < VIEW_W; c += 2) putchar(scr[r * VIEW_W + c] || scr[(r+1) * VIEW_W + c] ? '*' : ' '); putchar('\n'); }
    return 0;
}
