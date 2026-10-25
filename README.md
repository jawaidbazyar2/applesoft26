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
- `BRIANSTHEME.abas` — a short music demo that uses `& TONE`

Open either from **File → Open…**.

### Graphics

These display modes are supported:

- `TEXT`
- `GR` and `GR(40,40)` — 40×40 lo-res, 16 colors
- `GR(80,80)` — 80×80 lo-res, 16 colors
- `HGR` — 280×192 hi-res
- `HGR2` — second hi-res page
- `HGR(640,400)` — 640×400, 16 colors

`PLOT`, `HLIN`, `VLIN`, `HPLOT`, `COLOR=`, and `HCOLOR=` work in the matching modes. Graphics are drawn cleanly; the Apple II’s color-fringing artifacts are not reproduced.

`CALL` is tokenized but raises an error at run time. `PR#` and `IN#` are accepted and ignored.

## Custom `&` commands

AppleSoft’s `&` hook is used for two extensions. After `&`, the name is stored as letters (not as a keyword), so `DELAY` is not eaten by `DEL` and `TONE` is not eaten by `TO`.

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

Any other word after `&` is a syntax error (`& NOPE 1`).
