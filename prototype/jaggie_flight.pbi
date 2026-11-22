; jaggie_flight.pb - PureBasic port of the flight routines of jaggie.asm
; (Behind Jaggi Lines! / Rescue on Fractalus).  Same logic as jaggie_flight.py.
;
; Ported from jaggie.asm:
;   L8530  joystick -> turn_offset / pitch acceleration
;   L8651  bank spring-back            L868C  pitch spring-back
;   L86CA  speed (thrust + drag)       L873A  bank integration (clamped)
;   L8778  pitch integration (clamped) L87B6  climb rate
;   heading += bank/16                 L8F67  heading -> velocity vector
;   position / altitude integration    HUD marker positions
;
; Variable formats follow the game: every game variable is one unsigned byte (.a), 16 bit values
; are lo/hi byte pairs exactly as at their 6502 addresses (given in the comments), all 6502
; arithmetic is done on integers and masked back to a byte.  The sine table is the 65 byte
; table at $900E of the game.
;
; Include file: used by jaggie_flight.pb (console demo) and jaggie_3d.pb (renderer).

EnableExplicit

; joystick bits as read from PORTA (a set bit = not pressed)
#UP    = 1
#DOWN  = 2
#LEFT  = 4
#RIGHT = 8

Structure TFlight
  turn.a        ; $2886 turn_offset
  speedCfg.a    ; $2887 thrust value, also scales the turn rate
  pitchAcc.a    ; $2888
  rollLo.a      ; $2889 displayed roll (copy of bank, one tick late)
  rollHi.a      ; $288A
  bankLo.a      ; $288B bank angle, 8.8 signed, -5 .. +4.FF
  bankHi.a      ; $288C
  pitchLo.a     ; $288D pitch ("ship_sink_rate"), 8.8 signed, -12 .. +11.FF
  pitchHi.a     ; $288E
  m2884.a       ; $2884
  velXLo.a      ; $2891
  velXHi.a      ; $2892
  velZLo.a      ; $2893
  velZHi.a      ; $2894
  climbLo.a     ; $2895
  climbHi.a     ; $2896
  speedLo.a     ; $2897
  speedHi.a     ; $2898
  headLo.a      ; $2899 heading, a full turn = $4000
  headHi.a      ; $289A
  posXLo.a      ; $289B
  posXHi.a      ; $289C
  posZLo.a      ; $289D
  posZHi.a      ; $289E
  altLo.a       ; $289F
  altHi.a       ; $28A0
  markRoll.a    ; $2878 HUD marker
  markPitch.a   ; $287A HUD marker
  f0.a          ; $28F0
  f1.a          ; $28F1
  f2.a          ; $28F2
  f3.a          ; $28F3
  m0626.a       ; mode flags: normal flight is $0626=2, $28A5=0, $061C=0, $065F<>0/2, $28A4=0
  m28A5.a
  m061C.a
  m065F.a
  m28A4.a
  m293D.a
  m283F.a
  m0625.a       ; <> 0: a ground hit costs speed
  m061D.a       ; height above ground (altitude - ground height), 0 after a ground hit
EndStructure

; --- the 16x16 terrain heightmap of the game ($0900); bit 7 set = high mountain.  Used by the ground collision here
; and by the renderer in jaggie_3d.pb.
Global Dim heightmap.a(255)

; --- sine table of the game ($900E), 65 entries -----------------------------------------------
Global Dim sintable.a(64)

DataSection
  SinData:
  Data.a $00,$06,$0C,$12,$18,$1F,$25,$2B,$31,$37,$3D,$44,$4A,$4F,$55,$5B
  Data.a $61,$67,$6D,$72,$78,$7D,$83,$88,$8D,$92,$97,$9C,$A1,$A6,$AB,$AF
  Data.a $B4,$B8,$BC,$C1,$C5,$C9,$CC,$D0,$D4,$D7,$DA,$DD,$E0,$E3,$E6,$E9
  Data.a $EB,$ED,$F0,$F2,$F4,$F5,$F7,$F8,$FA,$FB,$FC,$FD,$FD,$FE,$FE,$FE
  Data.a $FF
EndDataSection

Procedure LoadSinTable()
  Protected i.i
  Restore SinData
  For i = 0 To 64
    Read.a sintable(i)
  Next
