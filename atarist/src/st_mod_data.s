; st_mod_data.s - the MOD of the STE flight (music/mod/DESERTD2.MOD: the Desert Dream of Kefrens)
	section	.rodata,data
	xdef	modTune
	xdef	modTuneEnd
	cnop	0,4
modTune:
	incbin	"music/mod/DESERTD2.MOD"
modTuneEnd:
	even
