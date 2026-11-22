# Blitter, STE, TT030, Falcon030

## Blitter (Mega ST, STE, Mega STE) @ $FF8A00
Not on plain 520/1040 STF. Detect by probing for bus error at $FF8A00.
| Address | Register |
|---|---|
| $FF8A00-$FF8A1E | Halftone RAM (16 words) |
| $FF8A20 | Source X increment |
| $FF8A22 | Source Y increment |
| $FF8A24 | Source address (32 bit) |
| $FF8A28 / 2A / 2C | Endmasks 1 / 2 / 3 |
| $FF8A2E | Dest X increment |
| $FF8A30 | Dest Y increment |
| $FF8A32 | Dest address (32 bit) |
| $FF8A36 | X count (words) |
| $FF8A38 | Y count |
| $FF8A3A | HOP (halftone op) |
| $FF8A3B | OP (logic op, 16 boolean ops) |
| $FF8A3C | Line number / control (bit7 = busy/start, bit6 = HOG mode, bits 0-3 halftone line) |
| $FF8A3D | Skew (shift 0-15, FXSR/NFSR flags) |
- Bit-block transfer on 16-pixel word boundaries with shift, 4-plane aware via repeating per word. 1 bus master; in non-HOG mode shares bus with CPU (about 64 cycles blit / 64 cpu alternating).
- Completion: GPIP3 / MFP interrupt $10C.

## STE (1989) additions
- 12-bit palette (4096 colours), see 03.
- Hardware scroll: $FF8264 fine pixel scroll (0-15), $FF820F line-offset (extra words per line => wide virtual screen), $FF820D video base low byte.
- DMA stereo 8-bit sound: $FF8901 control (bit0 play, bit1 loop), $FF8903/05/07 frame start, $FF8909/0B/0D counter, $FF890F/11/13 frame end, $FF8921 mode (rate in bits 0-1: 0=6258, 1=12517, 2=25033, 3=50066 Hz; bit7 mono). Signed 8-bit.
- Microwire $FF8922 data / $FF8924 mask -> LMC1992 (master vol, L/R vol, bass, treble).
- Two extra DB9 joypad/lightgun ports $FF9200 / $FF9202 (+ paddles $FF9210...).
- Standard blitter, SIMM sockets, TOS 1.06 / 1.62.
- STE-specific 4-cycle/DMA-sound quirks: DMA sound steals bus cycles.

## Mega STE (1991)
8/16 MHz switchable, 16K cache (control at $FF8E21), VME, SCSI, serial LAN, real-time clock, TOS 2.0x.

## TT030 (1990)
- 68030 @ 32 MHz, 68882 FPU, ST-RAM (up to 10 MB, bus 8 MHz) + TT-RAM (fast, up to 256 MB) at $01000000.
- TT Shifter: modes ST low/med/high + TT low 320x480x256 (of 4096), TT medium 640x480x16, TT high 1280x960 mono. Palette $FF8400.
- SCSI (NCR5380 + DMA), SCU, 2 serial extras, 4 serial ports, VME, TOS 3.0x. YM + 8-bit DMA stereo sound inherited from STE.

## Falcon030 (1992)
- 68030 @ 16 MHz on 16-bit bus; 1-14 MB RAM (4 MB ST-RAM + up to 10 MB TT-RAM).
- **Videl** video controller: 262144-colour palette, modes 2/4/16/256 colours (planar) and true colour 16 bpp (65K), up to ~800x608+; VGA & RGB monitors; fully programmable timing.
- **DSP56001** @ 32 MHz: host-port interface at $FFA200; 24-bit fixed-point; used for sound/3D/crypto.
- 16-bit stereo codec sound (matrix at $FF8930+), up to 50 kHz; 8 ch record/playback.
- SCSI-2 (not DMA-fast) + IDE, 3.5" HD floppy, MIDI, Mega-ST-style ports, TOS 4.0x.
(Falcon register addresses: take from Atari Falcon Developer docs - verify.)
