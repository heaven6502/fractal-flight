; jaggie_3d_16bit.pb - jaggie_3d.pb with a 16 / 32 bit extension for the 68000 (Atari ST / Amiga): see the block '16 BIT EXTENSION' below and jaggie_16bit_analysis.md.
; Keys in the window: 1 .. 6 switch the wide stages (position fraction, heading / sine, perspective + ramp, ridge lines, flight velocity, ground collision), 0 = the original 8 bit game, 9 = everything wide.
; Options: --wide n (flags of the stages, 63 = all)   --analyse (console report of the precision loss)
; Compile:  pbcompiler jaggie_3d_16bit.pb /CONSOLE /EXE jaggie_3d_16bit.exe
;
; jaggie_3d.pb - PureBasic port of the 3D terrain view of jaggie.asm (Behind Jaggi Lines!)
; together with the flight model of jaggie_flight.pbi.  Arrow keys fly, Esc quits.
;
; What is ported (names of the game routines in brackets):
;   heightmap generation        [init_grid_map, set_high_mountains, L84E6]   same algorithm, RANDOM = Random()
;   camera + sin/cos            [LAD6C, L9B3C, L9B80, L9C0E]   16 bit sine tables ($900E/$904F), 3 step interpolation
;   rotated vertex grid         [LA0DB, update_tables]  same walk tables, same 16 bit add/sub per step
;   height / dy                 [update_tables $9FEA]
;   frustum flags, near fix-up  [LA01E, LA080]
;   perspective                 [LA157]  integer, same doubling/shift scheme; the 8 bit quotient is
;                                        computed by integer division instead of the restoring loop
;   roll / pitch horizon ramp   [LAE0A, LA315]
;   edge list, trivial reject   [LA3E7]
;   edge clipping + bisection   [LB24D, LB3A7]  16 bit, incl. the fractal displacement
;   fractal line + height buffer [set_pixel_line, LB45D, LB51C]  byte routine with the zero page stacks;
;                                        a pixel is plotted only if it is higher than what line_pixel_height_buffer
;                                        ($2608) already holds for that column (so interior lines mostly vanish)
;   sky fill                    [render_sky]
; Everything is in the game's own units: 16 bit signed world/camera values, 8 bit heights, screen
; columns 44..211 and ridge values $68..$97 (48 rows, $68 = bottom row).
;
;   ground collision           [L99C2, L97A9]  in jaggie_flight.pbi: terrain height at the camera, lift / slow down
;
; Compile:  pbcompiler jaggie_3d.pb /EXE jaggie_3d.exe
; Options:  --shot out.png [--state state.txt] [--ticks n] [--stick left|right|up|down]  (no window)
;           --seed n  --cam alt16 heading16 pitch16 posx posz  --save state.txt   (random terrain / camera for tests)

EnableExplicit
IncludeFile "jaggie_flight_16bit.pbi"
IncludeFile "jaggie_data.pbi"

#VIEW_W = 168            ; screen columns 44 .. 211 (42 bytes of 4 pixels)
#VIEW_H = 48             ; 48 mode D rows: ridge value Y = $68 (bottom row) .. $97 (top row)
#Y_MIN  = $68
#Y_MAX  = $97
#X_MIN  = 44
#X_MAX  = 211
#SX     = 3              ; screen pixel is 1 colour clock wide x 2 scanlines tall
#SY     = 4
#COCKPIT_ROWS = 5        ; bottom rows (ridge Y <= $6C) are covered by the cockpit bar in the game: 43 of 48 rows are visible

