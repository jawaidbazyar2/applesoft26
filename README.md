# AppleSoft Basic for MacOS

If macOS refuses to open a downloaded `AppleSoft.app` ("Apple cannot check it for malicious software"), clear the quarantine flag and try again:

```bash
xattr -dr com.apple.quarantine AppleSoft.app
```

You can also Control-click the app and choose **Open**.

A native macOS app that edits and runs AppleSoft BASIC. It is a compiled interpreter, not an emulator. The editor tokenizes each line the way AppleSoft does; the run window is an Apple II text screen that also supports GR and HGR graphics.

Requires **macOS 11** or later, **CMake 3.20+**, and the Xcode command-line tools (`clang`). The app is a universal binary (Apple Silicon and Intel).

## Build

```bash
cmake -S . -B build
cmake --build build
```

The app bundle is `build/AppleSoft.app`. Open it with:

```bash
open build/AppleSoft.app
```

To run the unit tests:

```bash
ctest --test-dir build --output-on-failure
```

## Using the program

The editor window is the listing. Each row has a breakpoint checkbox, a line number, and the source. Keywords and variables are highlighted as you type; a syntax error is underlined in red.

Type a program line (or just a line number) in the **Immediate** field at the bottom and press Return. A numbered line is inserted into the listing. A command without a number runs immediately and prints on the run window.

| Action | Where |
| --- | --- |
| Run | **Run** button, or Program → Run (**⌘R**) |
| Pause | **Pause** button, or Program → Pause |
| Stop | **Stop** button, or Program → Stop (**⌘.**) |
| Single-step | **Step** button, or Program → Step |
| Breakpoint | Checkbox to the left of a line number |
| New / Open / Save | File menu. Programs use the `.abas` extension |
| Renumber | File → Renumber (starts at 10, step 10) |
| Delete line | Edit → Delete line (**⌘⌫**) |

**Save** writes a tokenized AppleSoft program image (`.abas`). That is the same on-disk form an Apple II uses, so a file saved here can be loaded on real hardware, and a classic AppleSoft file can be opened here.

When you Run, a second window opens for program input and output. It uses the Apple II font for text. `INPUT` and `GET` read from that window.

Two sample programs are in `Samples/`:

- `LEMONADE.abas` — the classic lemonade-stand game
- `BRIANSTHEME.abas` — the classic Moire pattern demo
- `DHGRDEMO.abas` — double hi-res color bars and a fan of lines (`& DHGR`)

Open either from **File → Open…**.

### Graphics

These display modes are supported. Every mode is drawn in the traditional Apple II frame — the same 280×192 aspect as `HGR` — and scaled to the run window. Split-screen modes keep four text lines at the bottom.

- `TEXT`
- `GR` and `GR(40,40)` — 40×48 lo-res, 16 colors. Each block is 7×4 pixels of the 280×192 screen. Mixed mode shows the top 40 rows.
- `GR(80,50)` — 80×50 lo-res, 16 colors. Each block is 4×4 pixels of the `HGR(320,200)` grid.
- `HGR` — 280×192 hi-res, 8 colors
- `HGR2` — second hi-res page, full screen
- `HGR(320,200)` — 320×200, 16 colors
- `HGR(640,400)` — 640×400, 16 colors. This is the 320×200 grid doubled, so each of its pixels is one quarter of an `HGR(320,200)` pixel, and a `GR(80,50)` block is 8×8 of these pixels.

`PLOT`, `HLIN`, `VLIN`, `HPLOT`, `COLOR=`, and `HCOLOR=` work in the matching modes. Graphics are drawn cleanly; the Apple II’s color-fringing artifacts are not reproduced.

`CALL` is tokenized but raises an error at run time. `PR#` and `IN#` are accepted and ignored.

## Custom `&` commands

AppleSoft’s `&` hook is used for these extensions. After `&`, the name is stored as letters (not as a keyword), so `DELAY` is not eaten by `DEL` and `TONE` is not eaten by `TO`.

### `& DELAY seconds`

Pauses the program for `seconds`. The argument is a numeric expression. Zero is allowed and returns immediately. A negative value is an illegal quantity.

```basic
10 PRINT "WAIT"
20 & DELAY 0.5
30 PRINT "DONE"
```

### `& TONE hertz, seconds`

Plays a square-wave tone at `hertz` for `seconds`, and waits that long before the next statement. Both arguments are numeric expressions.

- Frequency must be in **0–8000**. Above 8000 is an illegal quantity.
- **0 hertz** is a rest: silence for the given duration.
- **Pause** mutes the tone; **Run** / continue starts it again for the time still remaining.
- **Stop** cancels the wait and the sound.

```basic
10 A = 440
20 & TONE A, 0.25
30 & TONE 0, 0.1
40 & TONE 880, 0.25
```

### Double hi-res: `& DHGR`, `& DHCOLOR`, `& DHPLOT`

- `& DHGR` — switches to double hi-res: **560×192, 16 colors** (the lo-res palette, 0–15), and clears it to black. Like `HGR`, it is mixed: the top 160 rows show above four text lines. `POKE 49234,0` shows all 192 rows, `POKE 49235,0` brings the text lines back.
- `& DHCOLOR = n` — sets the drawing color, 0–15 (the `=` is optional). It is the same color as `HCOLOR=`.
- `& DHPLOT x,y` — plots a point. `x` is 0–559, `y` is 0–191.
- `& DHPLOT x1,y1 TO x2,y2 [TO x3,y3 ...]` — draws lines, like `HPLOT`.
- `& DHPLOT TO x,y` — draws a line from the last point plotted.

`& DHPLOT` outside `& DHGR` mode, or off the screen, is an illegal quantity. `HPLOT` and `HCOLOR=` also work in double hi-res mode. As with the other modes, it is drawn clean — one color per pixel, without the Apple IIe’s 4-pixel color artifacts.

```basic
10 & DHGR
20 FOR C = 0 TO 15
30 & DHCOLOR = C
40 & DHPLOT C * 35,0 TO 280,159
50 NEXT C
```

Any other word after `&` is a syntax error (`& NOPE 1`).
