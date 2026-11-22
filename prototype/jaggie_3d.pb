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
;   guns + green laser          [L7B7B, LA8AB, LAA2F, LAB79 .. LABFB]  nodetype $64 on high ground; a gun on screen is picked at random and fires one
;                                        beam per round (wedge down to row $6C, colour $BE).  Key F = shoot now, --guns n (L0617, 0 = none), --fire.
;
; Compile:  pbcompiler jaggie_3d.pb /EXE jaggie_3d.exe
; Options:  --shot out.png [--state state.txt] [--ticks n] [--stick left|right|up|down]  (no window)
;           --seed n  --cam alt16 heading16 pitch16 posx posz  --save state.txt   (random terrain / camera for tests)

EnableExplicit
IncludeFile "jaggie_flight.pbi"
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
Global Dim nodetype.a(255)        ; $0A00  node type: $64 = gun emplacement (it shoots the green laser)
Global Dim lz.a(#VIEW_W * #VIEW_H - 1)      ; the laser beam and the gun sprites of this frame (the game ORs them into the same bitmap with colour 3 / the DLI colour $BE)
Global gunThresh.i = $14          ; L0617: a node becomes a gun if RANDOM < this (the game: 0 on level 1, higher on the later levels)
Global laserMask.i = $15          ; L0618: random part of the time between two shots (level 1 of the game: $15)
Global laserCnt.i                 ; L2913: counts the render passes to the next shot
Global gunDepth.i, gunX.i, gunY.i ; L2912, L2910, L2911: the gun that was picked in this round (depth = z high byte, 0 = none)
Global laserNo.i                  ; L28AC: shots in this round (the first one flashes the sky)
Global laserFlash.i               ; sky colour flash of the first beam frame ($DE = $28)
Global laserFire.i                ; test: fire at once at the next pass
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
  Protected a.i, t.i, c2.i, b6.i, cH.i, mid.i, guard.i
  Repeat
    guard + 1
    If guard > 4000 : ProcedureReturn : EndIf
    If X >= $D4
      ProcedureReturn
    EndIf
    z82 = X
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

; --- the guns and the green laser [L7B7B, LA8AB, LAA2F, LAB79 .. LABFB, LA75C] -------------------------------------
; The enemy guns are nodes with nodetype $64 on high ground.  Every drawn frame LA8AB looks at the guns that are on screen: about every second one
; (RANDOM bit 7) is remembered as the shooter of the round (screen x, y and depth), if none is yet.  LAA2F counts the passes down (L0618 mask of random);
; at 0 it fires ONE beam: a wedge from the gun to the lower edge of the view (row $6C), 1 pixel wide at the gun and 1 pixel wider every 256 / L291E rows,
; with a random sideways slope.  Steep beams are drawn as horizontal runs per row, flat ones as vertical runs per column (so it is as thick everywhere).
; Pixels go in without the ridge test (over the mountains).  After the shot the round is over: the counter reloads and a new gun is picked.
Procedure PlaceGuns()                               ; L7B7B: every node in the order X = X + $43, RANDOM < L0617 and high ground (heightmap bit 7)
  Protected x.i = 0
  FillMemory(@nodetype(0), 256, 0)
  Repeat
    If Random(255) < gunThresh And (heightmap(x) & $80)
      nodetype(x) = $64
    EndIf
    x = (x + $43) & $FF
  Until x = 0
EndProcedure

Procedure LzPlot(x.i, y.i)                          ; LA75C without the height test
  If x >= #X_MIN And x <= #X_MAX And y >= #Y_MIN And y < #Y_MAX + 1
    lz((#Y_MAX - y) * #VIEW_W + (x - #X_MIN)) = 2
  EndIf
EndProcedure

Procedure LzGun(x.i, y.i, zh.i)                     ; the gun itself: a small dark tower on the ridge (3 sizes by distance)
  Protected h.i = 1, w.i = 1, i.i, k.i
  If zh < 5 : h = 4 : w = 2 : ElseIf zh < 10 : h = 3 : w = 2 : ElseIf zh < 18 : h = 2 : EndIf
  For i = 0 To h - 1
    For k = 0 To w - 1
      If x + k >= #X_MIN And x + k <= #X_MAX And y + i >= #Y_MIN And y + i <= #Y_MAX
        lz((#Y_MAX - (y + i)) * #VIEW_W + (x + k - #X_MIN)) = 1
      EndIf
    Next
  Next
EndProcedure

Procedure GunPass(landed.i)                                 ; LA8AB for every vertex that is a gun, on screen and not behind a nearer ridge
  Protected i.i, cell.i, zh.i, x.i, y.i
  For i = 0 To 44
    cell = vcell(i)
    If nodetype(cell) = $64 And pdone(i) And vflag(i) = 0
      x = px(i) : y = py(i) : zh = (vz(i + 1) >> 8) & $FF
      If zh > 0 And x >= #X_MIN And x < #X_MAX - 1 And y >= #Y_MIN And y <= #Y_MAX
        If y >= lineBuf(x)
          LzGun(x, y, zh)
          If gunDepth = 0 And (Random(255) & $80) And landed = 0
            gunX = x : gunY = y : gunDepth = zh
          EndIf
        EndIf
      EndIf
    EndIf
  Next
EndProcedure

Procedure LaserFire()                               ; LAA58 .. LABFB: one beam from (gunX, gunY)
  Protected x.i = gunX, y.i = gunY, w.i = 1, accY.i = $80, fx.i = $80, fy.i = $80
  Protected slope.i, r.i, yStep.i, grow.i, k.i, run.i, sum.i, ny.i
  r = Random(255)
  slope = (r * 2) & $FF : If r >= 128 : slope - 256 : EndIf          ; L291A / L291B: signed, -256 .. 254 (1/256 pixel per step)
  yStep = (Random(255) * 2) & $FF                                     ; L291C: the fraction of the y step of a flat beam
  grow = (((~$7B) & $FF) >> 3) + $0C                                  ; L291E: the widening (L060C = $7B in flight)
  laserFlash = Bool(laserNo = 0) : laserNo + 1
  If Random(255) & $80                                                ; LAB79: BMI -> flat beam (LABD6): vertical runs of w pixels per column
    Repeat
      If x < $2C Or x >= $D4 Or y < $6C : Break : EndIf
      For k = 0 To w - 1
        If y - k < $6C : Break : EndIf
        LzPlot(x, y - k)
      Next
      accY + grow : If accY > 255 : accY - 256 : w + 1 : EndIf        ; L2918 / L2919
      fy + yStep : sum = -1 : If fy > 255 : fy - 256 : sum = 0 : EndIf ; y + $FF + carry: 0 or -1 a column
      y + sum
      If slope < 0 : x - 1 : Else : x + 1 : EndIf                     ; L291B sign: left or right
    ForEver
  Else                                                                ; steep beam: horizontal runs of w pixels per row, the start follows the slope
    slope - $40                                                       ; LAB7E: a small bias to the left
    If y >= $6C
      Repeat
        For k = 0 To w - 1
          If x + k >= $2C And x + k < $D4 : LzPlot(x + k, y) : EndIf
        Next
        accY + grow : If accY > 255 : accY - 256 : w + 1 : EndIf
        fx + slope
        While fx > 255 : fx - 256 : x + 1 : Wend
        While fx < 0 : fx + 256 : x - 1 : Wend
        y - 1
        If y < $6C : Break : EndIf
      ForEver
    EndIf
  EndIf
EndProcedure

Procedure LaserPass()                               ; LAA2F: once per render pass (the game renders two passes per flight tick)
  If laserFire And gunDepth <> 0                                     ; test key / --fire: shoot now
    laserFire = 0 : LaserFire() : ProcedureReturn
  EndIf
  laserCnt - 1
  If laserCnt < 0
    laserCnt = laserMask & Random(255)                                ; the round is over
    gunDepth = 0 : laserNo = 0
    ProcedureReturn
  EndIf
  If laserCnt <> 0 And laserFire = 0 : ProcedureReturn : EndIf       ; fires when the counter is exactly 0 (laserFire: test key)
  If gunDepth = 0 : ProcedureReturn : EndIf                          ; no gun was seen in this round
  laserFire = 0
  LaserFire()
EndProcedure

; --- one frame: LA3E7 edge loop, then the sky fill (render_sky) ---------------------------------------------
Procedure RenderScene(*f.TFlight)
  Protected e.i, a.i, b.i
  FillMemory(@scr(0), #VIEW_W * #VIEW_H, 0)
  FillMemory(@lz(0), #VIEW_W * #VIEW_H, 0)
  laserFlash = 0
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
  GunPass(*f\m28A4)
  LaserPass() : LaserPass()                                            ; the game renders two passes per flight tick, each ends with LAA2F
EndProcedure

Procedure PresentScene(ox.i, oy.i)
  Protected col.i, row.i, first.i, SX.i = #SX, SY.i = #SY
  Protected sky.i = RGB(236, 132, 96), ground.i = RGB(152, 78, 34), line.i = RGB(60, 26, 8)
  If laserFlash : sky = RGB(204, 108, 64) : EndIf                    ; the first beam frame: the sky flashes darker ($DE = $28)
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
  Protected lc.i = RGB(200, 255, 90), gc.i = RGB(40, 14, 4), lr.i, lcol.i
  For row = 0 To #VIEW_H - 1
    For col = 0 To #VIEW_W - 1
      lcol = lz(row * #VIEW_W + col)
      If lcol = 2 : Box(ox + col * SX, oy + row * SY, SX, SY, lc) : ElseIf lcol = 1 : Box(ox + col * SX, oy + row * SY, SX, SY, gc) : EndIf
    Next
  Next
  Box(ox, oy + (#VIEW_H - #COCKPIT_ROWS) * SY, #VIEW_W * SX, #COCKPIT_ROWS * SY, RGB(107, 107, 107))   ; cockpit bar hides the bottom rows
EndProcedure

Procedure.s LandText(*f.TFlight)
  If *f\m28A4 : ProcedureReturn "  LANDED" : EndIf
  If *f\m065F = 0 : ProcedureReturn "  LANDING" : EndIf
  ProcedureReturn ""
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
Define shot.i = 0, ridgeFile.s, seed.i = -1, saveFile.s, useCam.i = 0
Define camAlt.i, camHead.i, camPitch.i, camX.i, camZ.i

LoadTables()
For i = 1 To CountProgramParameters()
  Select ProgramParameter(i - 1)
    Case "--shot"  : shot = 1 : shotFile = ProgramParameter(i)
    Case "--state" : stateFile = ProgramParameter(i)
    Case "--ticks" : ticks = Val(ProgramParameter(i))
    Case "--stick" : stick = ProgramParameter(i)
    Case "--ridge" : ridgeFile = ProgramParameter(i)
    Case "--seed"  : seed = Val(ProgramParameter(i))
    Case "--guns"  : gunThresh = Val(ProgramParameter(i))             ; L0617: 0 = none, 20 = default
    Case "--fire"  : laserFire = 1                                     ; test: the first gun that is seen shoots at once
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
PlaceGuns()

Procedure.i StickToPorta(stick.s)
  Select stick
    Case "left"  : ProcedureReturn $F & ~#LEFT
    Case "right" : ProcedureReturn $F & ~#RIGHT
    Case "up"    : ProcedureReturn $F & ~#UP
    Case "down"  : ProcedureReturn $F & ~#DOWN
  EndSelect
  ProcedureReturn $F
EndProcedure

If shot
  UsePNGImageEncoder()
  For k = 1 To ticks
    FlightTick(@f, StickToPorta(stick))
    GroundCollision(@f)                                   ; L97A9
  Next
  RenderScene(@f)
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
    DrawText(4, #VIEW_H * #SY + 4, Hud(@f), RGB(255, 255, 255), RGB(0, 0, 0))
    StopDrawing()
    SaveImage(0, shotFile, #PB_ImagePlugin_PNG)
  EndIf
  End
EndIf

; interactive window
Define ev.i, last.i = ElapsedMilliseconds(), now.i
If OpenWindow(0, 0, 0, #VIEW_W * #SX, #VIEW_H * #SY + 24, "jaggie_3d - arrows fly, +/- speed, L land, T or * take off, F fire a gun, Esc quits", #PB_Window_SystemMenu | #PB_Window_ScreenCentered)
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
        If GetAsyncKeyState_(#VK_LEFT) & $8000  : porta = porta & ~#LEFT  : EndIf
        If GetAsyncKeyState_(#VK_RIGHT) & $8000 : porta = porta & ~#RIGHT : EndIf
        If GetAsyncKeyState_(#VK_UP) & $8000    : porta = porta & ~#UP    : EndIf
        If GetAsyncKeyState_(#VK_DOWN) & $8000  : porta = porta & ~#DOWN  : EndIf
        If GetAsyncKeyState_('F') & $8000 : laserFire = 1 : EndIf                     ; test: shoot now (when a gun is on screen)
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
        GroundCollision(@f)                                  ; L97A9: the camera cannot enter the terrain
        RenderScene(@f)
        If StartDrawing(CanvasOutput(0))
          PresentScene(0, 0)
          DrawText(4, #VIEW_H * #SY + 4, Hud(@f), RGB(255, 255, 255), RGB(0, 0, 0))
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