Global Dim sinlo.a(64)            ; $904F  low bytes of the sine table
Global Dim walk.a(179)            ; $B6A3  4 tables of 45
Global Dim edges.a(143)           ; $B757  72 pairs
Global Dim stk.i(255)             ; $24BE  recursion stack of the terrain generator
Global Dim vx.i(45)               ; $229D/$22CB  camera space X (index 0 = walk origin)
Global Dim vz.i(45)               ; $22F9/$2327  camera space Z
Global Dim vcell.i(44)            ; $2270  heightmap index of the vertex
Global Dim vh.i(44)               ; $23AF  raw height
Global Dim vdy.i(44)              ; $2355/$2382  (height - altitude) >> 4
Global Dim vflag.i(44)            ; $2490  $80 near, $40 left, $20 right
Global Dim px.i(44)               ; $23DC/$2409  projected X
Global Dim py.i(44)               ; $2436/$2463  projected Y
Global Dim pdone.i(44)
Global Dim ramp.i(31)             ; $2708  horizon ramp (roll / pitch)
Global Dim scr.a(#VIEW_W * #VIEW_H - 1)
Global lineSteps.i, lineCulled.i                      ; loop steps of the byte line routines and pieces skipped by #W_CULL
Global Dim lineBuf.i(255)         ; $2608  line_pixel_height_buffer: highest ridge value plotted per column
Global Dim ZX.i(63)               ; zero page stack of the byte line routine: $95+Y  x
Global Dim ZY.i(63)               ;                                        $E3+Y  y
Global Dim ZH.i(63)               ;                                        $ED+Y  h
Global Dim sx.i(15)               ; $2572/$2590  outer stack of LB24D, x
Global Dim sy.i(15)               ; $25AE/$25CC  y
Global Dim sh.i(15)               ; $25EA        h
Global z82.i, z84.i, z86.i        ; end 1 of the byte routine: x, y, h
Global e1x.i, e1y.i, e1h.i        ; end 1 of LB24D ($82/$83, $84/$85, $86)
Global mx.i, my.i, mh.i           ; LB3A7 result ($8D/$8E, $8F/$90, $91)
Global cnt9F.i

Global hm98.i, hm9C.i, hm9A.i, hm9B.i
Global dbgFile.i = 0

; --- small helpers -------------------------------------------------------------------------------
Procedure.i W16(v.i)
  v = v & $FFFF
  If v & $8000
    v - 65536
  EndIf
  ProcedureReturn v
EndProcedure

; L9CCF: A (unsigned 8 bit) * m (signed 16 bit) >> 8, the 6502 shift-and-add loop
Procedure.i MulS(a.i, m.i)
  Protected mag.i = m, neg.i = 0, acc.i = 0, ad.i = a & $FF, c.i, lsb.i, k.i
  If m < 0
    mag = -m
    neg = 1
  EndIf
  mag = mag & $FFFF
  c = ad & 1
  ad = ad >> 1
  If c
    acc = mag
  EndIf
  For k = 1 To 7
    lsb = acc & 1
    acc = acc >> 1
    c = ad & 1
    ad = (ad >> 1) | (lsb << 7)
    If c
      acc = (acc + mag) & $FFFF
    EndIf
  Next
  acc = acc >> 1
  If neg
    acc = (-acc) & $FFFF
  EndIf
  ProcedureReturn W16(acc)
EndProcedure

; L9C0E: signed 16 bit sine of idx (0..255 per turn), value = hi:lo with hi = sintable ($900E) and lo = the table at
; $904F, mirrored with i6 XOR $3F in the odd quadrants, negated in the lower half (L9B34 / L9B38)
Procedure.i SinV(idx.i)
  Protected q.i, y.i, v.i
  idx = idx & 255
  q = idx >> 6
  y = idx & 63
  If q & 1 : y = y ! $3F : EndIf
  v = sintable(y) * 256 + sinlo(y)
  If q >= 2
    v = -v
  EndIf
  ProcedureReturn v
EndProcedure

; L9B80 + the store in L9B3C: acc24 = B*(8-w) + A*w with B = value(idx), A = value(idx+1); result = acc24 >> 8
Procedure.i SinInterp(idx.i, w.i)
  ProcedureReturn (SinV(idx) * (8 - w) + SinV(idx + 1) * w) >> 8
EndProcedure

; --- terrain generation: init_grid_map ($843C) ------------------------------------------------------
Procedure.i SetHigh()           ; set_high_mountains: 1 if the game's result is negative (all 3 cells unresolved)
  Protected x.i, y.i
  x = (hm9C + hm9A) & $FF
  If (heightmap(x) & $80) = 0
    ProcedureReturn 0
  EndIf
  y = (x + hm9A) & $FF
  If (heightmap(y) & $80) = 0
    ProcedureReturn 0
  EndIf
  x = (x + hm9B) & $FF
  If heightmap(x) & $80
    ProcedureReturn 1
  EndIf
  ProcedureReturn 0
EndProcedure

Procedure PushCell()            ; L8526
  stk(hm98) = hm9C
  hm98 + 1
EndProcedure

Procedure TryDirs()             ; L84E6
  hm9A = $01 : hm9B = $10
  If SetHigh() : PushCell() : EndIf
  hm9A = $FF : hm9B = $F0
  If SetHigh() : PushCell() : EndIf
  hm9A = $10 : hm9B = $FF
  If SetHigh() : PushCell() : EndIf
  hm9A = $F0 : hm9B = $01
  If SetHigh() : PushCell() : EndIf
EndProcedure

Procedure InitGridMap()
  Protected i.i, hm99.i, r.i, x.i, guard.i
  For i = 0 To 255
    heightmap(i) = (Random(255) & $3F) | $80
  Next
  hm98 = 1
  hm9C = Random(255)
  heightmap(hm9C) = Random(255) & $3F
  Repeat
    hm99 = hm98
    TryDirs()
    If hm98 = hm99
      hm98 - 1
      If hm98 = 0
        Break
      EndIf
      hm9C = stk(hm98)
    Else
      guard = 0
      Repeat
        r = Random(255)
        If r & $40
          If r & $80
            hm9A = $F0 : hm9B = $01
          Else
            hm9A = $10 : hm9B = $FF
          EndIf
        Else
          If r & $80
            hm9A = $FF : hm9B = $F0
          Else
            hm9A = $01 : hm9B = $10
          EndIf
        EndIf
        guard + 1
      Until SetHigh() Or guard > 500
      x = (hm9C + hm9A) & $FF
      heightmap(x) = Random(255) & $3F
      x = (x + hm9A) & $FF
      heightmap(x) = Random(255) & $3F
      hm9C = x
    EndIf
  ForEver
EndProcedure

; --- camera and vertex grid: LAD6C, LA0DB, update_tables ----------------------------------------------
Procedure BuildRamp(*f.TFlight)             ; LAD6C (A4/A5/A6) + LAE0A
  Protected a6.i, a4.i, a5.i, b6.i, x.i, t.i, c.i
  a6 = ((*f\pitchHi << 2) | (*f\pitchLo >> 6)) & $FF
  a6 = (6 - a6) & $FF
  a4 = (*f\rollHi >> 1) | (*f\rollHi & $80)
  a5 = (*f\rollLo >> 1) | ((*f\rollHi & 1) << 7)
  ramp(16) = a6
  b6 = 0
  For x = 0 To 14
    t = b6 + a5
    c = t >> 8
    b6 = t & $FF
    ramp(17 + x) = (ramp(16 + x) + a4 + c) & $FF
  Next
  b6 = 0
  For x = 15 To 0 Step -1
    t = b6 - a5
    If t < 0
      c = 1
    Else
      c = 0
    EndIf
    b6 = t & $FF
    ramp(x) = (ramp(x + 1) - a4 - c) & $FF
  Next
EndProcedure

Procedure UpdateTables(*f.TFlight)
  Protected posX.i, posZ.i, alt16.i, frX.i, frZ.i, cellX.i, cellZ.i, c8B.i, c8C.i
  Protected idx.i, w.i, s.i, c.i, quad.i, tbl.i, b7.i, xc.i, y.i, b.i, X.i, Z.i, r0.i, r1.i
  Protected idxh.i, h.i, borrow.i, v.i, dy.i, f.i, ax.i, a.i, i.i, e.i, va.i, vb.i

  posX = *f\posXLo + *f\posXHi * 256
  posZ = *f\posZLo + *f\posZHi * 256
  frX = (posX >> 4) & $FF : cellX = (posX >> 12) & $FF            ; $87 / $88
  frZ = (posZ >> 4) & $FF : cellZ = (posZ >> 12) & $FF            ; $89 / $8A
  If *f\altHi >= $40
    c8C = $FF : c8B = *f\altLo
  Else
    alt16 = (*f\altLo + *f\altHi * 256) << 2
    c8B = alt16 & $FF : c8C = (alt16 >> 8) & $FF
  EndIf

  idx = ((*f\headHi << 2) | (*f\headLo >> 6)) & $FF
  w = (*f\headLo >> 3) & 7
  s = SinInterp(idx, w)                      ; $A0/$A1  (sin)
  c = SinInterp(idx + 64, w)                 ; $A2/$A3  (cos)
  quad = (*f\headHi >> 4) & 3                ; $92 bit 7/6

  vx(0) = W16(MulS(frZ, s) - MulS(frX, c))                 ; LA0DB: walk origin
  vz(0) = W16(-(MulS(frZ, c) + MulS(frX, s)))

  Select quad                                ; walk table of the quadrant
    Case 3 : tbl = 0
    Case 2 : tbl = 45
    Case 0 : tbl = 90
    Default : tbl = 135
  EndSelect
  b7 = (cellZ << 4) & $F0
  xc = cellX & $0F

  For y = 0 To 44
    b = walk(tbl + y)
    X = vx(y) : Z = vz(y)
    If b & $80                                           ; row -1
      X + s : Z - c : b7 = (b7 + $F0) & $FF
      r0 = b & $40 : r1 = b & $20
    ElseIf b & $40                                       ; row +1
      X - s : Z + c : b7 = (b7 + $10) & $FF
      r0 = b & $20 : r1 = b & $10
    Else
      r0 = b & $20 : r1 = b & $10
    EndIf
    If r0                                                ; column -1
      X - c : Z - s : xc = (xc - 1) & $FF
    ElseIf r1                                            ; column +1
      X + c : Z + s : xc = (xc + 1) & $FF
    EndIf
    vx(y + 1) = W16(X) : vz(y + 1) = W16(Z)

    idxh = b7 | (xc & $0F)
    vcell(y) = idxh
    h = heightmap(idxh)
    vh(y) = h
    v = h * 256 - (c8C * 256 + c8B)                      ; (h - altitude) as 8.8, then >> 4 (L9FEA)
    vdy(y) = v >> 4                                      ; sign comes from the borrow, result is sign extended: no 16 bit wrap

    Z = vz(y + 1) : X = vx(y + 1)                        ; LA01E: frustum flag
    If Z < 6
      f = $80
    ElseIf X < 0
      If -X < Z : f = 0 : Else : f = $40 : EndIf
    Else
      If X < Z : f = 0 : Else : f = $20 : EndIf
    EndIf
    vflag(y) = f
  Next

  For e = 0 To 10 Step 2                                 ; LA080: near-plane fix-up, first 6 edges
    va = edges(e) : vb = edges(e + 1)
    If vflag(va) = $80 And vflag(vb) = 0
      vz(va + 1) = 6 : vflag(va) = 0
    ElseIf vflag(va) = 0 And vflag(vb) = $80
      vz(vb + 1) = 6 : vflag(vb) = 0
    EndIf
  Next
  For i = 0 To 44
    pdone(i) = 0
  Next
  BuildRamp(*f)
EndProcedure

; --- perspective: LA157 --------------------------------------------------------------------------------
Procedure.i ProjCoord(num.i, z.i, neg.i)
  Protected zz.i = z & $FFFF, n.i = 0, q.i, v.i, off.i
  While num >= zz
    zz = (zz << 1) & $FFFF
    n + 1
    If n = 8
      If neg
        ProcedureReturn -16384
      EndIf
      ProcedureReturn 16384
    EndIf
  Wend
  q = (num << 8) / zz                                    ; 8 bit quotient, L9DA7
  If n = 0
    off = (((q >> 1) + q + 1) >> 1) >> 1
  Else
    v = q << n
    off = (v + (v >> 1)) >> 2
  EndIf
  If neg
    ProcedureReturn $80 - off
  EndIf
  ProcedureReturn $80 + off
EndProcedure

Procedure ProjectVertex(i.i)
  Protected X.i, Z.i, dy.i, xs.i, ys.i, r.i
  If pdone(i)
    ProcedureReturn
  EndIf
  pdone(i) = 1
  X = vx(i + 1) : Z = vz(i + 1) : dy = vdy(i)
  xs = ProjCoord(Abs(X), Z, Bool(X < 0))
  ys = ProjCoord(Abs(dy), Z, Bool(dy < 0))
  If xs < 0
    r = ramp(0)
  ElseIf xs >= 256
    r = ramp(31)
  Else
    r = ramp(xs >> 3)
  EndIf
  If r & $80
    r - 256
  EndIf
  px(i) = xs
  py(i) = ys + r                                        ; LA315
EndProcedure

;==============================================================================================
; CLIPPING IN Y - there is no geometric clip against the top or bottom of the view.  Four stages:
;   1. DrawEdge (LB24D, LB2EC..LB338): 16 bit cull / subdivide with the threshold $6C
;        both ends below $6C             -> skip the piece
;        one end below, one above:       -> piece < $14 columns wide: draw as it is
;                                           wider: draw only if (low end - width/4) >= 0, else subdivide
;        top end >= 256                  -> draw (no test against the top of the view here)
;   2. DrawEdge (LB356): both end Y values are saturated to a byte (>255 -> $FF, <0 -> 0)
;   3. Line51C / SetPixelLine (LB45D, LB51C): 8 bit arithmetic, adding the displacement saturates
;        at $FF, subtracting it wraps without a clamp
;   4. PlotCol: a pixel is plotted only if Y > lineBuf(x); lineBuf starts at $6B ($67 at the corners)
;        = the bottom clip; Y >= $97 is drawn on the top row $97 and closes the column (lineBuf = $FF)
;==============================================================================================

; --- ridge buffer and pixel plotting (set_pixel_line, the repeated block in LB51C) ----------------------
Procedure InitLineBuf()          ; init_line_height_buffer + the presets of init_pixel_to_collum_table
  Protected i.i
  For i = 0 To 255 : lineBuf(i) = $FF : Next
  For i = 44 To 63 : lineBuf(i) = $67 : Next
  For i = 64 To 195 : lineBuf(i) = $6B : Next
  For i = 196 To 211 : lineBuf(i) = $67 : Next
EndProcedure

Procedure PlotPixel(x.i, y.i)    ; ora bitmask,X at (row address of y) + LBD00,X
  If x >= #X_MIN And x <= #X_MAX And y >= #Y_MIN And y <= #Y_MAX
    scr((#Y_MAX - y) * #VIEW_W + (x - #X_MIN)) = 1
  EndIf
EndProcedure

Procedure PlotCol(x.i, y.i)      ; compare with the buffer, store, plot only if higher than anything before
  ; stage 4 of the Y clipping: lineBuf(x) is the floor ($6B / $67) or the highest pixel so far.
  ; top clip: $97 or more -> top row $97 and the column is closed with $FF (nothing can be higher)
  If y <= lineBuf(x)
    ProcedureReturn
  EndIf
  lineBuf(x) = y
  If y >= $97
    lineBuf(x) = $FF
    y = $97
  EndIf
  PlotPixel(x, y)
EndProcedure

; LB51C: the byte line routine.  X = column of end 1 (the stored x of a point is its column + 1),
; stack top = end 2 in ZX/ZY/ZH(yi).  Halves the segment (pushing midpoints) until it is 1 or 2
; columns wide, then plots.  Midpoints get the same fractal displacement as in LB3A7.
Procedure Line51C(X.i, yi.i)
  Protected a.i, t.i, c2.i, b6.i, cH.i, mid.i, guard.i, n.i, y0.i, y1.i, hi.i, lo.i, half.i, bound.i, c.i, e.i, culled.i
  Repeat
    guard + 1
    If guard > 4000 : ProcedureReturn : EndIf
    If X >= $D4
      ProcedureReturn
    EndIf
    z82 = X
    lineSteps + 1
    culled = 0
    If wide & #W_CULL                                       ; a piece of 3 or more columns whose columns all hold a ridge value above anything it can draw (max(y0, y1) + n / 2 + 3): skipped, exact
      n = (ZX(yi) - X) & $FF
      If n >= 3
        y0 = z84 : y1 = ZY(yi)
        If y0 > y1 : hi = y0 : lo = y1 : Else : hi = y1 : lo = y0 : EndIf
        half = (n >> 1) + 3
        If lo >= half And hi + half <= 255
          bound = hi + half
          c = X : e = X + n
          While c < e And lineBuf(c) >= bound : c + 1 : Wend
          If c = e
            z84 = ZY(yi) : z86 = ZH(yi) : X = ZX(yi)         ; the state after the piece
            yi - 1
            culled = 1
            lineCulled + 1
            If yi < 0 : ProcedureReturn : EndIf
          EndIf
        EndIf
      EndIf
    EndIf
    If culled = 0
      a = (X - ZX(yi)) & $FF
      If a = $FE                                              ; 2 columns: plot the middle, then the end
        t = ZY(yi) + z84 + 1
        PlotCol(X, (t >> 1) & $FF)
        X + 1
        z84 = ZY(yi)
        PlotCol(X, ZY(yi))
        yi - 1
        If yi < 0 : ProcedureReturn : EndIf
        X + 1
        z86 = ZH(yi + 1)
      ElseIf a = $FF                                          ; 1 column
        z84 = ZY(yi)
        PlotCol(X, ZY(yi))
        yi - 1
        If yi < 0 : ProcedureReturn : EndIf
        X + 1
        z86 = ZH(yi + 1)
      Else                                                    ; LB5E4: push the midpoint
        mid = ((X + ZX(yi)) >> 1) & $FF
        ZX(yi + 1) = mid
        t = z86 + ZH(yi) + 1
        cH = t >> 8
        ZH(yi + 1) = t & $FF
        t = z84 + ZY(yi)
        c2 = t & 1                                            ; carry out of the ror
        a = (t >> 1) & $FF
        If ZH(yi + 1) & $80
          If cH = 0
            b6 = (((mid - z82 - 1) & $FF) >> 1)
            a = (a - b6 - (1 - c2)) & $FF
          Else
            b6 = (((mid - z82) & $FF) >> 1)
            t = a + b6 + c2
            If t > 255
              a = $FF
            Else
              a = t
            EndIf
          EndIf
        EndIf
        ZY(yi + 1) = a
        yi + 1
      EndIf
    EndIf
  ForEver
EndProcedure

; set_pixel_line ($B418): draws end 1 (z82, z84, z86) to the stack top ZX/ZY/ZH(0) in the buffer.
; While end 1 is left of column 44 it is bisected towards the right (LB45D).
Procedure SetPixelLine()
  Protected a.i, mid.i, t.i, cH.i, b6.i, yi.i = 0, guard.i
  a = ZX(0)
  If a < $2D Or a < z82
    ProcedureReturn
  EndIf
  If a = z82                                                ; end 1 and end 2 in the same column
    PlotCol(ZX(0), ZY(0))
    ProcedureReturn
  EndIf
  Repeat                                                    ; LB45F
    guard + 1
    If guard > 4000 : ProcedureReturn : EndIf
    If z82 >= $2C
      Line51C(z82, yi)
      ProcedureReturn
    EndIf
    mid = ((z82 + ZX(yi)) >> 1) & $FF
    If mid <= $2C                                           ; LB474: end 1 moves to the midpoint
      z82 = mid
      t = z86 + ZH(yi) + 1
      cH = t >> 8
      z86 = t & $FF
      t = z84 + ZY(yi)
      a = (t >> 1) & $FF
      If z86 & $80
        b6 = (((ZX(yi) - z82) & $FF) >> 1)
        If cH = 0
          a = (a - b6) & $FF
        Else
          t = a + b6
          If t > 255
            a = $FF
          Else
            a = t
          EndIf
        EndIf
      EndIf
      z84 = a
    Else                                                    ; LB4C2: push the midpoint
      ZX(yi + 1) = mid
      t = z86 + ZH(yi) + 1
      cH = t >> 8
      ZH(yi + 1) = t & $FF
      t = z84 + ZY(yi)
      a = (t >> 1) & $FF
      If ZH(yi + 1) & $80
        b6 = (((mid - z82) & $FF) >> 1)
        If cH = 0
          a = (a - b6) & $FF
        Else
          t = a + b6
          If t > 255
            a = $FF
          Else
            a = t
          EndIf
        EndIf
      EndIf
      ZY(yi + 1) = a
      yi + 1
    EndIf
  ForEver
EndProcedure

; --- LB3A7: midpoint of end 1 (e1) and stack entry xi, with the fractal displacement ------------------
Procedure Mid3A7(xi.i)
  Protected sum.i, d.i
  mx = (e1x + sx(xi) + 1) >> 1
  my = (e1y + sy(xi) + 1) >> 1
  sum = e1h + sh(xi) + 1
  mh = sum & $FF
  If mh & $80
    d = (mx - e1x) >> 1
    If sum > 255
      my = W16(my + d)                                      ; LB3FA
    Else
      my = W16(my - d)                                      ; LB3DA
    EndIf
  EndIf
EndProcedure

Procedure.i Clamp8(y.i)
  If y < 0 : ProcedureReturn 0 : EndIf
  If y > 255 : ProcedureReturn 255 : EndIf
  ProcedureReturn y
EndProcedure

; --- LB24D: clip one projected edge, bisect it in 16 bits, and hand the pieces to the byte routine ------------
Procedure DrawEdge(x1.i, y1.i, h1.i, x2.i, y2.i, h2.i)
  Protected xi.i = 0, action.i, doSub.i, xw.i, b6.i, guard.i
  e1x = x1 : e1y = y1 : e1h = h1
  sx(0) = x2 : sy(0) = y2 : sh(0) = h2
  If e1x >= sx(0)                                           ; back-facing / folded edge
    ProcedureReturn
  EndIf
  cnt9F = $14
  Repeat
    guard + 1
    If guard > 2000 : ProcedureReturn : EndIf
    If e1x < 0                                              ; LB267: left of the screen, bisect
      cnt9F - 1
      If cnt9F < 0 : ProcedureReturn : EndIf
      Mid3A7(xi)
      If mx < 0 Or (mx < 256 And mx < $28)
        e1x = mx : e1y = my : e1h = mh                      ; LB29D: drop the left part
      Else
        xi + 1
        sx(xi) = mx : sy(xi) = my : sh(xi) = mh             ; LB27E: push
        If xi >= $0F : ProcedureReturn : EndIf
      EndIf
    Else
      If e1x >= 256 Or e1x >= $D8 : ProcedureReturn : EndIf ; LB2B4
      Repeat                                                ; LB2BE
        doSub = 0
        action = 0                                          ; 0 draw, 1 skip, 2 subdivide
        If (sx(xi) >> 8) <> 0
          doSub = 1
        Else                                                ; LB2EC: vertical tests (stage 1 of the Y clipping)
          xw = (sx(xi) - e1x) & $FF
          If e1y < $6C
            If sy(xi) < $6C
              action = 1
            ElseIf xw >= $14
              b6 = xw >> 2
              If e1y - b6 < 0 : action = 2 : EndIf
            EndIf
          Else
            If sy(xi) < $6C And xw >= $14
              b6 = xw >> 2
              If sy(xi) - b6 < 0 : action = 2 : EndIf
            EndIf
          EndIf
          If action = 2 : doSub = 1 : EndIf
        EndIf
        If doSub
          cnt9F - 1
          If cnt9F < 0 : ProcedureReturn : EndIf
          Mid3A7(xi)
          xi + 1
          sx(xi) = mx : sy(xi) = my : sh(xi) = mh
          If xi >= $0F : ProcedureReturn : EndIf
        Else
          Break
        EndIf
      ForEver
      If action = 0                                         ; LB356: draw, Y saturated to a byte (stage 2 of the Y clipping)
        z82 = e1x & $FF
        z84 = Clamp8(e1y)
        z86 = e1h
        ZX(0) = sx(xi) & $FF
        ZY(0) = Clamp8(sy(xi))
        ZH(0) = sh(xi)
        SetPixelLine()
      EndIf
      If xi = 0 : ProcedureReturn : EndIf                   ; LB385: pop
      e1x = sx(xi) : e1y = sy(xi) : e1h = sh(xi)
      xi - 1
    EndIf
  ForEver
EndProcedure

;==============================================================================================
; 16 BIT EXTENSION (jaggie_3d_16bit.pb).  The game computes with bytes because the 6502 has no wider registers.  On a 68000 (16 / 32 bit) the same stages can be
; wide.  Every stage that loses precision in the game has a switch (wide, bit flags #W_xxx, keys 1 .. 6 in the window, 0 = the original game path, 9 = everything):
;   #W_POS   camera position inside the cell: 8 bit fraction (posX >> 4 & $FF)  ->  12 bit
;   #W_TRIG  heading: 11 bit angle (idx 8 bit + 3 bit interpolation), sine +-2040 (the 16 bit table >> 5), the table mirrored one step off (i XOR $3F)  ->  14 bit, 16 bit, mirrored right
;   #W_PROJ  perspective: 8 bit quotient, whole pixel vertices, horizon ramp = 32 byte table (8 column steps)  ->  exact 8.8 division, ramp per column
;   #W_LINE  ridge lines: 8 bit y, whole pixel end points, displacement in whole rows  ->  y in 8.8, line through the exact end points sampled at the column centres
;   #W_VEL   flight model: speed 8 bit x sine 8 bit (6 bit table index)  ->  speed 8.8 x 16 bit sine, 8 more position fraction bits
;   #W_GND   ground collision: 8 bit height interpolation, pop up by 4 height units  ->  bilinear in 1/4096 height units, lifted exactly to the ground
; Camera space values (vx16, vz16, vdy16) are 16 times the game's units (4 more fraction bits), projected positions are 8.8 (px16 = columns * 256, py16 = rows * 256).
;==============================================================================================
Procedure.i IAbs(v.i)
  If v < 0 : ProcedureReturn -v : EndIf
  ProcedureReturn v
EndProcedure

Global Dim vx16.i(45)
Global Dim vz16.i(45)
Global Dim vdy16.i(44)
Global Dim px16.i(44)
Global Dim py16.i(44)
Global Dim lineBuf16.i(255)
Global Dim ZY16.i(63)
Global Dim sy16.i(15)
Global z84w.i, e1yw.i, myw.i
Global s16.i, c16.i

; the sine table used the right way round: 65 entries per quadrant, mirrored with 64 - y.  (The game mirrors with y XOR $3F = 63 - y, which is one table step, 1.4 degrees, off in the
; quadrants 1 and 3: the view jumps by 1.4 degrees when the heading crosses 90 / 180 / 270 degrees, and the camera does not look where the velocity goes.)
Procedure.i SinV16(idx.i)
  Protected q.i, y.i, v.i
  idx = idx & 255
  q = idx >> 6
  y = idx & 63
  If q & 1 : y = 64 - y : EndIf
  v = sintable(y) * 256 + sinlo(y)
  If q >= 2 : v = -v : EndIf
  ProcedureReturn v
EndProcedure

; sine of the 14 bit heading ($4000 per turn): linear interpolation between the steps of 1.4 degrees with 6 bit weight, result +-65280 (full 16 bit table, nothing shifted away)
Procedure.i SinHead(head.i)
  Protected idx.i = (head >> 6) & 255, w.i = head & 63
  ProcedureReturn (SinV16(idx) * (64 - w) + SinV16(idx + 1) * w) >> 6
EndProcedure

; exact perspective: 96 * num / z in 8.8 (the game: 96 * (8 bit quotient), see ProjCoord), truncated towards zero, limited to +-16384 pixels like the game's overflow
Procedure.i Proj88(num.i, z.i)
  Protected v.i
  If z < 1 : z = 1 : EndIf
  v = (num * 96 * 256) / z
  If v > 16384 * 256 : v = 16384 * 256 : EndIf
  If v < -16384 * 256 : v = -16384 * 256 : EndIf
  ProcedureReturn v
EndProcedure

Procedure UpdateTables16(*f.TFlight)
  Protected posX.i, posZ.i, alt16.i, frX.i, frZ.i, cellX.i, cellZ.i, c8B.i, c8C.i
  Protected head.i, idx.i, w.i, quad.i, tbl.i, b7.i, xc.i, y.i, b.i, X.i, Z.i, r0.i, r1.i
  Protected idxh.i, h.i, v.i, f.i, i.i, e.i, va.i, vb.i

  posX = *f\posXLo + *f\posXHi * 256
  posZ = *f\posZLo + *f\posZHi * 256
  cellX = (posX >> 12) & $FF
  cellZ = (posZ >> 12) & $FF
  If wide & #W_POS
    frX = posX & $FFF : frZ = posZ & $FFF                       ; 12 bit fraction of the cell
  Else
    frX = ((posX >> 4) & $FF) << 4 : frZ = ((posZ >> 4) & $FF) << 4   ; the game: 8 bit
  EndIf
  If *f\altHi >= $40
    c8C = $FF : c8B = *f\altLo
  Else
    alt16 = (*f\altLo + *f\altHi * 256) << 2
    c8B = alt16 & $FF : c8C = (alt16 >> 8) & $FF
  EndIf

  head = *f\headLo + *f\headHi * 256
  If wide & #W_TRIG
    s16 = SinHead(head) >> 1 : c16 = SinHead(head + $1000) >> 1  ; +-32640 = 16 * the game's +-2040
  Else
    idx = (head >> 6) & 255
    w = (head >> 3) & 7
    s16 = SinInterp(idx, w) << 4 : c16 = SinInterp(idx + 64, w) << 4
  EndIf
  quad = (*f\headHi >> 4) & 3

  vx16(0) = (frZ * s16 - frX * c16) >> 12                       ; walk origin
  vz16(0) = -((frZ * c16 + frX * s16) >> 12)

  Select quad
    Case 3 : tbl = 0
    Case 2 : tbl = 45
    Case 0 : tbl = 90
    Default : tbl = 135
  EndSelect
  b7 = (cellZ << 4) & $F0
  xc = cellX & $0F

  For y = 0 To 44
    b = walk(tbl + y)
    X = vx16(y) : Z = vz16(y)
    If b & $80
      X + s16 : Z - c16 : b7 = (b7 + $F0) & $FF
      r0 = b & $40 : r1 = b & $20
    ElseIf b & $40
      X - s16 : Z + c16 : b7 = (b7 + $10) & $FF
      r0 = b & $20 : r1 = b & $10
    Else
      r0 = b & $20 : r1 = b & $10
    EndIf
    If r0
      X - c16 : Z - s16 : xc = (xc - 1) & $FF
    ElseIf r1
      X + c16 : Z + s16 : xc = (xc + 1) & $FF
    EndIf
    vx16(y + 1) = X : vz16(y + 1) = Z

    idxh = b7 | (xc & $0F)
    vcell(y) = idxh
    h = heightmap(idxh)
    vh(y) = h
    vdy16(y) = h * 256 - (c8C * 256 + c8B)                      ; (height - altitude) in 1/256 height units = 16 * the game's dy (which drops these 4 bits)

    Z = vz16(y + 1) : X = vx16(y + 1)
    If Z < 96
      f = $80
    ElseIf X < 0
      If -X < Z : f = 0 : Else : f = $40 : EndIf
    Else
      If X < Z : f = 0 : Else : f = $20 : EndIf
    EndIf
    vflag(y) = f
  Next

  For e = 0 To 10 Step 2
    va = edges(e) : vb = edges(e + 1)
    If vflag(va) = $80 And vflag(vb) = 0
      vz16(va + 1) = 96 : vflag(va) = 0
    ElseIf vflag(va) = 0 And vflag(vb) = $80
      vz16(vb + 1) = 96 : vflag(vb) = 0
    EndIf
  Next
  For i = 0 To 44
    pdone(i) = 0
  Next
  BuildRamp(*f)
EndProcedure

; vertex i -> px16 / py16 (8.8).  The vertices of the camera space tables are in 16 * game units.
Procedure ProjectVertex16(i.i, *f.TFlight)
  Protected X.i, Z.i, dy.i, xs.i, ys.i, r.i, a6.i, xi.i
  If pdone(i)
    ProcedureReturn
  EndIf
  pdone(i) = 1
  X = vx16(i + 1) : Z = vz16(i + 1) : dy = vdy16(i)
  If wide & #W_PROJ
    xs = Proj88(X, Z) + 128 * 256
    ys = Proj88(dy, Z) + 128 * 256
    a6 = (6 << 8) - S16(*f\pitchLo, *f\pitchHi) * 4             ; pitch: rows * 256
    r = a6 + (((xs - 128 * 256) * S16(*f\rollLo, *f\rollHi)) >> 12)   ; the roll: roll / 4096 rows per column, exact at the column of the vertex
  Else
    xi = ProjCoord(IAbs(X) >> 4, Z >> 4, Bool(X < 0))            ; the game: whole pixels, 8 bit quotient
    ys = ProjCoord(IAbs(dy) >> 4, Z >> 4, Bool(dy < 0)) << 8
    If xi < 0
      r = ramp(0)
    ElseIf xi >= 256
      r = ramp(31)
    Else
      r = ramp(xi >> 3)                                         ; the ramp table: 32 byte entries, one per 8 columns
    EndIf
    If r & $80 : r - 256 : EndIf
    r << 8
    xs = xi << 8
  EndIf
  px16(i) = xs
  py16(i) = ys + r
EndProcedure

Procedure InitLineBuf16()
  Protected i.i
  For i = 0 To 255 : lineBuf16(i) = $FFFF : Next
  For i = 44 To 63 : lineBuf16(i) = $67FF : Next
  For i = 64 To 195 : lineBuf16(i) = $6BFF : Next
  For i = 196 To 211 : lineBuf16(i) = $67FF : Next
EndProcedure

Procedure PlotCol16(x.i, y.i)       ; y 8.8: a pixel is plotted only if it is higher than anything before in the column
  If y <= lineBuf16(x)
    ProcedureReturn
  EndIf
  lineBuf16(x) = y
  If y >= $9700
    lineBuf16(x) = $FFFF
    y = $9700
  EndIf
  PlotPixel(x, y >> 8)
EndProcedure

Procedure.i Clamp16(y.i)
  If y < 0 : ProcedureReturn 0 : EndIf
  If y > $FFFF : ProcedureReturn $FFFF : EndIf
  ProcedureReturn y
EndProcedure

; LB51C with 16 bit y: halves the segment until it is 1 or 2 columns wide.  The displacement of a midpoint is half the column distance in rows, in 1/256 rows (the game: whole rows).
Procedure Line51C16(X.i, yi.i)
  Protected a.i, t.i, c2.i, b6.i, cH.i, mid.i, guard.i, n.i, y0.i, y1.i, hi.i, lo.i, half.i, bound.i, c.i, e.i, culled.i
  Repeat
    guard + 1
    If guard > 4000 : ProcedureReturn : EndIf
    If X >= $D4
      ProcedureReturn
    EndIf
    z82 = X
    lineSteps + 1
    culled = 0
    If wide & #W_CULL                                       ; the same with y in 8.8: bound = max(y0, y1) + (n / 2 + 3) rows
      n = (ZX(yi) - X) & $FF
      If n >= 3
        y0 = z84w : y1 = ZY16(yi)
        If y0 > y1 : hi = y0 : lo = y1 : Else : hi = y1 : lo = y0 : EndIf
        half = ((n >> 1) + 3) << 8
        If lo >= half And hi + half <= $FFFF
          bound = hi + half
          c = X : e = X + n
          While c < e And lineBuf16(c) >= bound : c + 1 : Wend
          If c = e
            z84w = ZY16(yi) : z86 = ZH(yi) : X = ZX(yi)
            yi - 1
            culled = 1
            lineCulled + 1
            If yi < 0 : ProcedureReturn : EndIf
          EndIf
        EndIf
      EndIf
    EndIf
    If culled = 0
      a = (X - ZX(yi)) & $FF
      If a = $FE
        PlotCol16(X, (ZY16(yi) + z84w + 1) >> 1)
        X + 1
        z84w = ZY16(yi)
        PlotCol16(X, ZY16(yi))
        yi - 1
        If yi < 0 : ProcedureReturn : EndIf
        X + 1
        z86 = ZH(yi + 1)
      ElseIf a = $FF
        z84w = ZY16(yi)
        PlotCol16(X, ZY16(yi))
        yi - 1
        If yi < 0 : ProcedureReturn : EndIf
        X + 1
        z86 = ZH(yi + 1)
      Else
        mid = ((X + ZX(yi)) >> 1) & $FF
        ZX(yi + 1) = mid
        t = z86 + ZH(yi) + 1
        cH = t >> 8
        ZH(yi + 1) = t & $FF
        a = (z84w + ZY16(yi)) >> 1
        If ZH(yi + 1) & $80
          If cH = 0
            b6 = ((mid - z82 - 1) & $FF) << 7
            a = a - b6
            If a < 0 : a = 0 : EndIf
          Else
            b6 = ((mid - z82) & $FF) << 7
            a = a + b6
            If a > $FFFF : a = $FFFF : EndIf
          EndIf
        EndIf
        ZY16(yi + 1) = a
        yi + 1
      EndIf
    EndIf
  ForEver
EndProcedure

Procedure SetPixelLine16()
  Protected a.i, mid.i, t.i, cH.i, b6.i, yi.i = 0, guard.i
  a = ZX(0)
  If a < $2D Or a < z82
    ProcedureReturn
  EndIf
  If a = z82
    PlotCol16(ZX(0), ZY16(0))
    ProcedureReturn
  EndIf
  Repeat
    guard + 1
    If guard > 4000 : ProcedureReturn : EndIf
    If z82 >= $2C
      Line51C16(z82, yi)
      ProcedureReturn
    EndIf
    mid = ((z82 + ZX(yi)) >> 1) & $FF
    If mid <= $2C
      z82 = mid
      t = z86 + ZH(yi) + 1
      cH = t >> 8
      z86 = t & $FF
      a = (z84w + ZY16(yi)) >> 1
      If z86 & $80
        b6 = ((ZX(yi) - z82) & $FF) << 7
        If cH = 0
          a = a - b6
          If a < 0 : a = 0 : EndIf
        Else
          a = a + b6
          If a > $FFFF : a = $FFFF : EndIf
        EndIf
      EndIf
      z84w = a
    Else
      ZX(yi + 1) = mid
      t = z86 + ZH(yi) + 1
      cH = t >> 8
      ZH(yi + 1) = t & $FF
      a = (z84w + ZY16(yi)) >> 1
      If ZH(yi + 1) & $80
        b6 = ((mid - z82) & $FF) << 7
        If cH = 0
          a = a - b6
          If a < 0 : a = 0 : EndIf
        Else
          a = a + b6
          If a > $FFFF : a = $FFFF : EndIf
        EndIf
      EndIf
      ZY16(yi + 1) = a
      yi + 1
    EndIf
  ForEver
EndProcedure

Procedure Mid3A7w(xi.i)
  Protected sum.i, d.i
  mx = (e1x + sx(xi) + 1) >> 1
  myw = (e1yw + sy16(xi)) >> 1
  sum = e1h + sh(xi) + 1
  mh = sum & $FF
  If mh & $80
    d = (mx - e1x) << 7
    If sum > 255
      myw = myw + d
    Else
      myw = myw - d
    EndIf
  EndIf
EndProcedure

; LB24D with y in 8.8 (x are whole columns, the sub pixel x is folded into y by the caller)
Procedure DrawEdge16(x1.i, y1.i, h1.i, x2.i, y2.i, h2.i)
  Protected xi.i = 0, action.i, doSub.i, xw.i, b6.i, guard.i
  e1x = x1 : e1yw = y1 : e1h = h1
  sx(0) = x2 : sy16(0) = y2 : sh(0) = h2
  If e1x >= sx(0)
    ProcedureReturn
  EndIf
  cnt9F = $14
  Repeat
    guard + 1
    If guard > 2000 : ProcedureReturn : EndIf
    If e1x < 0
      cnt9F - 1
      If cnt9F < 0 : ProcedureReturn : EndIf
      Mid3A7w(xi)
      If mx < 0 Or (mx < 256 And mx < $28)
        e1x = mx : e1yw = myw : e1h = mh
      Else
        xi + 1
        sx(xi) = mx : sy16(xi) = myw : sh(xi) = mh
        If xi >= $0F : ProcedureReturn : EndIf
      EndIf
    Else
      If e1x >= 256 Or e1x >= $D8 : ProcedureReturn : EndIf
      Repeat
        doSub = 0
        action = 0
        If (sx(xi) >> 8) <> 0
          doSub = 1
        Else
          xw = (sx(xi) - e1x) & $FF
          If e1yw < $6C00
            If sy16(xi) < $6C00
              action = 1
            ElseIf xw >= $14
              b6 = xw << 6
              If e1yw - b6 < 0 : action = 2 : EndIf
            EndIf
          Else
            If sy16(xi) < $6C00 And xw >= $14
              b6 = xw << 6
              If sy16(xi) - b6 < 0 : action = 2 : EndIf
            EndIf
          EndIf
          If action = 2 : doSub = 1 : EndIf
        EndIf
        If doSub
          cnt9F - 1
          If cnt9F < 0 : ProcedureReturn : EndIf
          Mid3A7w(xi)
          xi + 1
          sx(xi) = mx : sy16(xi) = myw : sh(xi) = mh
          If xi >= $0F : ProcedureReturn : EndIf
        Else
          Break
        EndIf
      ForEver
      If action = 0
        z82 = e1x & $FF
        z84w = Clamp16(e1yw)
        z86 = e1h
        ZX(0) = sx(xi) & $FF
        ZY16(0) = Clamp16(sy16(xi))
        ZH(0) = sh(xi)
        SetPixelLine16()
      EndIf
      If xi = 0 : ProcedureReturn : EndIf
      e1x = sx(xi) : e1yw = sy16(xi) : e1h = sh(xi)
      xi - 1
    EndIf
  ForEver
EndProcedure

; one frame with the wide stages (wide & 15 <> 0)
Procedure RenderScene16(*f.TFlight)
  Protected e.i, a.i, b.i, ca.i, cb.i, ya.i, yb.i, dx.i
  FillMemory(@scr(0), #VIEW_W * #VIEW_H, 0)
  InitLineBuf()
  InitLineBuf16()
  UpdateTables16(*f)
  For e = 0 To 142 Step 2
    a = edges(e) : b = edges(e + 1)
    If (vflag(a) & ($80 | $20)) = 0 And (vflag(b) & ($80 | $40)) = 0
      ProjectVertex16(a, *f) : ProjectVertex16(b, *f)
      ca = px16(a) >> 8 : cb = px16(b) >> 8                    ; the pixel columns of the end points
      If ca < cb
        If wide & #W_LINE
          dx = px16(b) - px16(a)                               ; > 0 (the columns differ)
          ya = py16(a) + ((py16(b) - py16(a)) * ((ca << 8) + 128 - px16(a))) / dx   ; the exact line, sampled at the column centres of the 2 ends
          yb = py16(a) + ((py16(b) - py16(a)) * ((cb << 8) + 128 - px16(a))) / dx
          DrawEdge16(ca, ya, vh(a), cb, yb, vh(b))
        Else
          DrawEdge(ca, py16(a) >> 8, vh(a), cb, py16(b) >> 8, vh(b))      ; the game's whole pixel line routine
        EndIf
      EndIf
    EndIf
  Next
EndProcedure

; --- flight model: velocity and ground collision in wide arithmetic (#W_VEL, #W_GND) --------------------------
; position += speed * (sin heading, cos heading): speed 8.8 (speedHi:speedLo) x 16 bit sine / 65536 = position units * 256 (the game: speedHi x 8 bit sine table at a 1.4 degree step)
Procedure Velocity16(*f.TFlight)
  Protected head.i, sp.i, vX.i, vZ.i, pX.i, pZ.i
  head = *f\headLo + *f\headHi * 256
  sp = *f\speedHi * 256 + *f\speedLo
  vX = (sp * SinHead(head)) >> 16
  vZ = (sp * SinHead(head + $1000)) >> 16
  pX = ((*f\posXHi << 16) | (*f\posXLo << 8) | *f\posXfr) + vX
  pZ = ((*f\posZHi << 16) | (*f\posZLo << 8) | *f\posZfr) + vZ
  *f\posXHi = (pX >> 16) & $FF : *f\posXLo = (pX >> 8) & $FF : *f\posXfr = pX & $FF
  *f\posZHi = (pZ >> 16) & $FF : *f\posZLo = (pZ >> 8) & $FF : *f\posZfr = pZ & $FF
  *f\velXLo = (vX >> 8) & $FF : *f\velXHi = (vX >> 16) & $FF
  *f\velZLo = (vZ >> 8) & $FF : *f\velZHi = (vZ >> 16) & $FF
EndProcedure

; terrain height under the camera, bilinear, in 1/4096 height units, scaled by the game's 1.09 (a0 + a0 / 16 + a0 / 32)
Procedure.i GroundHeight4096(*f.TFlight)
  Protected posX.i, posZ.i, frX.i, cX.i, frZ.i, cZ.i, row.i, row2.i, i00.i, i10.i, h00.i, h01.i, h10.i, h11.i, r0.i, r1.i, a0.i
  posX = *f\posXLo + *f\posXHi * 256
  posZ = *f\posZLo + *f\posZHi * 256
  frX = posX & $FFF : cX = (posX >> 12) & $FF
  frZ = posZ & $FFF : cZ = (posZ >> 12) & $FF
  row = (cZ << 4) & $FF
  i00 = (cX & 15) | row
  h00 = heightmap(i00)
  h01 = heightmap(((i00 + 1) & 15) | row)
  row2 = (row + 16) & $FF
  i10 = (cX & 15) | row2
  h10 = heightmap(i10)
  h11 = heightmap(((i10 + 1) & 15) | row2)
  r0 = h00 * 4096 + (h01 - h00) * frX
  r1 = h10 * 4096 + (h11 - h10) * frX
  a0 = (r0 * (4096 - frZ) + r1 * frZ) >> 12
  ProcedureReturn a0 + (a0 >> 4) + (a0 >> 5)
EndProcedure

Procedure.i GroundCollision16(*f.TFlight)
  Protected alt16.i, alt4096.i, g.i
  alt16 = *f\altLo + *f\altHi * 256
  If alt16 >= $4000 : alt16 = $3FFF : EndIf
  alt4096 = alt16 * 64                                          ; altitude in 1/4096 height units (alt16 / 64 height units)
  g = GroundHeight4096(*f)
  If alt4096 >= g
    *f\m061D = (alt4096 - g) >> 12
    ProcedureReturn 0
  EndIf
  If *f\m065F <> 0 And *f\m0625 <> 0 And *f\speedHi <> 0
    *f\speedHi = *f\speedHi - 1
  EndIf
  alt16 = (g + 63) >> 6                                         ; lifted exactly onto the ground (the game: up by 4 height units)
  *f\altHi = (alt16 >> 8) & $FF : *f\altLo = alt16 & $FF
  *f\m061D = 0
  If *f\pitchHi & $80
    *f\pitchHi = (*f\pitchHi + 1) & $FF
    *f\pitchLo = 0
  EndIf
  ProcedureReturn 1
EndProcedure

; --- one frame: LA3E7 edge loop, then the sky fill (render_sky) ---------------------------------------------
Procedure RenderScene(*f.TFlight)
  Protected e.i, a.i, b.i
  FillMemory(@scr(0), #VIEW_W * #VIEW_H, 0)
  InitLineBuf()
  UpdateTables(*f)
  For e = 0 To 142 Step 2
    a = edges(e) : b = edges(e + 1)
    If (vflag(a) & ($80 | $20)) = 0 And (vflag(b) & ($80 | $40)) = 0   ; trivial reject
      ProjectVertex(a) : ProjectVertex(b)
      If dbgFile
        WriteStringN(dbgFile, "edge " + Str(e / 2) + " v" + Str(a) + "(" + Hex(vflag(a)) + ") -> v" + Str(b) + "(" + Hex(vflag(b)) + ")  x1=" + Str(px(a)) + " y1=" + Str(py(a)) + " x2=" + Str(px(b)) + " y2=" + Str(py(b)) + "  z1=" + Str(vz(a + 1)) + " z2=" + Str(vz(b + 1)) + "  h1=" + Str(vh(a)) + " h2=" + Str(vh(b)))
      EndIf
      If px(a) < px(b)                                                  ; back-facing edges are dropped
        DrawEdge(px(a), py(a), vh(a), px(b), py(b), vh(b))
      EndIf
    EndIf
  Next
EndProcedure

Procedure PresentScene(ox.i, oy.i)
  Protected col.i, row.i, first.i, SX.i = #SX, SY.i = #SY
  Protected sky.i = RGB(236, 132, 96), ground.i = RGB(152, 78, 34), line.i = RGB(60, 26, 8)
  For col = 0 To #VIEW_W - 1
    first = -1
    For row = 0 To #VIEW_H - 1
      If scr(row * #VIEW_W + col)
        first = row
        Break
      EndIf
    Next
    If first < 0
      Box(ox + col * SX, oy, SX, #VIEW_H * SY, sky)
    Else
      If first > 0 : Box(ox + col * SX, oy, SX, first * SY, sky) : EndIf
      Box(ox + col * SX, oy + first * SY, SX, (#VIEW_H - first) * SY, ground)
      For row = first To #VIEW_H - 1
        If scr(row * #VIEW_W + col)
          Box(ox + col * SX, oy + row * SY, SX, SY, line)
        EndIf
      Next
    EndIf
  Next
  Box(ox, oy + (#VIEW_H - #COCKPIT_ROWS) * SY, #VIEW_W * SX, #COCKPIT_ROWS * SY, RGB(107, 107, 107))   ; cockpit bar hides the bottom rows
EndProcedure

Procedure.s LandText(*f.TFlight)
  If *f\m28A4 : ProcedureReturn "  LANDED" : EndIf
  If *f\m065F = 0 : ProcedureReturn "  LANDING" : EndIf
  ProcedureReturn ""
EndProcedure

Procedure.s WideText()
  Protected t.s = ""
  If wide = 0 : ProcedureReturn "8 bit" : EndIf
  If wide & #W_POS : t + "pos " : EndIf
  If wide & #W_TRIG : t + "trig " : EndIf
  If wide & #W_PROJ : t + "proj " : EndIf
  If wide & #W_LINE : t + "line " : EndIf
  If wide & #W_VEL : t + "vel " : EndIf
  If wide & #W_GND : t + "gnd " : EndIf
  If wide & #W_CULL : t + "cull " : EndIf
  ProcedureReturn "wide: " + t
EndProcedure

Procedure.s Hud(*f.TFlight)
  Protected bank.f = S16(*f\bankLo, *f\bankHi) / 256.0
  Protected pitch.f = S16(*f\pitchLo, *f\pitchHi) / 256.0
  Protected head.f = (*f\headLo + *f\headHi * 256) * 360.0 / 16384.0
  Protected alt.f = (*f\altLo + *f\altHi * 256) / 256.0
  Protected spd.f = (*f\speedLo + *f\speedHi * 256) / 256.0
  ProcedureReturn "bank " + StrF(bank, 1) + "  pitch " + StrF(pitch, 1) + "  heading " + StrF(head, 0) + "  alt " + StrF(alt, 0) + "  speed " + StrF(spd, 1) + "  thrust " + Str(*f\speedCfg) + LandText(*f)
EndProcedure

; --- state files (dumped from the emulator) and set-up ----------------------------------------------------
Procedure.i LoadState(file.s, *f.TFlight)
  Protected fh.i, line.s, i.i
  fh = ReadFile(#PB_Any, file)
  If fh = 0 : ProcedureReturn 0 : EndIf
  line = ReadString(fh)                                   ; 512 hex digits: heightmap
  For i = 0 To 255
    heightmap(i) = Val("$" + Mid(line, i * 2 + 1, 2))
  Next
  line = ReadString(fh)                                   ; posx posz alt heading pitch roll bank speed (16 bit, decimal)
  Protected.i v0 = Val(StringField(line, 1, " ")), v1 = Val(StringField(line, 2, " ")), v2 = Val(StringField(line, 3, " "))
  Protected.i v3 = Val(StringField(line, 4, " ")), v4 = Val(StringField(line, 5, " ")), v5 = Val(StringField(line, 6, " "))
  Protected.i v6 = Val(StringField(line, 7, " ")), v7 = Val(StringField(line, 8, " "))
  *f\posXLo = v0 & $FF : *f\posXHi = (v0 >> 8) & $FF
  *f\posZLo = v1 & $FF : *f\posZHi = (v1 >> 8) & $FF
  *f\altLo = v2 & $FF : *f\altHi = (v2 >> 8) & $FF
  *f\headLo = v3 & $FF : *f\headHi = (v3 >> 8) & $FF
  *f\pitchLo = v4 & $FF : *f\pitchHi = (v4 >> 8) & $FF
  *f\rollLo = v5 & $FF : *f\rollHi = (v5 >> 8) & $FF
  *f\bankLo = v6 & $FF : *f\bankHi = (v6 >> 8) & $FF
  *f\speedLo = v7 & $FF : *f\speedHi = (v7 >> 8) & $FF
  CloseFile(fh)
  ProcedureReturn 1
EndProcedure

Procedure.i SaveState(file.s, *f.TFlight)
  Protected fh.i, line.s, i.i
  fh = CreateFile(#PB_Any, file)
  If fh = 0 : ProcedureReturn 0 : EndIf
  For i = 0 To 255
    line + RSet(Hex(heightmap(i)), 2, "0")
  Next
  WriteStringN(fh, line)
  WriteStringN(fh, Str(*f\posXLo + *f\posXHi * 256) + " " + Str(*f\posZLo + *f\posZHi * 256) + " " +
                   Str(*f\altLo + *f\altHi * 256) + " " + Str(*f\headLo + *f\headHi * 256) + " " +
                   Str(*f\pitchLo + *f\pitchHi * 256) + " " + Str(*f\rollLo + *f\rollHi * 256) + " " +
                   Str(*f\bankLo + *f\bankHi * 256) + " " + Str(*f\speedLo + *f\speedHi * 256))
  CloseFile(fh)
  ProcedureReturn 1
EndProcedure

Procedure LoadTables()
  Protected i.i
  Restore SinData
  For i = 0 To 64 : Read.a sintable(i) : Next
  Restore SinLoData
  For i = 0 To 64 : Read.a sinlo(i) : Next
  Restore WalkData
  For i = 0 To 179 : Read.a walk(i) : Next
  Restore EdgeData
  For i = 0 To 143 : Read.a edges(i) : Next
EndProcedure

Procedure StartFlight(*f.TFlight)
  NewFlight(*f)
  *f\posXHi = $80 : *f\posZHi = $80
  *f\altHi = $24
EndProcedure

; --- main -------------------------------------------------------------------------------------------------
Define f.TFlight
Define i.i, k.i, porta.i, shotFile.s, stateFile.s, ticks.i = 0, stick.s = "center"
Define shot.i = 0, ridgeFile.s, seed.i = -1, saveFile.s, useCam.i = 0, analyse.i = 0, cullTest.i = 0
Define camAlt.i, camHead.i, camPitch.i, camX.i, camZ.i

LoadTables()
For i = 1 To CountProgramParameters()
  Select ProgramParameter(i - 1)
    Case "--shot"  : shot = 1 : shotFile = ProgramParameter(i)
    Case "--wide"  : wide = Val(ProgramParameter(i))
    Case "--analyse" : analyse = 1
    Case "--culltest" : cullTest = 1
    Case "--state" : stateFile = ProgramParameter(i)
    Case "--ticks" : ticks = Val(ProgramParameter(i))
    Case "--stick" : stick = ProgramParameter(i)
    Case "--ridge" : ridgeFile = ProgramParameter(i)
    Case "--seed"  : seed = Val(ProgramParameter(i))
    Case "--save"  : saveFile = ProgramParameter(i)
    Case "--cam"                                           ; alt16 heading16 pitch16 posx posz
      useCam = 1
      camAlt = Val(ProgramParameter(i)) : camHead = Val(ProgramParameter(i + 1)) : camPitch = Val(ProgramParameter(i + 2))
      camX = Val(ProgramParameter(i + 3)) : camZ = Val(ProgramParameter(i + 4))
    Case "--edges" : dbgFile = CreateFile(#PB_Any, ProgramParameter(i))
  EndSelect
Next

StartFlight(@f)
If stateFile <> ""
  If LoadState(stateFile, @f) = 0
    MessageRequester("jaggie_3d", "cannot read " + stateFile)
    End
  EndIf
Else
  If seed >= 0 : RandomSeed(seed) : EndIf
  InitGridMap()
EndIf
If useCam
  f\altLo = camAlt & $FF : f\altHi = (camAlt >> 8) & $FF
  f\headLo = camHead & $FF : f\headHi = (camHead >> 8) & $3F
  f\pitchLo = camPitch & $FF : f\pitchHi = (camPitch >> 8) & $FF
  f\posXLo = camX & $FF : f\posXHi = (camX >> 8) & $FF
  f\posZLo = camZ & $FF : f\posZHi = (camZ >> 8) & $FF
EndIf
If saveFile <> "" : SaveState(saveFile, @f) : EndIf

Procedure.i StickToPorta(stick.s)
  Select stick
    Case "left"  : ProcedureReturn $F & ~#LEFT
    Case "right" : ProcedureReturn $F & ~#RIGHT
    Case "up"    : ProcedureReturn $F & ~#UP
    Case "down"  : ProcedureReturn $F & ~#DOWN
  EndSelect
  ProcedureReturn $F
EndProcedure

; --- analysis (--analyse): where does the 8 bit game lose precision?  Needs the console (compile with /CONSOLE) --------------------------------
Global Dim aPx.i(44)
Global Dim aPy.i(44)
Global Dim aOk.i(44)
Global Dim rPx.i(44)
Global Dim rPy.i(44)
Global Dim rOk.i(44)
Global Dim gMaxX.d(4)
Global Dim gSumX.d(4)
Global Dim gMaxY.d(4)
Global Dim gSumY.d(4)
Global Dim gN.i(4)

Procedure Verts(mode.i, *f.TFlight)
  Protected i.i
  wide = mode
  If (mode & 15) = 0
    UpdateTables(*f)
    For i = 0 To 44 : ProjectVertex(i) : aPx(i) = px(i) << 8 : aPy(i) = py(i) << 8 : Next
  Else
    UpdateTables16(*f)
    For i = 0 To 44 : ProjectVertex16(i, *f) : aPx(i) = px16(i) : aPy(i) = py16(i) : Next
  EndIf
  For i = 0 To 44 : aOk(i) = Bool(vflag(i) = 0) : Next
EndProcedure

; errors of the vertices of the mode against the reference (all wide): the vertices inside the picture (columns 44 .. 211, rows $60 .. $A0)
Procedure CompareMode(slot.i, mode.i, *f.TFlight)
  Protected i.i, n.i, ex.d, ey.d, mxx.d, mxy.d, sxx.d, syy.d
  Verts(#W_ALL, *f)
  For i = 0 To 44 : rPx(i) = aPx(i) : rPy(i) = aPy(i) : rOk(i) = aOk(i) : Next
  Verts(mode, *f)
  For i = 0 To 44
    If rOk(i) And aOk(i) And rPx(i) >= 44 * 256 And rPx(i) < 212 * 256 And rPy(i) >= $60 * 256 And rPy(i) < $A0 * 256
      ex = Abs(aPx(i) - rPx(i)) / 256.0
      ey = Abs(aPy(i) - rPy(i)) / 256.0
      If ex > mxx : mxx = ex : EndIf
      If ey > mxy : mxy = ey : EndIf
      sxx + ex : syy + ey : n + 1
    EndIf
  Next
  If n > 0
    PrintN("    " + LSet(Str(mode), 3) + " max dx " + StrF(mxx, 2) + "  mean dx " + StrF(sxx / n, 2) + "   max dy " + StrF(mxy, 2) + "  mean dy " + StrF(syy / n, 2) + "   (" + Str(n) + " vertices)")
    If mxx > gMaxX(slot) : gMaxX(slot) = mxx : EndIf
    If mxy > gMaxY(slot) : gMaxY(slot) = mxy : EndIf
    gSumX(slot) + sxx : gSumY(slot) + syy : gN(slot) + n
  EndIf
EndProcedure

; the 32 step horizon ramp of the game against the exact ramp (rows), over the picture columns
Procedure.d RampError(*f.TFlight)
  Protected xs.i, r.i, a6.d, ex.d, e.d, m.d
  wide = 0
  UpdateTables(*f)
  For xs = 44 To 211
    r = ramp(xs >> 3)
    If r & $80 : r - 256 : EndIf
    a6 = 6 - S16(*f\pitchLo, *f\pitchHi) / 64.0
    ex = a6 + (xs - 128) * S16(*f\rollLo, *f\rollHi) / 4096.0
    e = Abs(r - ex)
    If e > m : m = e : EndIf
  Next
  ProcedureReturn m
EndProcedure

; --- --culltest: the pictures with and without #W_CULL must be identical; the steps of the line routines saved
Procedure CullTest()
  Protected f.TFlight, sc.i, k.i, m.i, i.i, diff.i, s0.i, s1.i, c1.i, same.i, total.i
  Protected Dim sticks.s(5)
  Protected Dim ticks.i(5)
  Protected Dim ref.a(#VIEW_W * #VIEW_H - 1)
  sticks(0) = "center" : ticks(0) = 30
  sticks(1) = "left"   : ticks(1) = 45
  sticks(2) = "right"  : ticks(2) = 70
  sticks(3) = "up"     : ticks(3) = 25
  sticks(4) = "down"   : ticks(4) = 40
  sticks(5) = "left"   : ticks(5) = 30
  If OpenConsole() = 0 : ProcedureReturn : EndIf
  StartFlight(@f)
  For sc = 0 To 5
    wide = 0
    For k = 1 To ticks(sc)
      FlightTick(@f, StickToPorta(sticks(sc)))
      GroundCollision(@f)
    Next
    For m = 0 To 1
      Protected md.i = 0
      If m = 1 : md = 15 : EndIf
      wide = md : lineSteps = 0 : lineCulled = 0
      If wide & 15 : RenderScene16(@f) : Else : RenderScene(@f) : EndIf
      CopyMemory(@scr(0), @ref(0), #VIEW_W * #VIEW_H) : s0 = lineSteps
      wide = md | #W_CULL : lineSteps = 0 : lineCulled = 0
      If wide & 15 : RenderScene16(@f) : Else : RenderScene(@f) : EndIf
      s1 = lineSteps : c1 = lineCulled
      diff = 0
      For i = 0 To #VIEW_W * #VIEW_H - 1
        If scr(i) <> ref(i) : diff + 1 : EndIf
      Next
      If diff = 0 : same + 1 : EndIf
      total + 1
      PrintN("scenario " + Str(sc + 1) + " wide " + RSet(Str(md), 2) + ": " + Str(diff) + " pixels differ, line steps " + Str(s0) + " -> " + Str(s1) + " (" + Str(c1) + " pieces skipped, " + StrF(100.0 * (s0 - s1) / s0, 1) + " % fewer)")
    Next
  Next
  PrintN(Str(same) + " of " + Str(total) + " pictures identical")
  CloseConsole()
EndProcedure

Procedure Analyse()
  Protected f.TFlight, sc.i, k.i, t.i, st.s, n.i, slot.i
  Protected Dim sticks.s(7)
  Protected Dim ticks.i(7)
  Protected Dim modes.i(3)
  Protected Dim mname.s(3)
  Protected vxi.i, vzi.i, head.i, idx.i, w.i, th.d, ss.d, cc.d, err.d, mx0.d, mx1.d, sum0.d, sum1.d, sp.i, m2.d, vxx.d, vzz.d, mxv0.d, mxv1.d, sv0.d, sv1.d, mxd.d
  sticks(0) = "center" : ticks(0) = 30
  sticks(1) = "left"   : ticks(1) = 45
  sticks(2) = "left"   : ticks(2) = 45
  sticks(3) = "right"  : ticks(3) = 70
  sticks(4) = "up"     : ticks(4) = 25
  sticks(5) = "center" : ticks(5) = 30
  sticks(6) = "down"   : ticks(6) = 40
  sticks(7) = "right"  : ticks(7) = 30
  modes(0) = 0   : mname(0) = "all 8 bit (the game)"
  modes(1) = 14  : mname(1) = "only the position fraction is 8 bit (#W_POS off)"
  modes(2) = 13  : mname(2) = "only the heading / sine is 8 bit (#W_TRIG off)"
  modes(3) = 11  : mname(3) = "only perspective + ramp are 8 bit (#W_PROJ off)"
  If OpenConsole() = 0 : ProcedureReturn : EndIf
  PrintN("Screen error of the projected vertices in pixels (dx: columns, dy: rows of the 48 row game view) against the all wide computation.")
  StartFlight(@f)
  For sc = 0 To 7
    wide = 0
    For k = 1 To ticks(sc)
      FlightTick(@f, StickToPorta(sticks(sc)))
      GroundCollision(@f)
    Next
    PrintN("")
    PrintN("scenario " + Str(sc + 1) + ": " + sticks(sc) + " " + Str(ticks(sc)) + " ticks   " + Hud(@f))
    PrintN("  horizon ramp table vs exact ramp: max error " + StrF(RampError(@f), 2) + " rows")
    For k = 0 To 3
      PrintN("  " + mname(k))
      CompareMode(k, modes(k), @f)
    Next
  Next
  PrintN("")
  PrintN("=== summary over the scenarios (pixels) ===")
  For k = 0 To 3
    If gN(k) : PrintN(LSet(mname(k), 52) + " max dx " + StrF(gMaxX(k), 2) + "  mean dx " + StrF(gSumX(k) / gN(k), 2) + "   max dy " + StrF(gMaxY(k), 2) + "  mean dy " + StrF(gSumY(k) / gN(k), 2)) : EndIf
  Next

  ; ---- heading -> camera direction: error of the direction of (sin, cos) against the true heading
  mx0 = 0 : mx1 = 0 : mxd = 0
  For head = 0 To $3FFF
    th = head * 2 * #PI / 16384.0
    idx = (head >> 6) & 255 : w = (head >> 3) & 7
    ss = SinInterp(idx, w) : cc = SinInterp(idx + 64, w)
    err = Abs(ASin((ss * Cos(th) - cc * Sin(th)) / Sqr(ss * ss + cc * cc)))
    If err > mx0 : mx0 = err : EndIf : sum0 + err
    m2 = Abs(Sqr(ss * ss + cc * cc) / 2040.0 - 1) : If m2 > mxd : mxd = m2 : EndIf
    ss = SinHead(head) : cc = SinHead(head + $1000)
    err = Abs(ASin((ss * Cos(th) - cc * Sin(th)) / Sqr(ss * ss + cc * cc)))
    If err > mx1 : mx1 = err : EndIf : sum1 + err
  Next
  PrintN("")
  PrintN("camera direction (sin / cos) against the true heading, over all 16384 headings:")
  PrintN("  game (8 bit idx + 3 bit interpolation, mirror XOR $3F):  max " + StrF(Degree(mx0), 3) + " deg, mean " + StrF(Degree(sum0 / 16384), 3) + " deg;  length of the step varies up to " + StrF(mxd * 100, 2) + " %")
  PrintN("  16 bit (14 bit angle, 64 steps of weight, mirror 64 - y):  max " + StrF(Degree(mx1), 3) + " deg, mean " + StrF(Degree(sum1 / 16384), 3) + " deg")

  ; ---- flight model: the ground track (direction of the velocity) against the heading
  For k = 0 To 1
    sp = 12
    If k = 1 : sp = 30 : EndIf
    mxv0 = 0 : mxv1 = 0 : sv0 = 0 : sv1 = 0
    For head = 0 To $3FFF Step 3
      th = head * 2 * #PI / 16384.0
      f\headLo = head & $FF : f\headHi = (head >> 8) & $3F
      f\speedHi = sp : f\speedLo = 0
      Velocity(@f)
      vxx = S16(f\velXLo, f\velXHi) : vzz = S16(f\velZLo, f\velZHi)
      If vxx <> 0 Or vzz <> 0
        err = Abs(ASin((vxx * Cos(th) - vzz * Sin(th)) / Sqr(vxx * vxx + vzz * vzz)))
      Else
        err = 0
      EndIf
      If err > mxv0 : mxv0 = err : EndIf : sv0 + err
      vxi = (sp * 256 * SinHead(head)) >> 16 : vzi = (sp * 256 * SinHead(head + $1000)) >> 16
      vxx = vxi : vzz = vzi
      err = Abs(ASin((vxx * Cos(th) - vzz * Sin(th)) / Sqr(vxx * vxx + vzz * vzz + 1e-9)))
      If err > mxv1 : mxv1 = err : EndIf : sv1 + err
    Next
    PrintN("ground track against the heading, speed " + Str(sp) + ":  game max " + StrF(Degree(mxv0), 2) + " deg, mean " + StrF(Degree(sv0 / 5462), 2) + " deg   |   16 bit max " + StrF(Degree(mxv1), 3) + " deg, mean " + StrF(Degree(sv1 / 5462), 3) + " deg")
  Next
  CloseConsole()
EndProcedure

If cullTest
  CullTest()
  End
EndIf
If analyse
  Analyse()
  End
EndIf

If shot
  UsePNGImageEncoder()
  For k = 1 To ticks
    FlightTick(@f, StickToPorta(stick))
    If wide & #W_GND : GroundCollision16(@f) : Else : GroundCollision(@f) : EndIf     ; L97A9
  Next
  If wide & 15 : RenderScene16(@f) : Else : RenderScene(@f) : EndIf
  If ridgeFile <> ""                                     ; per column topmost ridge value, like the game's $2608 buffer
    Define rf.i = CreateFile(#PB_Any, ridgeFile), cc.i, rr.i, line.s
    If rf
      For cc = 0 To #VIEW_W - 1
        line = "$FF"
        For rr = 0 To #VIEW_H - 1
          If scr(rr * #VIEW_W + cc)
            line = "$" + RSet(Hex(#Y_MAX - rr), 2, "0")
            Break
          EndIf
        Next
        WriteStringN(rf, Str(cc + #X_MIN) + " " + line)
      Next
      CloseFile(rf)
    EndIf
  EndIf
  If CreateImage(0, #VIEW_W * #SX, #VIEW_H * #SY + 24) And StartDrawing(ImageOutput(0))
    PresentScene(0, 0)
    DrawText(4, #VIEW_H * #SY + 4, Hud(@f) + "  " + WideText(), RGB(255, 255, 255), RGB(0, 0, 0))
    StopDrawing()
    SaveImage(0, shotFile, #PB_ImagePlugin_PNG)
  EndIf
  End
EndIf

; interactive window
Define ev.i, last.i = ElapsedMilliseconds(), now.i, kk.i, kd.i
Dim kPrev.i(9)
If OpenWindow(0, 0, 0, #VIEW_W * #SX, #VIEW_H * #SY + 24, "jaggie_3d - arrows fly, +/- speed, L land, T or * take off, Esc quits", #PB_Window_SystemMenu | #PB_Window_ScreenCentered)
  If CanvasGadget(0, 0, 0, #VIEW_W * #SX, #VIEW_H * #SY + 24)
    Repeat
      Repeat
        ev = WindowEvent()
        If ev = #PB_Event_CloseWindow : End : EndIf
      Until ev = 0
      If GetAsyncKeyState_(#VK_ESCAPE) & $8000 : End : EndIf
      now = ElapsedMilliseconds()
      If now - last >= 33                                    ; flight tick at 30 Hz, like the game
        last = now
        porta = $F
        For kk = 0 To 7                                      ; 1 .. 7 toggle a wide stage (7 = skip hidden line pieces), 0 = the 8 bit game, 9 = all wide
          kd = Bool(GetAsyncKeyState_('0' + kk) & $8000)
          If kd And kPrev(kk) = 0
            If kk = 0 : wide = 0 : Else : wide = wide ! (1 << (kk - 1)) : EndIf
          EndIf
          kPrev(kk) = kd
        Next
        kd = Bool(GetAsyncKeyState_('9') & $8000)
        If kd And kPrev(9) = 0 : wide = #W_ALL : EndIf
        kPrev(9) = kd
        If GetAsyncKeyState_(#VK_LEFT) & $8000  : porta = porta & ~#LEFT  : EndIf
        If GetAsyncKeyState_(#VK_RIGHT) & $8000 : porta = porta & ~#RIGHT : EndIf
        If GetAsyncKeyState_(#VK_UP) & $8000    : porta = porta & ~#UP    : EndIf
        If GetAsyncKeyState_(#VK_DOWN) & $8000  : porta = porta & ~#DOWN  : EndIf
        If GetAsyncKeyState_('L') & $8000 : KeyLand(@f) : EndIf                      ; land (only below $64 clearance)
        If GetAsyncKeyState_(#VK_MULTIPLY) & $8000 Or GetAsyncKeyState_('T') & $8000 : KeyTakeoff(@f) : EndIf
        If f\m065F = 0 And (GetAsyncKeyState_(#VK_ADD) & $8000 Or GetAsyncKeyState_(#VK_OEM_PLUS) & $8000 Or GetAsyncKeyState_(#VK_PRIOR) & $8000 Or GetAsyncKeyState_(#VK_SUBTRACT) & $8000 Or GetAsyncKeyState_(#VK_OEM_MINUS) & $8000 Or GetAsyncKeyState_(#VK_NEXT) & $8000)
          f\m065F = $FF : f\speedCfg = $1E                   ; a throttle key cancels the landing (L4ABA / L4A9C)
        EndIf
        If GetAsyncKeyState_(#VK_ADD) & $8000 Or GetAsyncKeyState_(#VK_OEM_PLUS) & $8000 Or GetAsyncKeyState_(#VK_PRIOR) & $8000
          If f\speedCfg < 100 : f\speedCfg + 1 : EndIf        ; throttle up: $2887 is the thrust value of L86CA
        EndIf
        If GetAsyncKeyState_(#VK_SUBTRACT) & $8000 Or GetAsyncKeyState_(#VK_OEM_MINUS) & $8000 Or GetAsyncKeyState_(#VK_NEXT) & $8000
          If f\speedCfg > 0 : f\speedCfg - 1 : EndIf        ; throttle down
        EndIf
        FlightTick(@f, porta)
        If wide & #W_GND : GroundCollision16(@f) : Else : GroundCollision(@f) : EndIf    ; L97A9: the camera cannot enter the terrain
        If wide & 15 : RenderScene16(@f) : Else : RenderScene(@f) : EndIf
        If StartDrawing(CanvasOutput(0))
          PresentScene(0, 0)
          DrawText(4, #VIEW_H * #SY + 4, Hud(@f) + "  " + WideText(), RGB(255, 255, 255), RGB(0, 0, 0))
          StopDrawing()
        EndIf
      Else
        Delay(2)
      EndIf
    ForEver
  EndIf
EndIf

; IDE Options = PureBasic 6.41 (Windows - x64)
; Folding = -----
; EnableXP
; DPIAware