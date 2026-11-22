# MFP 68901, ACIA, IKBD

## MFP MC68901 @ $FFFA00 (registers on ODD byte addresses; clock 2.4576 MHz)
| Offset | Reg | Notes |
|---|---|---|
| $01 | GPIP | General purpose I/O data |
| $03 | AER | Active edge |
| $05 | DDR | Data direction |
| $07 / $09 | IERA / IERB | Interrupt enable |
| $0B / $0D | IPRA / IPRB | Interrupt pending |
| $0F / $11 | ISRA / ISRB | In-service |
| $13 / $15 | IMRA / IMRB | Interrupt mask |
| $17 | VR | Vector reg (TOS uses $48: vectors at $100+, software EOI) |
| $19 | TACR | Timer A control |
| $1B | TBCR | Timer B control |
| $1D | TCDCR | Timers C & D control |
| $1F / $21 / $23 / $25 | TADR / TBDR / TCDR / TDDR | Timer data |
| $27-$2F | SCR, UCR, RSR, TSR, UDR | USART (serial) |
Prescalers: /4 /10 /16 /50 /64 /100 /200 of 2.4576 MHz; Timer B also has event-count mode.

## Interrupt sources (high -> low priority) and vector addresses
| Bit | Source | Vector |
|---|---|---|
| 15 | Mono detect (GPIP7) | $13C |
| 14 | RI (GPIP6) | $138 |
| 13 | Timer A | $134 |
| 12 | RX buffer full | $130 |
| 11 | RX error | $12C |
| 10 | TX buffer empty | $128 |
| 9 | TX error | $124 |
| 8 | Timer B | $120 |
| 7 | GPIP5 (FDC/HDC) | $11C |
| 6 | GPIP4 (keyboard/MIDI ACIA) | $118 |
| 5 | Timer C | $114 |
| 4 | Timer D | $110 |
| 3 | GPIP3 (Blitter done) | $10C |
| 2 | CTS | $108 |
| 1 | DCD | $104 |
| 0 | Centronics busy | $100 |

## Timer conventions
- Timer A: usually free for programs.
- **Timer B**: event-count (counts HBL) or delay mode -> per-scanline raster interrupt (palette splits, border removal).
- Timer C: TOS 200 Hz system tick. Timer D: serial baud generator.
- One timer tick ~ 3.26 CPU cycles. IRQs only fire on instruction boundaries => jitter; sync by reading the video counter low byte ($FF8209) and nop-padding to the beam.

## ACIA 6850
- Keyboard $FFFC00 status/ctrl, $FFFC02 data (7812.5 baud to IKBD).
- MIDI $FFFC04 / $FFFC06 (31250 baud). Both share MFP GPIP4.

## IKBD (HD6301)
- Keyboard scan, mouse, 2 joysticks, real-time clock. Commands via ACIA (e.g. $80 reset, $12 mouse off, $14 joystick event mode, $15 joystick interrogate).
- Demos that kill the OS read the keyboard ACIA directly.

## Serial / parallel
- RS232: MFP USART + YM2149 port A bits (RTS/DTR). Centronics: YM port B = data, port A bit 5 = strobe, busy = GPIP0.