EndProcedure

; --- 6502 helpers -------------------------------------------------------------------------------
Global carry.i
Global rndState.i = 1
Global useLcg.i = 0

Procedure.i Adc8(a.i, b.i, c.i)
  Protected t.i = a + b + c
  carry = t >> 8
  ProcedureReturn t & $FF
EndProcedure

Procedure.i Sbc8(a.i, b.i, c.i)
  Protected t.i = a - b - (1 - c)
  If t >= 0
    carry = 1
  Else
    carry = 0
  EndIf
  ProcedureReturn t & $FF
EndProcedure

Procedure.i Rnd8()
  If useLcg
    rndState = (rndState * 5 + 17) & $FF   ; same generator as Lcg in the python port
    ProcedureReturn rndState
  EndIf
  ProcedureReturn Random(255)
EndProcedure

Procedure.i S16(lo.i, hi.i)
  Protected v.i = lo | (hi << 8)
  If v & $8000
    v - 65536
  EndIf
  ProcedureReturn v
EndProcedure

; L8FF9: shift-and-add multiply, result ~ f0*f1/256, rounded the way the 6502 loop does it
Procedure.i Mul8(f0.i, f1.i)
  Protected a.i = 0
  Protected c.i
  Repeat
    c = f0 >> 7
    f0 = (f0 << 1) & $FF
    If c
      c = f1 & 1
      f1 = f1 >> 1
      a = (a + f1 + c) & $FF
    Else
      f1 = f1 >> 1
      If f1 = 0
        ProcedureReturn a
      EndIf
    EndIf
  ForEver
EndProcedure

; --- L8F67: heading -> velocity vector (X in $2891/92, Z in $2893/94) = speed * sin/cos ------------
Procedure Velocity(*f.TFlight)
  Protected idx.i, b7.i, b6.i, vxh.i, vzh.i, i6.i, f2.i, f3.i, a.i
  idx = ((*f\headHi << 2) | (*f\headLo >> 6)) & $FF      ; 0..255 per turn
  b7 = idx >> 7
  b6 = (idx >> 6) & 1
  vxh = 0
  If b7 : vxh = $FF : EndIf
  If b6 : vzh = $FF : Else : vzh = 0 : EndIf
  vzh = vzh ! vxh
  i6 = idx & $3F
  f2 = i6
  f3 = i6
  If b6
    f3 = (i6 ! $3F) + 1                                   ; 64 - i6
  Else
    f2 = (i6 ! $3F) + 1
  EndIf
  *f\f2 = f2
  *f\f3 = f3

  a = Mul8(*f\speedHi, sintable(f3))
  *f\velXLo = a
  If a = 0 : vxh = 0 : EndIf
  If vxh : *f\velXLo = (vxh - a - 1) & $FF : EndIf
  *f\velXHi = vxh

  a = Mul8(*f\speedHi, sintable(f2))
  *f\velZLo = a
  If a = 0 : vzh = 0 : EndIf
  If vzh : *f\velZLo = (vzh - a - 1) & $FF : EndIf
  *f\velZHi = vzh
EndProcedure

; --- L8BDC: one tick of the landing descent ($065F = 0, key L) ----------------------------------------
; The displayed roll decays towards 0 by $14, the altitude drops by $30 per tick while there is ground
; clearance ($061D, set by the ground check); at $061D = 0 the ship touches down ($28A4 becomes 1).
Procedure LandStep(*f.TFlight)
  Protected hi.i = *f\rollHi, lo.i = *f\rollLo, v.i, alt.i
  If hi & $80                                              ; negative roll: add $14 (no clamp)
    lo + $14
    If lo > 255 : hi = (hi + 1) & $FF : EndIf
    lo = lo & $FF
  Else                                                     ; positive roll: subtract $14, stop at 0
    v = lo - $14
    If v < 0
      v = v & $FF
      hi - 1
    EndIf
    If hi < 0
      hi = 0 : v = 0
    EndIf
    lo = v
  EndIf
  *f\rollHi = hi : *f\rollLo = lo
  If *f\m061D
    alt = ((*f\altHi << 8) | *f\altLo) - $30
    *f\altLo = alt & $FF
    *f\altHi = (alt >> 8) & $FF
  ElseIf *f\m28A4 = 0                                      ; ($067A is 0 in flight)
    *f\m28A4 = 1
  EndIf
