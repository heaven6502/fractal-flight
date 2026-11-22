# Sound, FDC, DMA

## YM2149 @ $FF8800 (AY-3-8910 compatible)
- Select register: write number to $FF8800. Read value: read $FF8800. Write value: write $FF8802.
- Clock 2 MHz. 3 square channels, 1 noise, 1 envelope, 4-bit volumes.
| Reg | Function |
|---|---|
| 0,1 | Channel A tone period (fine, coarse 4 bit) |
| 2,3 | Channel B |
| 4,5 | Channel C |
| 6 | Noise period (5 bit) |
| 7 | Mixer: bits 0-2 tone off A/B/C, 3-5 noise off A/B/C, 6-7 port direction |
| 8,9,10 | Volume A/B/C (bit 4 = use envelope) |
| 11,12 | Envelope period |
| 13 | Envelope shape |
| 14 | Port A: bit0 side select, bits1-2 drive select, bit3 RTS, bit4 DTR, bit5 Centronics strobe, bit6 GPO. Do NOT clobber floppy bits |
| 15 | Port B: Centronics data |
- Tone frequency = 2 MHz / (16 x period).
- Digi-samples on plain ST: write volume registers at timer rate (4-bit playback, timer-driven). Also "sync-buzzer"/"SID voice" tricks.
- Music: SNDH files (68000 player code with init/exit/play, called at 50 Hz), YM register-dump files.

## WD1772 FDC + DMA chip
- $FF8604 data / FDC register, $FF8606 DMA mode (write) / status (read). Mode bits select FDC register (A0/A1), sector count reg, FDC vs HDC, DMA direction.
- DMA address: $FF8609 high, $FF860B mid, $FF860D low.
- 250 kbit/s MFM. Standard disk = 80 tracks x 9 sectors x 512 B x 2 sides = 720K.
- Drive/side select via YM port A.
- Images: .ST (raw, 737 280 bytes), .MSA (compressed, common), .STX (Pasti, preserves copy protection).

## ACSI hard disk port
- 8-bit parallel DMA, 8 Mbit/s, SCSI-bridge controllers. GEMDOS FAT16 partitions (limits depend on TOS version).

## STE DMA sound (STE/Mega STE only)
See 06.
