; jaggie_3d_tab.pb - jaggie_3d.pb with an experimental ridge line from tables (no midpoint displacement per frame).
;
; The game draws an edge by halving it in screen space; every midpoint gets the hash h1 + h2 + 1 and, when that is >= $80, is moved up
; (carry of the sum) or down by a quarter of the width of the piece.  The shape therefore scales with the width of the edge, so one
; normalised profile per edge is enough:
;   prof(e, 0 .. #NS)   the offsets of the edge e of the fan at a reference width of #NS columns, in quarter rows; computed with the
;                       game's rule from the two hash bytes, and only again when the hash bytes of that fan edge change (the camera
;                       enters another cell) - not per frame
;   drawing             per column: straight line from end 1 to end 2 (a fixed point step) + prof(e, i) * width / 256
; tabMode: 0 = the game's routine (as jaggie_3d.pb), 1 = table, exact scale (a multiplication per column), 2 = table, the width is
; rounded down to its top 3 bits (the scale is then 2 or 3 shifts / adds: what a Z80 / 6502 version would do).
; Key T in the window switches the mode.  Options:  --tab n   --cmp out.png (the three modes one below the other, with the difference
; of the ridge to the game's routine).
; Compile:  pbcompiler jaggie_3d_tab.pb /EXE jaggie_3d_tab.exe
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
#NS = 64                          ; samples of a ridge profile
Global tabMode.i = 0              ; 0 game routine, 1 table (exact scale), 2 table (scale by shifts)
Global Dim prof.i(71, #NS)        ; per edge of the fan: offsets in quarter rows at the reference width #NS
Global Dim profH1.i(71)           ; the hash bytes the profile was made for (-1 = none)
Global Dim profH2.i(71)
Global nodeCnt.i, callCnt.i, plotCnt.i, midCnt.i           ; work counters of the game's routine: midpoints pushed by the byte routine, PlotCol calls, pixels that really change the ridge, 16 bit midpoints
Global Dim ZK.i(63)                 ; id of the piece (heap index of the subdivision tree) whose right end is stack entry i
Global Dim markN.i(8191) : Global Dim expN.i(8191) : Global Dim touched.i(8191)
Global gen.i, nTouched.i, curLeaf.i, sumNec.i, sumLeafCols.i, sumNecCols.i
Global Dim EUp.i(255) : Global Dim EDn.i(255)   ; per node of the subdivision tree of the call: the highest / lowest excursion from the chord in 1/256 of the width of the piece (an upper bound, from the hash bytes alone)
Global cullKind.i = 2                                  ; 1 = the bound of the ST port (half the width + 3), 2 = the bound from the subdivision tree
Global lodN.i                                          ; > 0: pieces narrower than this are drawn as a straight line (no midpoints): the fine detail is at most 1 .. 2 rows
Global Dim wHist.i(5) : Global histOn.i                 ; expansions by width class: 3-4, 5-8, 9-16, 17-32, 33-64, 65+
; --- precalculated plans for small pieces (the "unrolled" kernels): for a piece of n columns the structure of the subdivision depends on n alone
Global kernelMax.i                                     ; > 0: pieces of 3 .. kernelMax columns are evaluated from a precalculated plan, without a stack
Global Dim planLo.i(16, 15) : Global Dim planMid.i(16, 15) : Global Dim planHi.i(16, 15) : Global Dim planCnt.i(16)     ; the midpoints of a piece of n columns, parents first
Global Dim leafLo.i(16, 15) : Global Dim leafHi.i(16, 15) : Global Dim leafCnt.i(16)                                  ; its pieces of 1 or 2 columns, left to right
Global Dim Dtab.i(255, 2, 1)                           ; [width][0 none, 1 up, 2 down][parity]: the midpoint is  (y0 + y1) >> 1  +  Dtab
Global Dim kY.i(16) : Global Dim kH.i(16)
Global kernelNodes.i, kernelPieces.i
Global cullOn.i, cullSlack.i = 3, cullPieces.i, cullScan.i, cullFail.i, cullActive.i
Global profBuilds.i               ; profiles computed so far (to see how rarely that happens)
Global Dim refBuf.i(255)          ; ridge of the game's routine, for the comparison

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
  callCnt + 1
  If y <= lineBuf(x)
    ProcedureReturn
  EndIf
  plotCnt + 1
  Protected nn.i = curLeaf
  While nn > 0 And nn < 8192 : markN(nn) = gen : nn >> 1 : Wend
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
Procedure BuildE(id.i, hl.i, hr.i, depth.i)                  ; excursion bounds of the piece id (hash bytes at its ends hl, hr), 1/256 of its width
  Protected sum.i, mh.i, up.i, dn.i, a.i, b.i
  If depth >= 7
    EUp(id) = 0 : EDn(id) = 0
    ProcedureReturn
  EndIf
  sum = hl + hr + 1 : mh = sum & $FF
  If mh & $80
    If sum > 255 : up = 64 : Else : dn = 64 : EndIf              ; the midpoint is moved by a quarter of the width: up when the sum has a carry
  EndIf
  BuildE(id * 2, hl, mh, depth + 1) : BuildE(id * 2 + 1, mh, hr, depth + 1)
  a = EUp(id * 2) : If EUp(id * 2 + 1) > a : a = EUp(id * 2 + 1) : EndIf
  b = EDn(id * 2) : If EDn(id * 2 + 1) > b : b = EDn(id * 2 + 1) : EndIf
  EUp(id) = up + (a >> 1)                                        ; the midpoint moves up by `up`, and the halves have their own excursions (half the width)
  EDn(id) = dn + (b >> 1)
EndProcedure

Procedure PlanSplit(n.i, lo.i, hi.i)
  Protected w.i = hi - lo, mid.i
  If w >= 3
    mid = lo + (w >> 1)
    planLo(n, planCnt(n)) = lo : planMid(n, planCnt(n)) = mid : planHi(n, planCnt(n)) = hi : planCnt(n) + 1
    PlanSplit(n, lo, mid) : PlanSplit(n, mid, hi)
  Else
    leafLo(n, leafCnt(n)) = lo : leafHi(n, leafCnt(n)) = hi : leafCnt(n) + 1
  EndIf
EndProcedure

Procedure BuildPlans()
  Protected n.i, m.i, c2.i
  For n = 3 To 16 : planCnt(n) = 0 : leafCnt(n) = 0 : PlanSplit(n, 0, n) : Next
  For n = 3 To 255
    m = n >> 1
    For c2 = 0 To 1
      Dtab(n, 0, c2) = 0
      Dtab(n, 1, c2) = c2 + ((m & $FF) >> 1)               ; up:   floor average + c2 + b6      (saturates at 255)
      Dtab(n, 2, c2) = c2 - (((m - 1) & $FF) >> 1) - 1     ; down: floor average + c2 - b6 - 1  (wraps in 8 bits)
    Next
  Next
EndProcedure

Procedure Line51Cbody(X.i, yi.i)
  Protected a.i, t.i, c2.i, b6.i, cH.i, mid.i, guard.i, n.i, hi.i, lo.i, up.i, dn.i, c.i, hidden.i
  Repeat
    guard + 1
    If guard > 4000 : ProcedureReturn : EndIf
    If X >= $D4
      ProcedureReturn
    EndIf
    z82 = X
    If cullActive                                           ; exact culling of a piece from the bounds of its own subdivision tree
      n = (ZX(yi) - X) & $FF
      If n >= 4 And ZK(yi) < 128
        cullPieces + 1
        hi = z84 : If ZY(yi) > hi : hi = ZY(yi) : EndIf
        lo = z84 : If ZY(yi) < lo : lo = ZY(yi) : EndIf
        If cullKind = 1
          up = hi + (n >> 1) + 3
          dn = lo - (n >> 1) - 3
        Else
          up = hi + ((EUp(ZK(yi)) * n) >> 8) + cullSlack
          dn = lo - ((EDn(ZK(yi)) * n) >> 8) - cullSlack
        EndIf
        If dn >= 0 And up <= 255
          hidden = 1
          For c = X To X + n - 1
            cullScan + 1
            If lineBuf(c) < up : hidden = 0 : Break : EndIf
          Next
          If hidden
            z84 = ZY(yi) : z86 = ZH(yi) : X = ZX(yi) : yi - 1
            If yi < 0 : ProcedureReturn : EndIf
            Continue
          EndIf
        EndIf
      EndIf
    EndIf
    If kernelMax > 0
      n = (ZX(yi) - X) & $FF
      If n >= 3 And n <= kernelMax
        kernelPieces + 1
        kY(0) = z84 : kY(n) = ZY(yi) : kH(0) = z86 : kH(n) = ZH(yi)
        For c = 0 To planCnt(n) - 1                         ; the midpoints, parents first: no stack, no width arithmetic: everything from the plan
          Protected pl.i = planLo(n, c), pm.i = planMid(n, c), ph.i = planHi(n, c), w.i = ph - pl, cls.i = 0, sm.i, base.i, c2k.i, v.i
          sm = kH(pl) + kH(ph) + 1
          kH(pm) = sm & $FF
          t = kY(pl) + kY(ph) : base = t >> 1 : c2k = t & 1
          If kH(pm) & $80
            If sm > 255 : cls = 1 : Else : cls = 2 : EndIf
          EndIf
          v = base + Dtab(w, cls, c2k)
          If cls = 1
            If v > 255 : v = 255 : EndIf
          ElseIf cls = 2
            v = v & $FF
          EndIf
          kY(pm) = v
          kernelNodes + 1
        Next
        For c = 0 To leafCnt(n) - 1                         ; the pixels, left to right
          If leafHi(n, c) - leafLo(n, c) = 1
            PlotCol(X + leafLo(n, c), kY(leafHi(n, c)))
          Else
            PlotCol(X + leafLo(n, c), (kY(leafLo(n, c)) + kY(leafHi(n, c)) + 1) >> 1)
            PlotCol(X + leafLo(n, c) + 1, kY(leafHi(n, c)))
          EndIf
        Next
        z84 = ZY(yi) : z86 = ZH(yi) : X = ZX(yi) : yi - 1
        If yi < 0 : ProcedureReturn : EndIf
        Continue
      EndIf
    EndIf
    If lodN > 0
      n = (ZX(yi) - X) & $FF
      If n >= 3 And n < lodN
        hi = z84 : lo = ZY(yi)
        For c = 1 To n
          PlotCol(X + c - 1, (hi * (n - c) + lo * c + (n >> 1)) / n)
        Next
        z84 = lo : z86 = ZH(yi) : X = ZX(yi) : yi - 1
        If yi < 0 : ProcedureReturn : EndIf
        Continue
      EndIf
    EndIf
    a = (X - ZX(yi)) & $FF
    If a = $FE                                              ; 2 columns: plot the middle, then the end
      curLeaf = ZK(yi)
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
      curLeaf = ZK(yi)
      z84 = ZY(yi)
      PlotCol(X, ZY(yi))
      yi - 1
      If yi < 0 : ProcedureReturn : EndIf
      X + 1
      z86 = ZH(yi + 1)
    Else                                                    ; LB5E4: push the midpoint
      nodeCnt + 1
      If histOn
        n = (ZX(yi) - X) & $FF
        If n <= 4 : wHist(0) + 1 : ElseIf n <= 8 : wHist(1) + 1 : ElseIf n <= 16 : wHist(2) + 1 : ElseIf n <= 32 : wHist(3) + 1 : ElseIf n <= 64 : wHist(4) + 1 : Else : wHist(5) + 1 : EndIf
      EndIf
      expN(ZK(yi)) = gen : touched(nTouched) = ZK(yi) : nTouched + 1
      ZK(yi + 1) = 2 * ZK(yi) : ZK(yi) = ZK(yi) * 2 + 1
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

Procedure Line51C(X.i, yi.i)                                ; wrapper: the necessary nodes (the expanded pieces that contain a pixel that changed the ridge) are counted
  Protected k.i, n.i
  gen + 1 : nTouched = 0 : curLeaf = 0
  For k = 0 To yi : ZK(k) = 1 : Next
  cullActive = 0
  If cullOn And yi = 0 : BuildE(1, z86, ZH(0), 0) : cullActive = 1 : EndIf
  Line51Cbody(X, yi)
  For k = 0 To nTouched - 1
    If markN(touched(k)) = gen : n + 1 : EndIf
  Next
  If cullOn = 0 : sumNec + n : EndIf
  curLeaf = 0
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
  midCnt + 1
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

; --- ridge line from tables ---------------------------------------------------------------------------------
; ProfRec: the midpoint rule of LB3A7 / LB51C on the normalised edge: hash = h1 + h2 + 1; >= $80: moved by a quarter of the width of
; the piece (here: hi - lo quarter rows), up when the sum has a carry
Procedure ProfRec(e.i, lo.i, hi.i, hlo.i, hhi.i)
  Protected mid.i, sum.i, mh.i, v.i
  If hi - lo < 2
    ProcedureReturn
  EndIf
  mid = (lo + hi) >> 1
  sum = hlo + hhi + 1
  mh = sum & $FF
  v = (prof(e, lo) + prof(e, hi)) >> 1
  If mh & $80
    If sum > 255
      v + (hi - lo)
    Else
      v - (hi - lo)
    EndIf
  EndIf
  prof(e, mid) = v
  ProfRec(e, lo, mid, hlo, mh)
  ProfRec(e, mid, hi, mh, hhi)
EndProcedure

Procedure BuildProfile(e.i, h1.i, h2.i)
  If profH1(e) = h1 And profH2(e) = h2
    ProcedureReturn
  EndIf
  profH1(e) = h1 : profH2(e) = h2
  profBuilds + 1
  prof(e, 0) = 0 : prof(e, #NS) = 0
  ProfRec(e, 0, #NS, h1, h2)
EndProcedure

; DrawEdgeTab: the edge e from (x1, y1) to (x2, y2): the columns x1 .. x2 - 1 (a point with the x value n is plotted in column n - 1,
; as in the game), clipped to the view; no bisection, no 16 bit stage
Procedure DrawEdgeTab(e.i, x1.i, y1.i, h1.i, x2.i, y2.i, h2.i)
  Protected w.i = x2 - x1, c.i, c0.i, c1.i, n.i, i.i, y.i, stepI.i, stepY.i, wa.i, k.i
  If w <= 0
    ProcedureReturn
  EndIf
  BuildProfile(e, h1, h2)
  c0 = x1 : If c0 < #X_MIN : c0 = #X_MIN : EndIf
  c1 = x2 - 1 : If c1 > #X_MAX : c1 = #X_MAX : EndIf
  If c0 > c1
    ProcedureReturn
  EndIf
  stepI = (#NS << 8) / w                                   ; 8.8 fixed point: profile samples per column
  stepY = ((y2 - y1) << 8) / w                             ; rows per column
  wa = w
  If tabMode = 2                                           ; the width rounded down to its top 3 bits
    k = 0
    While (wa >> k) > 7 : k + 1 : Wend
    wa = (wa >> k) << k
  EndIf
  For c = c0 To c1
    n = c + 1 - x1
    i = (n * stepI) >> 8
    If i > #NS : i = #NS : EndIf
    y = y1 + ((n * stepY) >> 8) + ((prof(e, i) * wa) >> 8)
    PlotCol(c, Clamp8(y))
  Next
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
        If tabMode = 1 Or tabMode = 2
          DrawEdgeTab(e / 2, px(a), py(a), vh(a), px(b), py(b), vh(b))
        Else
          DrawEdge(px(a), py(a), vh(a), px(b), py(b), vh(b))
        EndIf
      EndIf
    EndIf
  Next
EndProcedure

Procedure.s ModeName()
  Select tabMode
    Case 1 : ProcedureReturn "table, exact scale"
    Case 2 : ProcedureReturn "table, shift scale"
    Case 3 : ProcedureReturn "game routine, straight line below a width of " + Str(lodN) + " columns"
  EndSelect
  ProcedureReturn "game routine"
EndProcedure

; RidgeDiff: the ridge (highest value per column, $FF = closed at the top row $97) against the one kept in refBuf
Procedure.s RidgeDiff()
  Protected c.i, a.i, b.i, d.i, sum.i, mx.i, n.i, same.i
  For c = #X_MIN To #X_MAX
    a = lineBuf(c) : If a > $97 : a = $97 : EndIf
    b = refBuf(c) : If b > $97 : b = $97 : EndIf
    d = Abs(a - b)
    sum + d : n + 1
    If d > mx : mx = d : EndIf
    If d = 0 : same + 1 : EndIf
  Next
  ProcedureReturn Str(same) + "/" + Str(n) + " columns equal, mean " + StrF(sum / n, 2) + ", max " + Str(mx) + " rows"
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
Define lodArg.i, cmpFile.s, statsFile.s, m.i, tWas.i
For i = 0 To 71 : profH1(i) = -1 : profH2(i) = -1 : Next

LoadTables()
BuildPlans()
For i = 1 To CountProgramParameters()
  Select ProgramParameter(i - 1)
    Case "--shot"  : shot = 1 : shotFile = ProgramParameter(i)
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
    Case "--tab"   : tabMode = Val(ProgramParameter(i))
    Case "--cmp"   : cmpFile = ProgramParameter(i)
    Case "--stats" : statsFile = ProgramParameter(i)
    Case "--lod"   : lodArg = Val(ProgramParameter(i))
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

If statsFile <> ""                                       ; numbers: over many ticks, how far the table ridge is from the game's, and how often a profile has to be built
  Dim kNode.i(2) : Dim kKern.i(2) : Dim kPieces.i(2) : Dim kWrong.i(2)
  Dim lodNode.i(4) : Dim lodCall.i(4) : Dim lodSum.i(4) : Dim lodN2.i(4) : Dim lodMax.i(4) : Dim lodEq.i(4)
  Define sumNodeO.i, sumCallO.i, sumScanO.i, cullFailO.i, c0n.i, c0c.i, sumNodeC.i, sumCallC.i, sumScanC.i, sumPiecesC.i, sumNode.i, sumCall.i, sumPlot.i, sumMid.i, c2.i, a2.i, b2.i, d2.i, bw.i, dumped.i, dumpF.i, dumpNow.i
  Dim sumD.i(2) : Dim maxD.i(2) : Dim eq.i(2) : Dim nn.i(2) : Dim builds.i(2)
  If CreateFile(0, statsFile)
    RandomSeed(Val(Str(seed)))
    For k = 1 To ticks
      FlightTick(@f, StickToPorta(stick))
      GroundCollision(@f)
      dumpNow = 0
      nodeCnt = 0 : callCnt = 0 : plotCnt = 0 : midCnt = 0
      cullOn = 0 : tabMode = 0 : RenderScene(@f) : CopyArray(lineBuf(), refBuf())
      c0n = nodeCnt : c0c = callCnt
      nodeCnt = 0 : callCnt = 0 : cullPieces = 0 : cullScan = 0 : cullOn = 1 : tabMode = 3 : RenderScene(@f) : cullOn = 0
      sumNodeC + nodeCnt : sumCallC + callCnt : sumScanC + cullScan : sumPiecesC + cullPieces
      For c2 = #X_MIN To #X_MAX
        If lineBuf(c2) <> refBuf(c2) : cullFail + 1 : EndIf
      Next
      nodeCnt = 0 : callCnt = 0 : cullScan = 0 : cullOn = 1 : cullKind = 1 : tabMode = 3 : histOn = 1 : RenderScene(@f) : histOn = 0 : cullOn = 0 : cullKind = 2
      sumNodeO + nodeCnt : sumCallO + callCnt : sumScanO + cullScan
      For c2 = #X_MIN To #X_MAX
        If lineBuf(c2) <> refBuf(c2) : cullFailO + 1 : EndIf
      Next
      nodeCnt = c0n : callCnt = c0c
      For m = 0 To 4                                          ; the sweep: the cull of the ST port plus the straight line below a width
        lodN = Val(StringField("0,6,8,12,16", m + 1, ","))
        nodeCnt = 0 : callCnt = 0 : cullOn = 1 : cullKind = 1 : tabMode = 3 : RenderScene(@f) : cullOn = 0 : cullKind = 2 : lodN = 0
        lodNode(m) + nodeCnt : lodCall(m) + callCnt
        For c2 = #X_MIN To #X_MAX
          a2 = lineBuf(c2) : If a2 > $97 : a2 = $97 : EndIf
          b2 = refBuf(c2) : If b2 > $97 : b2 = $97 : EndIf
          d2 = Abs(a2 - b2) : lodSum(m) + d2 : lodN2(m) + 1 : If d2 > lodMax(m) : lodMax(m) = d2 : EndIf : If d2 = 0 : lodEq(m) + 1 : EndIf
        Next
      Next
      For m = 0 To 2                                          ; plans: the cull of the ST port + kernels for pieces up to 8 / 16 columns: the same pixels?
        kernelMax = Val(StringField("0,8,16", m + 1, ","))
        nodeCnt = 0 : callCnt = 0 : kernelNodes = 0 : kernelPieces = 0 : cullOn = 1 : cullKind = 1 : tabMode = 3 : RenderScene(@f) : cullOn = 0 : cullKind = 2 : kernelMax = 0
        kNode(m) + nodeCnt : kKern(m) + kernelNodes : kPieces(m) + kernelPieces
        For c2 = #X_MIN To #X_MAX
          If lineBuf(c2) <> refBuf(c2) : kWrong(m) + 1 : EndIf
        Next
      Next
      nodeCnt = c0n : callCnt = c0c
      tabMode = 0
      sumNode + nodeCnt : sumCall + callCnt : sumPlot + plotCnt : sumMid + midCnt
      For m = 1 To 2
        tabMode = m : bw = profBuilds : RenderScene(@f) : builds(m) + (profBuilds - bw)
        For c2 = #X_MIN To #X_MAX
          a2 = lineBuf(c2) : If a2 > $97 : a2 = $97 : EndIf
          b2 = refBuf(c2) : If b2 > $97 : b2 = $97 : EndIf
          d2 = Abs(a2 - b2)
          sumD(m) + d2 : nn(m) + 1 : If d2 > maxD(m) : maxD(m) = d2 : EndIf : If d2 = 0 : eq(m) + 1 : EndIf
          If d2 > 8 And m = 1 And dumped < 3
            If dumpF = 0 : dumpF = CreateFile(#PB_Any, "outliers.txt") : EndIf
            WriteStringN(dumpF, "tick " + Str(k) + " column " + Str(c2) + " game ridge " + Str(b2) + " table ridge " + Str(a2))
            dumpNow = 1
          EndIf
        Next
      Next
      If dumpNow And dumped < 3
        dumped + 1
        dbgFile = dumpF : tabMode = 0 : RenderScene(@f) : dbgFile = 0
        WriteStringN(dumpF, "---")
      EndIf
    Next
    For m = 0 To 4
      WriteStringN(0, "ST cull + straight line below width " + StringField("none,6,8,12,16", m + 1, ",") + ": " + StrF(lodNode(m) / ticks, 0) + " midpoints, " + StrF(lodCall(m) / ticks, 0) + " column plots tried; ridge: " + Str(lodEq(m) * 100 / lodN2(m)) + "% of the columns equal, mean " + StrF(lodSum(m) / lodN2(m), 2) + " rows, max " + Str(lodMax(m)) + " rows")
    Next
    For m = 0 To 2
      WriteStringN(0, "precalculated plans for pieces up to " + StringField("none,8,16", m + 1, ",") + " columns: " + StrF(kNode(m) / ticks, 0) + " midpoints by the stack routine + " + StrF(kKern(m) / ticks, 0) + " by plans (" + StrF(kPieces(m) / ticks, 0) + " pieces); wrong ridge columns in all pictures: " + Str(kWrong(m)))
    Next
    WriteStringN(0, "midpoints of the ST routine by piece width per picture:  3-4 columns " + StrF(wHist(0) / ticks, 0) + ", 5-8 " + StrF(wHist(1) / ticks, 0) + ", 9-16 " + StrF(wHist(2) / ticks, 0) + ", 17-32 " + StrF(wHist(3) / ticks, 0) + ", 33-64 " + StrF(wHist(4) / ticks, 0) + ", 65+ " + StrF(wHist(5) / ticks, 0))
    WriteStringN(0, "with the cull of the ST port (width / 2 + 3): " + StrF(sumNodeO / ticks, 0) + " midpoints, " + StrF(sumCallO / ticks, 0) + " column plots tried, " + StrF(sumScanO / ticks, 0) + " columns scanned; wrong ridge columns: " + Str(cullFailO))
    WriteStringN(0, "with the exact tree bound cull (slack " + Str(cullSlack) + "): " + StrF(sumNodeC / ticks, 0) + " midpoints, " + StrF(sumCallC / ticks, 0) + " column plots tried, " + StrF(sumScanC / ticks, 0) + " columns scanned by the tests; wrong ridge columns in all pictures: " + Str(cullFail))
    WriteStringN(0, "necessary midpoints (pieces that contain a pixel that changes the ridge): " + StrF(sumNec / ticks, 0) + " per picture")
    WriteStringN(0, "game routine per picture: " + StrF(sumNode / ticks, 0) + " midpoints in the byte routine, " + StrF(sumCall / ticks, 0) + " column plots tried, " + StrF(sumPlot / ticks, 0) + " really change the ridge (" + StrF(sumMid / ticks, 0) + " 16 bit midpoints)")
    For m = 1 To 2
      WriteStringN(0, "mode " + Str(m) + ": " + Str(eq(m) * 100 / nn(m)) + "% of the columns equal, mean " + StrF(sumD(m) / nn(m), 2) + " rows, max " + Str(maxD(m)) + " rows, profile builds per picture " + StrF(builds(m) / ticks, 2) + " (of 72 edges)")
    Next
    CloseFile(0)
    If dumpF : CloseFile(dumpF) : EndIf
  EndIf
  End
EndIf
If cmpFile <> ""                                         ; the three modes one below the other
  UsePNGImageEncoder()
  For k = 1 To ticks
    FlightTick(@f, StickToPorta(stick))
    GroundCollision(@f)
  Next
  If CreateImage(0, #VIEW_W * #SX, 3 * (#VIEW_H * #SY + 24)) And StartDrawing(ImageOutput(0))
    For m = 0 To 2
      tabMode = m
      If m = 2 And lodArg > 0 : tabMode = 3 : cullOn = 1 : cullKind = 1 : lodN = lodArg : EndIf
      RenderScene(@f)
      cullOn = 0 : lodN = 0
      If m = 0
        CopyArray(lineBuf(), refBuf())
      EndIf
      PresentScene(0, m * (#VIEW_H * #SY + 24))
      If m = 0
        DrawText(4, m * (#VIEW_H * #SY + 24) + #VIEW_H * #SY + 4, ModeName(), RGB(255, 255, 255), RGB(0, 0, 0))
      Else
        DrawText(4, m * (#VIEW_H * #SY + 24) + #VIEW_H * #SY + 4, ModeName() + ": " + RidgeDiff(), RGB(255, 255, 255), RGB(0, 0, 0))
      EndIf
    Next
    StopDrawing()
    SaveImage(0, cmpFile, #PB_ImagePlugin_PNG)
  EndIf
  End
EndIf

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
Define ev.i, modeIdx.i, last.i = ElapsedMilliseconds(), now.i
If lodArg > 0 : tabMode = 3 : cullOn = 1 : cullKind = 1 : lodN = lodArg : modeIdx = 3 : EndIf
If OpenWindow(0, 0, 0, #VIEW_W * #SX, #VIEW_H * #SY + 24, "jaggie_3d_tab - arrows fly, T ridge mode, +/- speed, L land, * take off, Esc quits", #PB_Window_SystemMenu | #PB_Window_ScreenCentered)
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
        If GetAsyncKeyState_('T') & $8000                     ; T: next ridge mode (on the press)
          If tWas = 0
            modeIdx = (modeIdx + 1) % 5                     ; 0 the game's routine, 1 / 2 table, 3 / 4 the game's routine with a straight line below a width of 6 / 8 columns
            Select modeIdx
              Case 0 : tabMode = 0 : cullOn = 0 : lodN = 0
              Case 1 : tabMode = 1 : cullOn = 0 : lodN = 0
              Case 2 : tabMode = 2 : cullOn = 0 : lodN = 0
              Case 3 : tabMode = 3 : cullOn = 1 : cullKind = 1 : lodN = 6
              Case 4 : tabMode = 3 : cullOn = 1 : cullKind = 1 : lodN = 8
            EndSelect
          EndIf
          tWas = 1
        Else
          tWas = 0
        EndIf
        If GetAsyncKeyState_('L') & $8000 : KeyLand(@f) : EndIf                      ; land (only below $64 clearance)
        If GetAsyncKeyState_(#VK_MULTIPLY) & $8000 : KeyTakeoff(@f) : EndIf                 ; (T is the ridge mode here)
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
          DrawText(4, #VIEW_H * #SY + 4, "[T] " + ModeName() + " (" + Str(profBuilds) + " profiles built)  " + Hud(@f), RGB(255, 255, 255), RGB(0, 0, 0))
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