EndProcedure

; Key L (keyboard handler L49A0 -> L4AC2): start the landing, only below $64 height units of clearance.
Procedure.i KeyLand(*f.TFlight)
  If *f\m0625 = 0 Or *f\m28A4 Or *f\m061C Or *f\m065F = 0 : ProcedureReturn 0 : EndIf
  If *f\m061D >= $64                                       ; "too high": the landing is refused
    *f\m065F = $FF
    ProcedureReturn 0
  EndIf
  *f\m065F = 0                                             ; landing mode; throttle 8 = thrust 0
  *f\speedCfg = 0
  ProcedureReturn 1
EndProcedure

; Key * while landed (L4A65, cpy #4): clear the landed flag, cruise thrust, back to normal flight.
Procedure.i KeyTakeoff(*f.TFlight)
  If *f\m0625 = 0 Or *f\m28A4 = 0 : ProcedureReturn 0 : EndIf
  If *f\m28A5 = 0 : *f\m28A4 = 0 : EndIf
  *f\bankLo = 0 : *f\bankHi = 0
  *f\speedCfg = $1E
  *f\m065F = $FF
  ProcedureReturn 1
EndProcedure

; --- one flight tick (L8530).  The game runs it every second frame (VBI counter $0642). -------------
; porta: joystick byte (bit set = not pressed).  When useOverride is set the speed update is
; replaced by (ovLo, ovHi), as the validation against the emulator does.
; Returns 0, or 1 if the game is in a mode that is not ported (autopilot, the $065F = 2 sequence).
Procedure.i FlightTick(*f.TFlight, porta.i, useOverride.i = 0, ovLo.i = 0, ovHi.i = 0)
  Protected a.i, c.i, t.i, v.i, bc.i, bd.i, y.i, i.i, carryOut.i, lo.i, hi.i, acc.i, turn.i, sh.i, bank.i
  Protected half.i, bcv.i

  ; ---- L8530: stick -> turn_offset and pitch acceleration
  If *f\m0626 = 2 And *f\m28A5 = 0
    a = *f\speedCfg
    t = ((a >> 2) + $20 + ((a >> 1) & 1)) & $FF
    If (porta & #LEFT) = 0
      t = t ! $FF
    ElseIf porta & #RIGHT
      t = 0
    EndIf
    *f\turn = t
    If *f\m293D = 0
      v = $D0
    ElseIf (porta & #UP) = 0
      v = $D0
      If *f\pitchHi = $F4 And *f\pitchLo = 0 : v = $FF : EndIf
    ElseIf (porta & #DOWN) = 0
      v = $30
      If *f\pitchHi = $0B And *f\pitchLo = $FF : v = $01 : EndIf
    Else
      v = 0
    EndIf
    *f\pitchAcc = v
  EndIf

  ; ---- L85A7 .. L8636: mode checks (these modes are not ported)
  If *f\m061C Or *f\m065F = 2
    ProcedureReturn 1
  EndIf
  If *f\m28A4
    ; L861D: landed, only graphics; the roll is kept
  ElseIf *f\m065F = 0
    LandStep(*f)                                           ; L8BDC: landing descent
  Else
    *f\rollLo = *f\bankLo                                  ; L860E
    *f\rollHi = *f\bankHi
  EndIf
  If *f\m28A5
    ProcedureReturn 0                                      ; L8636: crashed, no physics
  EndIf

  ; ---- L8651: bank spring-back when the stick is centred
  If *f\turn = 0
    bc = *f\bankHi
    a = *f\bankLo
    For i = 1 To 5
      c = a >> 7
      a = (a << 1) & $FF
      carryOut = bc >> 7
      bc = ((bc << 1) | c) & $FF
    Next
    a = *f\bankLo
    If carryOut
      a = Sbc8(a, bc, 1)
      *f\bankLo = a
      If carry : *f\bankHi = (*f\bankHi + 1) & $FF : EndIf
    Else
      a = Sbc8(a, bc, 0)
      If carry = 0
        *f\bankHi = (*f\bankHi - 1) & $FF
        If *f\bankHi & $80
          a = 0
          *f\bankHi = 0
        EndIf
      EndIf
      *f\bankLo = a
    EndIf
  EndIf

  ; ---- L868C: pitch spring-back
  If *f\m28A4 = 0 And *f\pitchAcc = 0
    bc = *f\pitchHi
    a = *f\pitchLo
    For i = 1 To 2
      c = a >> 7
      a = (a << 1) & $FF
      carryOut = bc >> 7
      bc = ((bc << 1) | c) & $FF
    Next
    a = *f\pitchLo
    If carryOut
      a = Sbc8(a, bc, 1)
      *f\pitchLo = a
      If carry : *f\pitchHi = (*f\pitchHi + 1) & $FF : EndIf
    Else
      a = Sbc8(a, bc, 0)
      If carry = 0
        *f\pitchHi = (*f\pitchHi - 1) & $FF
        If *f\pitchHi & $80
          a = 0
          *f\pitchHi = 0
        EndIf
      EndIf
      *f\pitchLo = a
    EndIf
  EndIf

  ; ---- L86CA: speed (thrust with random jitter, then drag)
  y = 1
  If *f\speedCfg = 0
    y = 3
  Else
    a = Sbc8(*f\speedCfg, *f\pitchHi, 1)
    a = Sbc8(a, *f\pitchHi, carry)
    bcv = a
    half = *f\speedCfg >> 1
    c = *f\speedCfg & 1
    a = (half | 7) & Rnd8()
    a = Adc8(a, bcv, c)
    If carry : a = $FF : EndIf
    lo = Adc8(a, *f\speedLo, 0)
    *f\speedLo = lo
    If carry : *f\speedHi = (*f\speedHi + 1) & $FF : EndIf
  EndIf
  If *f\m293D                                              ; L86FB: drag
    t = *f\speedHi + 2
    bc = t & $FF
    bd = t >> 8
    a = *f\speedLo
    carryOut = 0
    For i = 1 To y
      c = a >> 7
      a = (a << 1) & $FF
      v = bc >> 7
      bc = ((bc << 1) | c) & $FF
      carryOut = bd >> 7
      bd = ((bd << 1) | v) & $FF
    Next
    a = Sbc8(*f\speedLo, bc, carryOut)
    *f\speedLo = a
    a = Sbc8(*f\speedHi, bd, carry)
    *f\speedHi = a
    If carry = 0
      *f\speedLo = 0
      *f\speedHi = 0
    EndIf
  EndIf
  If useOverride
    *f\speedLo = ovLo
    *f\speedHi = ovHi
  EndIf

  ; ---- L873A: bank integration, clamped to -5 .. +4.FF
  turn = *f\turn
  lo = *f\bankLo + turn
  c = lo >> 8
  *f\bankLo = lo & $FF
  If turn & $80
    If c = 0
      *f\bankHi = (*f\bankHi - 1) & $FF
      If (*f\bankHi & $80) And *f\bankHi < $FB
        *f\bankHi = (*f\bankHi + 1) & $FF
        *f\bankLo = 0
      EndIf
    EndIf
  ElseIf c
    *f\bankHi = (*f\bankHi + 1) & $FF
    If (*f\bankHi & $80) = 0 And *f\bankHi >= 5
      *f\bankHi = (*f\bankHi - 1) & $FF
      *f\bankLo = $FF
    EndIf
  EndIf

  ; ---- L8778: pitch integration, clamped to -12 .. +11.FF
  acc = *f\pitchAcc
  lo = *f\pitchLo + acc
  c = lo >> 8
  *f\pitchLo = lo & $FF
  If acc & $80
    If c = 0
      *f\pitchHi = (*f\pitchHi - 1) & $FF
      If (*f\pitchHi & $80) And *f\pitchHi < $F4
        *f\pitchHi = (*f\pitchHi + 1) & $FF
        *f\pitchLo = 0
      EndIf
    EndIf
  ElseIf c
    *f\pitchHi = (*f\pitchHi + 1) & $FF
    If (*f\pitchHi & $80) = 0 And *f\pitchHi >= $0C
      *f\pitchHi = (*f\pitchHi - 1) & $FF
      *f\pitchLo = $FF
    EndIf
  EndIf

  ; ---- L87B6: climb = speed * pitch
  hi = *f\pitchHi
  a = *f\pitchLo
  For i = 1 To 3
    c = a >> 7
    a = (a << 1) & $FF
    carryOut = hi >> 7
    hi = ((hi << 1) | c) & $FF
  Next
  *f\m2884 = hi
  a = hi
  If carryOut
    a = ((a ! $FF) + 1) & $FF
  EndIf
  *f\f0 = a
  *f\f1 = *f\speedHi
  a = Mul8(*f\f0, *f\f1)
  y = 0
  If *f\m2884 & $80
    y = $FF
    a = (a ! $FF) + 1
    If a > 255 : y = 0 : EndIf
    a = a & $FF
  EndIf
  hi = y
  For i = 1 To 3
    c = a >> 7
    a = (a << 1) & $FF
    hi = ((hi << 1) | c) & $FF
  Next
  *f\climbHi = hi
  *f\climbLo = a

  ; ---- heading += bank / 16 (arithmetic shift of the 16 bit bank), wrap at $4000
  bank = S16(*f\bankLo, *f\bankHi)
  sh = bank >> 4
  lo = Adc8(*f\headLo, sh & $FF, 0)
  *f\headLo = lo
  hi = Adc8(*f\headHi, (sh >> 8) & $FF, carry)
  *f\headHi = hi & $3F

  Velocity(*f)                                             ; L8F67

  ; ---- position += velocity
  lo = Adc8(*f\posXLo, *f\velXLo, 0)
  *f\posXLo = lo
  *f\posXHi = Adc8(*f\posXHi, *f\velXHi, carry)
  lo = Adc8(*f\posZLo, *f\velZLo, 0)
  *f\posZLo = lo
  *f\posZHi = Adc8(*f\posZHi, *f\velZHi, carry)

  ; ---- altitude += climb, clamped
  lo = Adc8(*f\altLo, *f\climbLo, 0)
  *f\altLo = lo
  a = Adc8(*f\altHi, *f\climbHi, carry)
  If a = $FF : a = 0 : EndIf
  If a >= $50
    If *f\m065F <> 2
      *f\altLo = $FF
      a = $4F
    ElseIf a >= $60
      *f\m283F = $FF
    EndIf
  EndIf
  *f\altHi = a

  ; ---- HUD markers (rounded)
  *f\markPitch = (*f\pitchHi + $0C + (*f\pitchLo >> 7)) & $FF
  *f\markRoll = (*f\rollHi + 5 + (*f\rollLo >> 7)) & $FF
  ProcedureReturn 0
EndProcedure

; --- ground collision: L99C2 (terrain height under the camera) and L97A9 (response) -----------------------
; The game never lets the camera into a mountain: every flight tick it looks up the interpolated terrain height at the
; camera and, if the altitude is below it, lifts the camera, levels the nose and slows down.

; L99C2 inner loop: 8 step shift-and-add interpolation, MSB of f first; ~ (a*(255-f) + b*f) / 256
Procedure.i Lerp8(a.i, b.i, f.i)
  Protected acc.i = 0, k.i, bit.i, c.i
  For k = 1 To 8
    bit = (f >> 7) & 1
    f = (f << 1) & $FF
    If bit = 0
      b = b >> 1
      c = a & 1
      a = a >> 1
      acc = (acc + a + c) & $FF
    Else
      a = a >> 1
      c = b & 1
      b = b >> 1
      acc = (acc + b + c) & $FF
    EndIf
  Next
  ProcedureReturn acc
EndProcedure

; L99C2: bilinear terrain height at the camera position, scaled by about 1.09 (the game's $2801)
Procedure.i GroundHeight(*f.TFlight)
  Protected posX.i, posZ.i, frX.i, cX.i, frZ.i, cZ.i, row.i, row2.i, i00.i, i10.i
  Protected h00.i, h01.i, h10.i, h11.i, a0.i, y.i, t.i
  posX = *f\posXLo + *f\posXHi * 256
  posZ = *f\posZLo + *f\posZHi * 256
  frX = (posX >> 4) & $FF : cX = (posX >> 12) & $FF           ; $27FD / $27FE
  frZ = (posZ >> 4) & $FF : cZ = (posZ >> 12) & $FF           ; $27FF / $2800
  row = (cZ << 4) & $FF
  i00 = (cX & 15) | row
  h00 = heightmap(i00)
  h01 = heightmap(((i00 + 1) & 15) | row)
  row2 = (row + 16) & $FF
  i10 = (cX & 15) | row2
  h10 = heightmap(i10)
  h11 = heightmap(((i10 + 1) & 15) | row2)
  a0 = Lerp8(Lerp8(h00, h01, frX), Lerp8(h10, h11, frX), frZ)
  y = a0 >> 4
  t = (a0 + y) & $FF
  ProcedureReturn (t + (y >> 1) + (y & 1)) & $FF
EndProcedure

; L97A9 (runs right after the flight tick L8530).  Returns 1 on a ground hit.
Procedure.i GroundCollision(*f.TFlight)
  Protected c.i, alt.i, g.i
  If *f\altHi >= $40
    c = $3F
  Else
    c = *f\altHi
  EndIf
  alt = ((c << 2) | (*f\altLo >> 6)) & $FF                  ; $27FC: altitude in height units
  g = GroundHeight(*f)
  If alt >= g
    *f\m061D = alt - g
    ProcedureReturn 0
  EndIf
  If *f\m065F <> 0 And *f\m0625 <> 0 And *f\speedHi <> 0    ; L9AA5: speed high byte - 1
    *f\speedHi = *f\speedHi - 1
  EndIf
  *f\altHi = (*f\altHi + 1) & $FF                           ; lift by one high byte step (4 height units)
  *f\altLo = 0
  *f\m061D = 0
  If *f\pitchHi & $80                                       ; nose down: push the pitch up one step
    *f\pitchHi = (*f\pitchHi + 1) & $FF
    *f\pitchLo = 0
  EndIf
  ProcedureReturn 1
EndProcedure

; --- a level-flight state in normal flight mode (the values of jaggie.atstate2) --------------------
Procedure NewFlight(*f.TFlight)
  ClearStructure(*f, TFlight)
  *f\m0626 = 2
  *f\m065F = $FF
  *f\m293D = $FF
  *f\speedCfg = 30
  *f\speedLo = 64
  *f\speedHi = 18
  *f\altHi = $24
  *f\m0625 = 219
EndProcedure

Procedure.s H2(v.i)
  ProcedureReturn RSet(Hex(v & $FF), 2, "0")
EndProcedure

Procedure.s TraceLine(*f.TFlight)
  ProcedureReturn H2(*f\bankHi) + " " + H2(*f\bankLo) + " " + H2(*f\pitchHi) + " " + H2(*f\pitchLo) + " " +
                  H2(*f\headHi) + " " + H2(*f\headLo) + " " + H2(*f\altHi) + " " + H2(*f\altLo) + " " +
                  H2(*f\speedHi) + " " + H2(*f\speedLo) + " " + H2(*f\posXHi) + " " + H2(*f\posXLo) + " " +
                  H2(*f\posZHi) + " " + H2(*f\posZLo) + " " + H2(*f\velXHi) + " " + H2(*f\velXLo) + " " +
                  H2(*f\velZHi) + " " + H2(*f\velZLo)
EndProcedure

Procedure.s Describe(*f.TFlight)
  Protected iBank.i = S16(*f\bankLo, *f\bankHi)
  Protected iPitch.i = S16(*f\pitchLo, *f\pitchHi)
  Protected iHead.i = *f\headLo + *f\headHi * 256
  Protected iAlt.i = *f\altLo + *f\altHi * 256
  Protected iSpeed.i = *f\speedLo + *f\speedHi * 256
  Protected bank.f = iBank / 256.0
  Protected pitch.f = iPitch / 256.0
  Protected head.f = iHead * 360.0 / 16384.0
  Protected alt.f = iAlt / 256.0
  Protected speed.f = iSpeed / 256.0
  ProcedureReturn "bank=" + StrF(bank, 2) + " pitch=" + StrF(pitch, 2) + " heading=" + StrF(head, 2) +
                  "deg alt=" + StrF(alt, 1) + " speed=" + StrF(speed, 2)
EndProcedure

