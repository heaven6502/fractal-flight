; st_crt.s - entry point of a GEMDOS program without libc: go to supervisor mode, call main, return to user mode, Pterm0.  vasm, Motorola syntax.
	section	.text,code
	xdef	_start
	xref	main

_start:
	clr.l	-(sp)
	move.w	#$20,-(sp)		; Super(0)
	trap	#1
	addq.l	#6,sp
	move.l	d0,savedSsp
	jsr	main
	move.l	savedSsp,-(sp)
	move.w	#$20,-(sp)		; Super(old ssp): back to user mode
	trap	#1
	addq.l	#6,sp
	clr.w	-(sp)			; Pterm0
	trap	#1

	section	.bss,bss
savedSsp:
	ds.l	1
