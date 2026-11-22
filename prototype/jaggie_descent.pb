; jaggie_descent.pb - the descent from orbit to the planet of Behind Jaggi Lines!, as a prototype for the Atari ST
;
; What the Atari does (analysis of jaggie_descent.atstate2, routines L5FB5 / L5FD8 / L601D / L607D):
;   22 columns of 16 bit heights (1/256 rows, $088C..), mirrored left / right: a dome.  They start at $2EE0 + table L626A (> 46 rows = above the window).
;   every 2 frames one step:  acc += $18 (16 bit, high byte = rows),  h = max(0, h - 4 * (h >> 8) - (acc >> 8))   (about 1.6 % decay plus a growing linear term)
;   the limb: colour 3 (1 row, rust) on top, colour 2 (acc >> 8 rows, mid orange), from the row h >> 8 on colour 1 (light orange) - the top row is h - acc - 1
;   the sequence: 220 frames of black space with stars, then 190 steps (380 frames), then the flight starts.  50 Hz PAL.
; What is different here (the ST version):
;   the 8 pixel steps of the limb are gone: the heights are interpolated (Catmull Rom) across the columns to one height per pixel column, and in time
;   between the steps (a new picture every frame instead of every 2nd frame).  The columns stay the game's data, so the dome has the game's shape.
;
; Compile:  pbcompiler jaggie_descent.pb /EXE jaggie_descent.exe
; Keys:     Space = restart, Esc = quit.      Options: --shot out.png frame   (no window; frame = 50 Hz frame number of the sequence)

EnableExplicit
UsePNGImageEncoder()

#VIEW_W  = 320            ; ST low resolution
#WIN_H   = 112            ; the window above the cockpit, in lines (the Atari window has 43 rows of 2 lines: 112 / 43 = 2.6 lines per row)
#VIEW_H  = 150
#SC      = 3              ; zoom of the prototype window
#HOLD    = 220            ; frames of black space before the descent starts
#STEPS   = 190            ; steps of the descent (one per 2 frames on the Atari)
#NCOL    = 22

