# CPU, Bus, Memory Map

## 68000 specifics on ST
- 16-bit data bus, 24-bit address (16 MB space), big-endian, no cache, no MMU, no FPU.
- Instruction timing: every bus access is aligned by MMU to 4-clock (500 ns) slots shared with the Shifter
  → **all instruction times effectively round up to a multiple of 4 cycles**. (e.g. NOP = 4, MOVE.W d,d = 4, MULU up to 70.)
  RAM access slots: Shifter gets alternating slots with the CPU during display → CPU runs at full speed in RAM but
  with 4-cycle granularity.
- Always count cycles in nops (4 cycles) when beam-racing.
- Stack: supervisor SSP; user USP. TOS programs start in user mode; use GEMDOS Super ($20) or XBIOS Supexec ($26).

## Interrupt levels (ST)
| IPL | Source | Autovector |
|---|---|---|
| 2 | HBL (horizontal blank) | $68 |
| 4 | VBL (vertical blank) | $70 |
| 6 | MFP 68901 (all timers, ACIA, FDC, etc) | $78 (MFP supplies vector via VR → table at $100–$13C) |
(Levels 1,3,5,7 not used on base ST; level 7 = NMI/reset button via unused? `verify`.)

## Exception vectors (RAM, at $0)
$0 reset SSP, $4 reset PC (mirrored from ROM at power-on), $8 bus error, $C addr error, $10 illegal,
$14 div0, $18 CHK, $1C TRAPV, $20 privilege, $24 trace, $28 Line-A, $2C Line-F,
$68 HBL, $70 VBL, $80–$BC TRAP #0–#15 (TRAP #1 GEMDOS, #2 AES/VDI, #13 BIOS, #14 XBIOS),
$100–$13C MFP interrupt vectors (16 of them, see 04), $400 etv_timer.. system variables region $400–$5FF+.

## Memory map (24-bit)
| Range | Contents |
|---|---|
| $000000–$0007FF | Exception vectors + TOS system variables ($400+, OS area to ~$800+) |
| $000800–(memtop) | RAM (512K = $07FFFF, 1MB = $0FFFFF, 4MB = $3FFFFF); screen RAM usually at top (memtop − 32000, 256-byte aligned) |
| $3FFFFF max ST-RAM (4 MB) | |
| $400000–$DFFFFF | Unused/bus error on ST/STE (TT-RAM at $01000000+ on TT) |
| $E00000–$EFFFFF | 1 MB ROM space; TOS 2.x/3.x/4.x live here ($E00000), TOS 1.x at $FC0000 |
| $FA0000–$FBFFFF | 128 KB cartridge ROM |
| $FC0000–$FEFFFF | 192 KB system ROM (TOS 1.x) |
| $FF8000–$FFFFFF | Hardware I/O registers (see below) |

## I/O register map (ST base)
| Address | Device |
|---|---|
| $FF8001 | Memory configuration (MMU) |
| $FF8201 / 03 | Video base address high / mid (STE: +$FF820D low) |
| $FF8205 / 07 / 09 | Video address counter high / mid / low (read live) |
| $FF820A | Sync mode (bit 1: 0=60Hz, 1=50Hz; bit 0 ext sync) |
| $FF8240–$FF825E | Palette 0–15 (word each, 0RGB 3 bits per channel) |
| $FF8260 | Shifter resolution (0=low,1=med,2=high) |
| $FF8604 / 06 | FDC/HDC data + DMA mode/status |
| $FF8609 / 0B / 0D | DMA base address high/mid/low |
| $FF8800 / 02 | YM2149 select/read ; write data |
| $FF8A00–$FF8A3F | Blitter (Mega ST, STE) |
| $FFFA00–$FFFA2F | MFP 68901 (odd byte addresses) |
| $FFFC00 / 02 | Keyboard ACIA control/status ; data |
| $FFFC04 / 06 | MIDI ACIA control/status ; data |
Bus error occurs on accessing unmapped I/O.
