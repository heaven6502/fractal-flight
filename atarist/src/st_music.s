; st_music.s - the demo YM stream (a .ymr from tools/ymtool.py), the fall back when a program's SNDH file is not a valid SNDH
	section	.rodata,data
	xdef	ymTune
ymTune:	incbin	"music/demo.ymr"
	even