Global Dim H0.i(#NCOL - 1)
Global Dim HS.i(#STEPS, #NCOL - 1)                   ; the heights after each step (the game's integer arithmetic)
Global Dim Acc.i(#STEPS)                             ; the accumulator $08C8/9 after each step
Global Dim StarX.i(15), Dim StarY.i(15)
Global LS.f = #WIN_H / 43.0

Procedure InitModel()
  Protected c.i, s.i, h.i, acc.i, i.i
  Restore StartHeights
  For c = 0 To #NCOL - 1 : Read.i H0(c) : Next
  For c = 0 To #NCOL - 1 : HS(0, c) = H0(c) : Next
  Acc(0) = 0
  acc = 0
  For s = 1 To #STEPS
    acc + $18
    Acc(s) = acc
    For c = 0 To #NCOL - 1
      h = HS(s - 1, c) - 4 * (HS(s - 1, c) >> 8) - (acc >> 8)
      If h < 0 : h = 0 : EndIf
      HS(s, c) = h
    Next
  Next
  RandomSeed(7)                                       ; a few stars in the black: single pixels, the Atari picks them at random (L5F75), they do not move
  For i = 0 To 15
    StarX(i) = 8 + Random(#VIEW_W - 17)
    StarY(i) = 4 + Random(#WIN_H - 20)
  Next
EndProcedure

Procedure.f CatRom(p0.f, p1.f, p2.f, p3.f, t.f)       ; a smooth curve through p1 .. p2
  ProcedureReturn 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t)
EndProcedure

Procedure.f ColH(sf.f, c.i)                          ; the height of column c (0 .. 21) at the fractional step sf, in rows
  Protected s.i = Int(sf), f.f = sf - s
  If s >= #STEPS : s = #STEPS - 1 : f = 1.0 : EndIf
  If c < 0 : c = 0 : EndIf
  If c > #NCOL - 1 : c = #NCOL - 1 : EndIf
  ProcedureReturn (HS(s, c) * (1.0 - f) + HS(s + 1, c) * f) / 256.0
EndProcedure

Procedure DrawFrame(frame.i)                         ; into the current output, 320 x 150 lines at zoom #SC
  Protected x.i, y.i, c.i, st.f, accRows.f, d.f, cf.f, t.f, hrow.f, yr.i, ym.i, i.i
  Protected rust.i = RGB(145, 45, 3), mid.i = RGB(207, 106, 64), lite.i = RGB(243, 143, 101)
  Box(0, 0, #VIEW_W * #SC, #VIEW_H * #SC, RGB(107, 107, 107))                         ; the cockpit grey
  Box(0, 0, #VIEW_W * #SC, #WIN_H * #SC, RGB(0, 0, 0))                                ; the window: space
  If frame < #HOLD : st = 0.0 : Else : st = (frame - #HOLD) / 2.0 : EndIf
  If st > #STEPS : st = #STEPS : EndIf
  For i = 0 To 15
    Box(StarX(i) * #SC, StarY(i) * #SC, #SC, #SC, RGB(190, 190, 190))
  Next
  accRows = (st * $18) / 256.0                                                         ; the band of the mid colour: acc >> 8 rows (smooth in time)
  If st > 0
    For x = 0 To #VIEW_W - 1
      d = Abs(x + 0.5 - #VIEW_W / 2.0)                                                ; distance from the middle, in pixels
      cf = 21.0 - (d - 4.0) / 8.0                                                    ; the column position: the entry c sits at 4 + 8 * (21 - c) pixels from the middle
      If cf > 21.0 : cf = 21.0 : EndIf
      c = Int(cf) : t = cf - c
      hrow = CatRom(ColH(st, c - 1), ColH(st, c), ColH(st, c + 1), ColH(st, c + 2), t)
      If hrow < 0 : hrow = 0 : EndIf
      yr = Int((hrow - accRows - 1.0) * LS + 0.5)                                       ; the top of the limb: 1 row of rust, then acc >> 8 rows of mid orange above the height h
      ym = Int(hrow * LS + 0.5)                                                          ; the light orange starts at the row h >> 8 (measured in the game: the limb is drawn above h)
      If yr < 0 : yr = 0 : EndIf
      If ym < 0 : ym = 0 : EndIf
      If yr < #WIN_H
        If ym > #WIN_H : ym = #WIN_H : EndIf
        Box(x * #SC, yr * #SC, #SC, (#WIN_H - yr) * #SC, lite)
        Box(x * #SC, yr * #SC, #SC, (ym - yr) * #SC, mid)
        y = yr + Int(LS + 0.5) : If y > ym : y = ym : EndIf
        Box(x * #SC, yr * #SC, #SC, (y - yr) * #SC, rust)
      EndIf
    Next
  EndIf
  DrawText(4, #WIN_H * #SC + 4, "frame " + Str(frame) + "  step " + StrF(st, 1), RGB(255, 255, 255), RGB(107, 107, 107))
EndProcedure

DataSection
  StartHeights:
  Data.i 17022, 16643, 16264, 15923, 15581, 15278, 14975, 14710, 14445, 14217, 13990, 13800, 13611, 13459, 13308, 13194, 13080, 13004, 12929, 12891, 12853, 12853
EndDataSection

InitModel()

Define shotFile.s = "", shotFrame.i = 0, k.i
For k = 1 To CountProgramParameters()
  If ProgramParameter(k - 1) = "--shot" : shotFile = ProgramParameter(k) : shotFrame = Val(ProgramParameter(k + 1)) : EndIf
Next
If shotFile <> ""
  If CreateImage(0, #VIEW_W * #SC, #VIEW_H * #SC) And StartDrawing(ImageOutput(0))
    DrawFrame(shotFrame)
    StopDrawing()
    SaveImage(0, shotFile, #PB_ImagePlugin_PNG)
  EndIf
  End
EndIf

Define ev.i, frame.i = 0, last.i = ElapsedMilliseconds(), now.i
If OpenWindow(0, 0, 0, #VIEW_W * #SC, #VIEW_H * #SC, "jaggie_descent - Space restarts, Esc quits", #PB_Window_SystemMenu | #PB_Window_ScreenCentered)
  If CanvasGadget(0, 0, 0, #VIEW_W * #SC, #VIEW_H * #SC)
    Repeat
      Repeat
        ev = WindowEvent()
        If ev = #PB_Event_CloseWindow : End : EndIf
      Until ev = 0
      If GetAsyncKeyState_(#VK_ESCAPE) & $8000 : End : EndIf
      If GetAsyncKeyState_(#VK_SPACE) & $8000 : frame = 0 : EndIf
      now = ElapsedMilliseconds()
      If now - last >= 20                                                              ; 50 Hz
        last = now
        If StartDrawing(CanvasOutput(0))
          DrawFrame(frame)
          StopDrawing()
        EndIf
        frame + 1
        If frame > #HOLD + 2 * #STEPS + 100 : frame = 0 : EndIf                        ; a pause on the full orange, then again
      Else
        Delay(2)
      EndIf
    ForEver
  EndIf
EndIf
