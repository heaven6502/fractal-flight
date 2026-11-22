# Models & Hardware Overview

## Family
| Model | Year | CPU | RAM | Notes |
|---|---|---|---|---|
| 520ST / 1040STF | 1985/86 | 68000 @ 8 MHz | 512K / 1MB (520STFM has RF modulator + floppy built-in) | Original chipset: MMU, GLUE, Shifter, DMA. TOS 1.0–1.04 |
| 520STFM / 1040STFM | 1986-87 | same | 512K/1MB | Built-in drive + RF modulator |
| Mega ST 1/2/4 | 1987 | 68000 @ 8 | 1/2/4 MB | Blitter, RTC, detachable keyboard, ACSI HD, TOS 1.02+ |
| STE (520/1040STE) | 1989 | 68000 @ 8 | 512K–4MB (SIMMs) | Blitter, 4096 colours, DMA stereo 8-bit audio, HW scroll, 2 extra joy ports. TOS 1.06/1.62 |
| Mega STE | 1991 | 68000 @ 8/16 MHz switchable, 16K cache | 1–4 MB | STE chipset + VME, SCSI, LAN port, TOS 2.0x |
| TT030 | 1990 | 68030 @ 32 MHz + 68882 | 2–10 MB ST-RAM + up to 256 MB TT-RAM | TT Shifter, SCSI, TOS 3.0x |
| Falcon030 | 1992 | 68030 @ 16 MHz (16-bit bus) + DSP56001 @ 32 MHz | 1–14 MB | Videl, 16-bit codec audio, TOS 4.0x |

**Primary target: plain ST/STF (and STE as bonus).** Most demo-scene "ST" work targets 520/1040 STF or STE.

## Base ST ports
- Monitor: 13-pin DIN — analog RGB (colour, 50/60 Hz), mono (SM124, 71.4 Hz), composite/RF on some
- Parallel: Centronics (driven via YM2149 port B data + port A strobe)
- Serial: RS232 (MFP USART + YM port A RTS/DTR)
- MIDI IN / OUT (ACIA 6850 @ 31.25 kbaud)
- 2× DB9: port 0 mouse/joystick, port 1 joystick (via IKBD)
- ACSI/DMA port: hard disk (19-pin), 8 Mbit/s DMA
- Floppy port (external drive), internal 3.5" 720K/DD (STF has 1 floppy; early 520ST external)
- Cartridge slot: 128 KB ROM at $FA0000
- Keyboard: HD6301 microcontroller "IKBD" (also reads mouse/joysticks, has RTC)

## Chip list (original ST)
| Chip | Function |
|---|---|
| MC68000 | CPU, 8 MHz (PAL 8.021247 MHz from 32.084988 MHz crystal /4) |
| MMU (Atari custom) | DRAM controller, bus arbitration, video address generation, wait states |
| GLUE (Atari custom) | Address decode, IPL (interrupt) generation, video sync timing |
| Shifter (Atari custom) | Video: palette (512 colours), bitplane shifter, RGB DACs |
| DMA (Atari custom) | Floppy + ACSI DMA, 16-byte FIFO |
| MC68901 MFP | Interrupts, 4 timers, USART, 16 GPIP lines |
| YM2149 (PSG) | 3 tone + noise + envelope; I/O ports used for drive select/centronics |
| WD1772 | Floppy disk controller |
| MC6850 ×2 | ACIA: keyboard link and MIDI |
| HD6301 | IKBD keyboard/mouse/joystick/RTC controller |
| TOS ROMs | 192K at $FC0000 (6×32K on 1.0; 2×128K+ on later) |

Custom chips: MMU, GLUE, Shifter, DMA (Mega STE/STE consolidated these into new variants + Blitter ICs).
