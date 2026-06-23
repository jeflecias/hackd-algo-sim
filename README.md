# hackd-algo-sim (DEADLOCK)

A horror-themed **fake-malware** teaching game that secretly drills four core
Operating Systems modules through an animated, glitchy **simulated Kali shell**.

> ⚠️ **This is NOT real malware.** It never modifies your system, touches your
> files, or connects anywhere. The only thing it does is **screenshot your local
> desktop once at launch** to power a "your PC has been hacked" glitch effect —
> that image never leaves your machine.

---

## ⚠️ Safety & Disclaimer

- **Educational use only.** Built for the COEN 3374 Operating Systems course.
- The app **goes fullscreen** on launch and takes over the screen on purpose,
  for the horror effect.
- **Windows Defender / SmartScreen may show a warning** because the program
  captures a screenshot of the desktop for the glitch animation. It is a
  false positive — nothing is exfiltrated or changed.
- **Quit instantly at any time with `Esc` or `Ctrl+Q`.**
- Review the source — it is plain C with the Win32 GDI API. The screenshot is
  captured in `src/screenshot.c` and only ever blitted to the in-game screen.

---

## Requirements

- **Windows** (Vista+ / `_WIN32_WINNT=0x0601`, i.e. Windows 7+)
- **MinGW-W64 GCC** (`gcc`)
- Links against **`gdi32`** and **`user32`** (standard Windows libs).

---

## Build

```bash
# from the repo root:
gcc -O2 -Wall -Isrc -D_WIN32_WINNT=0x0601 \
    src/*.c src/modules/*.c \
    -o deadlock.exe -mwindows -lgdi32 -luser32
```

…or just:

```bash
make
```

(`-mwindows` builds it as a GUI app with no console window.)

---

## Run

```bash
./deadlock.exe
```

…or double-click `deadlock.exe`.

### Controls

| Action | How |
| --- | --- |
| Command list | type `help` (or `help <topic>` for `sched`/`mem`/`vmem`/`disk`) |
| Algorithm explainer | `man <algo>` |
| Edit datasets | `data <module> set ...` |
| Force a jumpscare | `scare` |
| Visualizer keys | `Space` play/pause, `←/→` single-step, `Esc` back to shell |
| Quit instantly | `Ctrl+Q` (or `Esc` outside the visualizer) |

---

## Features

- 🖥️ **Screenshot-glitch intro** — captures your desktop once, then tears it
  apart with RGB-split, scanlines, slice-tear and noise-block glitch effects.
- 🦠 **Fake "infection" boot sequence** — a fake Kali-style kernel parasite
  boots up and "claims" your machine.
- 💀 **Random skull jumpscare** — every **60–180 seconds** a glitching skull
  interrupts you with a **30-second OS quiz**. Answer correctly to "survive";
  your shell session resumes exactly where it left off.
- 🧠 **Four hidden OS modules** disguised as hacker tools (see below).
- 📊 **Animated algorithm visualizer** — running any `sched`/`mem`/`vmem`/`disk`
  command pops a **full-screen horror/hacker visualization** (Gantt chart, frame
  grid, disk-head sweep, memory map) with a live kernel-trace panel. `Space`
  pauses, `←/→` single-step, `Esc` returns to the shell, and the jumpscare
  countdown freezes while you watch.
- ✅ **Built-in self-test** verifies every algorithm against the textbook.

---

## Modules & Commands

The shell secretly teaches the four OS modules. Each command runs an animated,
step-by-step trace and prints the result.

### Module 4 — CPU Scheduling
```
sched fcfs | sjf | srtf | npp | pp | rr [q] | hrrn | mlq | mlfq
```
*(FCFS, SJF, Shortest-Remaining-Time-First, Non-preemptive / Preemptive Priority,
Round Robin, HRRN, Multilevel Queue, Multilevel Feedback Queue)*

### Module 5 — Memory Management
```
mem firstfit | bestfit | worstfit | bestavail | paging | swap
```
*(Placement policies + paging address translation + swap timing)*

### Module 6 — Virtual Memory
```
vmem fifo | opt | lru | belady | lfu | mfu | second | eat
```
*(Page replacement + Belady's anomaly + Effective Access Time)*

### Module 7 — Mass Storage
```
disk fcfs | sstf | scan | cscan | look | clook  [start]
```
*(Disk-arm scheduling — minimize total head movement)*

---

## Verification

A **console self-test** checks every algorithm against the textbook's published
numbers. Current status: **15/15 passing.**

| Check | Expected |
| --- | --- |
| VM FIFO faults | 15 |
| VM OPT faults | 9 |
| VM LRU faults | 12 |
| VM LFU faults | 13 |
| FIFO Belady (3 frames / 4 frames) | 9 → 10 |
| Disk FCFS movement | 640 |
| Disk SSTF movement | 236 |
| Disk SCAN movement | 236 |
| Disk LOOK movement | 299 |
| Disk C-LOOK movement | 322 |
| Paging PA (LA=3) | 23 |
| Paging PA (LA=10) | 6 |
| Swap total | 176 ms |
| Effective Access Time | 2004 µs |

Build and run the self-test:

```bash
gcc -O2 -Isrc tests/test_algos.c tests/stubs.c \
    src/modules/vmem.c src/modules/disk.c \
    -o test_algos.exe && ./test_algos.exe
```

The in-game `selftest` command runs the **same checks** and prints the results
inside the fake shell.

---

## Repo Layout

```
hackd-algo-sim/
├── Makefile
├── README.md
├── .gitignore
├── src/
│   ├── app.h          # shared types, globals, state machine
│   ├── main.c         # WinMain, window, message loop, key dispatch
│   ├── gfx.c          # 32-bit DIB framebuffer + glitch primitives
│   ├── screenshot.c   # one-shot desktop capture (local only)
│   ├── terminal.c     # terminal model + typewriter queue + caret
│   ├── commands.c     # command parse/dispatch + help/man
│   ├── intro.c        # glitch intro + fake-infection boot
│   ├── anim.c         # full-screen algorithm visualizer (ST_ANIM)
│   ├── skull.c        # ASCII/box-drawing skull renderer
│   ├── jumpscare.c    # random skull + 30s OS quiz + resume
│   ├── util.c         # RNG + clock
│   └── modules/
│       ├── sched.c    # Module 4: CPU scheduling
│       ├── memory.c   # Module 5: memory mgmt
│       ├── vmem.c     # Module 6: virtual memory / replacement
│       ├── disk.c     # Module 7: disk scheduling
│       ├── data.c     # editable datasets
│       └── selftest.c # textbook verification
└── tests/
    ├── test_algos.c   # console self-test driver
    └── stubs.c        # minimal stubs for the test build
```

---

## Notes

- **No audio yet** — the horror atmosphere is purely visual for now.
- The desktop screenshot is captured **once** at startup and only used as an
  in-game texture; it is never saved to disk or sent anywhere.

---

*DEADLOCK :: kernel parasite v6.6.6 — "you are not in control."*
