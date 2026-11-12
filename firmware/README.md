# OpenGong firmware

Firmware for the two Heltec WiFi LoRa 32 V3 boards in [the system design](../docs/System-Design.md):

- **target**: behind the plate. It timestamps the five comparator TRIG lines in hardware, solves the impact position and sends it over LoRa.
- **receiver**: at the firing line. It acknowledges shots, shows the hit and group on its OLED, and prints JSON over USB for a laptop app.

## Build and flash

Install [PlatformIO](https://platformio.org/install) (the VS Code extension or `pip install platformio`), then from this folder:

```
pio run -e target   -t upload     # plug in the target board
pio run -e receiver -t upload     # plug in the receiver board
pio device monitor                # 115200 baud
```

The project uses Arduino-ESP32 3.x (ESP-IDF 5) through the pioarduino platform, because the capture code needs the IDF 5 MCPWM capture driver. RadioLib and the ThingPulse SSD1306 library are fetched automatically.

**Before powering either board, attach its antenna.** The default band is 915 MHz (US). For the EU, set `kLoraFreqMHz` to 868 in [OpenGongProto.h](lib/OpenGongProto/OpenGongProto.h). If two OpenGong systems share a range, give each one its own `-DOG_NET_ID` in [platformio.ini](platformio.ini).

## Layout

| Path | What it does |
| --- | --- |
| [lib/OpenGongProto/](lib/OpenGongProto/) | Board pins, LoRa settings, packet format, CRC (shared by both boards) |
| [lib/OpenGongSolver/](lib/OpenGongSolver/) | Levenberg–Marquardt position solver and calibration fit (plain C++) |
| [src/target/capture.cpp](src/target/capture.cpp) | MCPWM hardware capture, window, hold-off, re-arm |
| [src/target/processor.cpp](src/target/processor.cpp) | Raw times → solved shot, flags, calibration taps, JSON output |
| [src/target/link.cpp](src/target/link.cpp) | LoRa task: shot + ACK + 3 retries, status heartbeat, remote commands |
| [src/target/console.cpp](src/target/console.cpp) | Text commands over USB serial or relayed over LoRa |
| [src/target/config.cpp](src/target/config.cpp) | Settings stored in flash (NVS) |
| [src/receiver/main.cpp](src/receiver/main.cpp) | Receiver: ACK, de-duplication, group stats, OLED plot, command relay |
| [test/host/solver_sim.cpp](test/host/solver_sim.cpp) | PC test of the solver and calibration (see below) |

## How the target works

1. **Capture.** Channels 1–3 run on MCPWM group 0 and channels 4–6 on group 1, with pins as in the [wiring diagram](../docs/Wiring-Diagram.md#4-heltec-v3-connections): GPIO 2, 4, 5, 6, 7, plus 47 for an optional 6th channel. The first rising edge on each channel latches the 80 MHz capture timer in hardware, so each timestamp has 12.5 ns resolution with no software delay.
2. **Timer sync.** Each MCPWM group has its own capture timer. Before every arm, the firmware toggles **GPIO48** internally, and that one edge resets both timers to zero at the same instant. **Leave GPIO48 unconnected.**
3. **Window.** The first edge opens a 200 µs window. When it closes, captures are switched off for the 250 ms hold-off, so the ringing plate doesn't flood the CPU with interrupts. If fewer than `minch` channels (default 4) fired, the event is logged as a reject and the target re-arms after 20 ms.
4. **Solve.** The solver removes each channel's calibrated delay, then fits x, y and the impact time with Levenberg–Marquardt from three starting points. The model includes plate thickness. If the RMS residual is above `rlim` and at least 5 channels fired, it re-solves without each channel in turn. It keeps a result only if the residual is clearly better, and flags it `dropped_ch`.
5. **Report.** Each shot is printed as a JSON line on USB, shown on the OLED and queued for LoRa. The radio runs in its own task on core 0, so sending and retries never delay capture.

## Coordinates

All coordinates use the jig's **front-view frame** ([System-Design 4.1](../docs/System-Design.md#41-placement-jig-petg-model-opengong_jigscad)): millimetres from the **ring centre**, +y up as the plate hangs, and x as the shooter sees it. The jig model is drawn from the back, so x_front = −x_model; use the front-view values the OpenSCAD file prints. That frame covers sensor positions, the aim point, calibration points and `sim`.

Shot results are reported relative to the aim point, which defaults to the ring centre (0, 0). `flip` is off by default. Use `set flip` only if hits show up mirrored.

Each shot's JSON also includes `px`/`py`, the raw position before the aim offset is applied.

Wherever a command takes a point (`cal at`, `check` and `sim`), you can type the name of a jig punch guide instead of coordinates: `P0`–`P5` (fitting) or `H1`, `H2` and `H4` (held out). These match System-Design 8, step 2.

## First boot and bring-up

1. Flash the target, open the serial monitor and run `show`. The defaults are the 120 mm jig ring from System-Design 8 (S1–S5 = channels 1–5, S4 straight down), 5900 m/s and 9.5 mm thickness. If your jig or plate differs, set the sensor positions and thickness, then save:
   ```
   set s 1 -70.53 97.08
   set h 9.5
   save
   ```
2. **Check the solver and radio without hardware.** Flash the receiver too, then on the target run `sim P1 150`. That's a synthetic hit at punch guide P1 (0, 60) with 150 ns noise. The target and receiver should both report about x 0, y +60, flagged `sim`.
3. **Check timer sync on the bench.** Drive all five TRIG inputs from one signal, either a 3.3 V function generator or a single comparator output, and pulse it. Every shot should show `t_ns` within about ±25 ns on all channels. If channels 4–5 are offset from 1–3, the group sync isn't working.
4. **Check the threshold.** Run `stats`; it shows per-channel edge counts while armed. With the plate quiet, the counts should stay still. If they climb, raise VTH with RV1.
5. **Check the sensors.** Tap with the center punch. All channels should fire (`"fired":"12345"`), and for repeated taps at one spot, `t_ns` should repeat within about 200 ns.

## Calibration

The fit runs on the target itself, so no laptop is needed. With known tap positions, the arrival-time model is linear in 1/v, the channel delays and each tap's start time. It's solved directly, not iteratively.

With the jig seated on the back face:

```
cal start                 # back-face taps (thickness set to 0 for the fit); "cal start front" otherwise
cal at P0                 #   ...tap 3-5 times through guide P0...
cal at P1                 #   ...tap 3-5 times... and so on through P5
cal list                  # taps per point
cal undo                  # drop the last tap if it was a bad one
cal fit                   # fits wave speed + delays; prints per-point error with the new values
cal apply                 # copy the result into the live config
cal stop
check H1                  # tap held-out guide H1 3-5 times: judge err_mean_mm (aim for < 2 mm);
                          #   single taps can exceed 2 mm from timing jitter alone
check H2
check H4
check off
save
```

While `check` is on, shots are solved for the same face as the last `cal start` (back by default). Turn it off with `check off` before live fire, so shots are solved with the full plate thickness.

Recalibrate for each plate. Calibration and settings are stored on the target board, one set per board.

- `cal fit fixv` fits only the delays and keeps the current wave speed.
- Channel 1's delay is the zero reference, so other channels' delays can be negative.
- A fitted speed far outside about 2500–7000 m/s means the sensor coordinates or tap positions are wrong.

You can run all of this from the firing line too. Type the same commands into the receiver's serial monitor, and they're relayed to the target over LoRa.

## Commands

Commands are typed into the target's serial monitor, or into the receiver's (which relays them over LoRa).

| Command | Effect |
| --- | --- |
| `help`, `show`, `stats` | Command list, current configuration, trigger and link counters |
| `set s <ch> <x> <y>` | Sensor position, mm |
| `set d <ch> <ns>` | Channel delay (normally set by `cal apply`) |
| `set v <m/s>` / `set h <mm>` | Wave speed / plate thickness |
| `set aim <x> <y>` / `set flip <fx> <fy>` | Reported origin / axis mirroring |
| `set n <3-6>` / `set minch <n>` | Sensor count (needs `save` + `reboot`) / channels needed per shot |
| `set win <us>` / `set hold <ms>` / `set rlim <ns>` | Capture window / hold-off / residual flag limit |
| `save`, `load`, `defaults`, `reboot` | Flash storage. Changes are live right away but are lost on reboot unless saved |
| `sim <pt> [noise_ns]` | Inject a synthetic hit (`<pt>` = `<x> <y>` or a guide name such as `P2`) |
| `check <pt>` / `check off` | Report each shot's error from a known point |
| `cal ...` | See [Calibration](#calibration) |

Receiver-only commands: `rx clear` (start a new group), `rx stats` and `rx help`.

## Serial output

Lines starting with `{` are JSON events; all other lines are for people to read.

Target:
```json
{"type":"shot","n":12,"x":-3.4,"y":21.7,"px":155.8,"py":174.1,"rms_ns":96,"fired":"12345","used":"12345","flags":[],"t_ns":[0.0,18412.5,30125.0,29987.5,17250.0]}
{"type":"reject","fired":"-2---","need":4}
```

Receiver:
```json
{"type":"shot","boot":40211,"n":12,"x":-3.4,"y":21.7,"rms_ns":96,"fired":31,"used":31,"flags":[],"batt_mv":4105,"rssi":-92,"snr":7.5,"group":{"n":5,"mpi_x":-1.2,"mpi_y":18.0,"es_mm":31.4}}
{"type":"status","boot":40211,"shots":12,"rejects":1,"batt_mv":4105,"uptime_min":14,"n_ch":5,"mode":"normal","rssi":-91}
```

Flags:

| Flag | Meaning |
| --- | --- |
| `missing_ch` | Not every channel fired; the shot was solved with the rest |
| `high_resid` | Residual above `rlim`; likely a bad trigger, so treat the position as suspect |
| `dropped_ch` | The solver discarded one bad channel |
| `outside_ring` | Hit is outside the sensor ring, so accuracy is reduced |
| `no_fix` | The solver failed |
| `cal` | Calibration tap |
| `sim` | Synthetic shot from `sim` |

`batt_mv` reads the Heltec battery divider, so it only means something with a LiPo connected to the board's battery connector. On a USB power bank, ignore it. If your board is a V3.2, set `kVbatCtrlOn` to `HIGH` in [board.h](src/target/board.h).

## Radio link

- **Settings:** SF8 / 125 kHz / CR 4/5 / +17 dBm / sync word 0x12, set in [OpenGongProto.h](lib/OpenGongProto/OpenGongProto.h).
- **Shot packets:** 20 bytes, about 103 ms on air (ACK: 9 bytes, about 72 ms). The receiver ACKs every copy it gets. If no ACK arrives, the target resends up to 3 times with a random back-off. Each target boot picks a random `boot` ID, and the receiver de-duplicates on (`boot`, `n`).
- **Status:** the target sends a status packet every 30 s.
- **Command responses:** sent back in 200-byte chunks.

## Host test

The solver has no hardware dependencies. [solver_sim.cpp](test/host/solver_sim.cpp) checks it in three ways:

- It reproduces the accuracy table from System-Design section 2.
- It runs a simulated calibration with unknown speed and delays.
- It checks bad-trigger rejection.

```
g++ -O2 -std=c++17 -I lib/OpenGongSolver test/host/solver_sim.cpp lib/OpenGongSolver/Solver.cpp -o solver_sim && ./solver_sim
```

Current result: 200 ns timing scatter gives a 1.1 mm mean and 2.2 mm 95th-percentile error. Calibration recovers the wave speed to within 0.1%. Without calibration, a 4% speed error plus channel delays costs up to 9 mm.

## Known limits

- **Capture overwrite:** the capture register is read in the interrupt handler. If a comparator chatters with edges less than a few µs apart, a later edge could overwrite the first before it's read. The comparator's hysteresis and the 50 kHz high-pass should prevent this; check on the scope during bring-up.
- **Not implemented:** peak-detector walk correction (System-Design section 5, optional) and Bluetooth on the receiver. Both are natural next steps if the punch test shows they're needed.
