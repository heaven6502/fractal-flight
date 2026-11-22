; st_mus_f.s - the SNDH tune of the flight (music/flight.sndh: Pajaro by Scavenger)
	section	.rodata,data
	xdef	sndhTune
	cnop	0,4
sndhTune:
	incbin	"music/flight.sndh"
	even
