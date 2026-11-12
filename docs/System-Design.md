# OpenGong — 5.56 Impact Multilateration System Design

Nov 11, 2026 (jig section revised: one ring for rectangular and round plates)

## 1. Summary

The system meets a **5 mm accuracy target** with simple, cheap parts. It uses **5 piezo sensors** (6 optional) bonded to the back of the plate, **plain threshold comparators** whose outputs are hardware-timestamped by an **ESP32-S3**, and a **LoRa link** to a matching receiver at the firing line. Parts cost is roughly **$80–110**, and the two radio boards are the biggest item.

- **Expected accuracy:** about 1–3 mm typical, inside 5 mm for 95% of hits within the sensor ring. These are estimates; the punch test confirms them.
- **What sets the accuracy:** large timing mistakes, not timing precision. The ESP32-S3 timestamps to 12.5 ns, far finer than needed. The real risk is a channel triggering on the wrong part of the wave. Section 11 covers how to prevent that.
- **Compared to the reference paper:** Waterman/Salazar aimed for 5 mm and got about 20 mm. This design removes their main error sources: software-polled timing, long unbuffered sensor cables and an unknown wave speed.

## 2. Accuracy budget

Timing scatter is what matters. To stay under 5 mm, each sensor's trigger time needs to be consistent to about **400 ns or better**. The design aims for about 200 ns, which leaves a 2× margin.

Monte Carlo result: 5 sensors on a 140 mm radius pentagon, 3/8 in (9.5 mm) plate, impacts spread evenly within 130 mm of center. Wave speed is 5,900 m/s, fixed from calibration.

| Per-sensor timing scatter (1σ) | Average error | 95% of hits within |
| --- | --- | --- |
| 100 ns | 0.6 mm | 1.1 mm |
| 200 ns | 1.3 mm | 2.4 mm |
| 500 ns | 3.2 mm | 5.7 mm |
| 1,000 ns | 6.1 mm | 11.0 mm |

**Re-run for the 120 mm ring** (the default, because it fits both rectangular and 12 in round plates; see 4.1): at 200 ns, hits within 110 mm of center average 1.1 mm and 95% fall within 2.2 mm, the same as the 140 mm ring inside its own zone. Hits out to 130 mm average 1.3 mm, with 95% within 2.9 mm, so accuracy falls off gently beyond the ring.

Sensor placement from a 3D-printed jig (about ±0.5 mm) and changes in wave speed with temperature (under 0.5 mm) are small next to these figures. **Fix the wave speed from calibration** rather than solving it on every shot; with 5 sensors, solving it per shot was unreliable in simulation.

## 3. System architecture

**Signal chain:** piezo sensor → protection + high-pass filter + buffer → threshold comparator → ESP32-S3 hardware capture pin → position solver → LoRa radio (915 MHz) → about 500 m → receiver board → USB or Bluetooth → laptop or phone.

Each shot produces one timestamp per sensor; no waveform is recorded. The ESP32's built-in ADC is too slow to record these signals usefully, but recording isn't needed at 5 mm.

## 4. Sensors, harness and mechanical

- **Sensors:** 12–15 mm bare brass piezo discs (about $0.30 each). Smaller discs give a sharper arrival. Buy 20 so you have spares.
- **Bonding:** sand the steel to bright metal and glue the brass side down with a **thin, rigid** layer of cyanoacrylate or hard-setting epoxy (e.g., JB Weld). Keep foam, tape and plastic out of the path between steel and sensor.
- **Layout:** 5 sensors evenly spaced on a **120 mm radius** pentagon. A spoke points straight up and sensor S4 sits straight down (back-face view). The scoring zone is inside the ring. A rectangle-only build can use a 140 mm ring (`ring_r = 140`), but it won't fit a 12 in round plate.
- **Placement jig:** see 4.1. Don't drill or weld the AR500; heat ruins its hardness.
- **Protective cover:** a separate printed piece with sensor caps, cable channels and foam pads, held to the plate by embedded neodymium magnets. Pot each sensor's solder joints in silicone.
- **Cabling:** shielded twisted pair or thin coax, under 30 cm from each sensor to the amplifier box behind the plate.
- **Plate thickness:** the sensors are on the back, so the solver includes the plate thickness in each sensor's path length. Store the thickness per plate in firmware.

### 4.1 Placement jig (PETG, model: `opengong_jig.scad`)

The jig has two parts: a **core ring**, which sets every sensor and punch position, and a **registration adapter**, which only locates the ring on a particular plate. Rectangular and round plates use the same ring and the same coordinates; only the adapter changes.

**Coordinate frame.** The origin is the **ring center** (not a plate corner), and +Y is up as the plate hangs. The model is drawn as seen from the back face, where the jig sits. The solver uses front-view coordinates: **x_front = −x_model**, y unchanged. The OpenSCAD file prints the front-view coordinates of every sensor and punch point to the console.

**Core ring (5 segments + hub)**
- **Segments:** each holds one sensor pocket (engraved S1–S5) and one spoke. Segment S1 carries an "up" arrow on the top spoke. Cuts fall 18° from both sensors and spokes, so no pocket is split. Each segment fits in about 129 × 129 mm, so the ring prints on any common bed.
- **Joints:** full-height puzzle knobs, an 8 mm knob on a 4.5 mm neck with 0.15 mm clearance, at ring cuts, spoke-to-hub and lug-to-adapter. They print flat with no supports and lock in-plane. Press them together on a flat surface; a drop of CA is optional.
- **Sensor pockets:** the bore is the disc diameter + 1.2 mm, with three 2 mm ribs at 120° that stand 0.6 mm proud. This leaves 0.15 mm radial clearance on the disc. A 0.8 mm chamfer at the plate face keeps glue squeeze-out from locking the jig. Bosses are 10 mm tall to give a pressing surface. Lift the jig off over the leads before connecting them, or solder after removal.
- **Ring:** 20 mm wide band, 6 mm thick, with 8 mm spokes and a 24 mm hub.
- **Punch guides:** bore = the nose diameter of your automatic center punch (default 8 mm), with 12 mm tall bosses. Fitting points: the center plus one per spoke at 60 mm. Held-out points (square bosses) sit at 92 mm on three spokes.
- **Lugs:** one per spoke, outside the ring, each with a puzzle socket. Every adapter plugs in here, so its position relative to the ring is fixed.

**Rectangular plate adapter (2 pieces)**
- **Bottom piece:** plugs into the two lower lugs. It has a 200 mm bar along the bottom edge with **2 stops** at its ends.
- **Side piece:** plugs into one side lug and has **1 stop** on that edge. `rect_side` chooses which edge.
- **Settings:** `rect_center_from_bottom` and `rect_center_from_side` set where the ring center sits on the plate. The defaults center it on a 12 × 12 in plate. Print one small pair per plate size; the ring is reused.
- **Use:** push the jig down and into the corner so all three stops touch.

**Round plate adapter (3 hooks)**
- **Hooks:** two rigid hooks on the lower lugs and one **flex hook** on the top lug. The flex hook's 1.2 mm beam has 0.4 mm interference with the rim, so it pushes the plate onto the two rigid hooks. That centers the ring despite ±1 mm variation in cut diameter.
- **Rotation:** a round plate has no edge that sets rotation. Line up the sight groove and V-notch on the top hook with the hanger hole's center or a paint mark on the rim. A 1 mm misalignment at the rim rotates coordinates by about 0.4°, which is under 0.7 mm at 100 mm.
- **Size:** `plate_d` sets the plate diameter. 12 in is the smallest round plate a 120 mm ring fits; the file prints a warning if the hooks would collide with the lugs.

**Printing and use**
1. Print a 200 mm test bar (`part = "test_bar"`), measure it and scale the model to correct shrinkage.
2. Print the 5 ring segments and the hub with the flat face down. Print the adapters for your plate; they export already flipped so they print without supports.
3. Assemble the ring on a flat surface, add the adapter and seat it on the sanded back face.
4. Glue the discs through the pockets. Let the glue cure, then lift the jig off.
5. Leave the jig on (or re-seat it) for the punch calibration in section 8.

## 5. Analog front end (per channel)

1. **Protection:** a 10 kΩ series resistor and BAV99 clamp diodes to the supply rails, plus a 1 MΩ bias resistor. Piezos can produce over 100 V on impact.
2. **High-pass filter at about 50 kHz:** removes slow plate bending, stand and chain rattle, and wind noise.
3. **Buffer/gain:** a fast single-supply op-amp such as the OPA2350 (dual), gain 2–10, biased at mid-rail (1.65 V).
4. **Threshold comparator:** TLV3502 (dual, fast) or a cheaper LMV393, with a little hysteresis. One shared threshold for all channels, set with a trim pot or an ESP32 PWM pin through an RC filter (the ESP32-S3 has no DAC). Set it a few times above the noise floor. A low threshold keeps the trigger close to the true wave arrival.
5. **Optional peak detector** (diode + capacitor + bleed resistor) read by the ESP32's ordinary ADC after each shot. It records how strong each channel's signal was, so software can correct strong-versus-weak trigger delay if the punch test shows it's needed. It costs a few cents per channel.

## 6. Timing: ESP32-S3 hardware capture

- The ESP32-S3 has 2 MCPWM units with 3 capture inputs each, so **6 hardware-timestamped inputs** at 12.5 ns resolution. The timestamp is taken by hardware the instant the comparator fires, so there's no software delay.
- **Firmware flow:**
    1. The first capture opens a 200 µs window. Waves cross the 12 in circle in well under 100 µs.
    2. Collect the other channels and subtract the earliest time.
    3. Solve for position, then transmit.
    4. Ignore the plate for about 250 ms while it stops ringing, then re-arm.
- **Missing channel:** if one channel doesn't fire within the window, solve with the rest (the minimum is 4) and flag the shot.

## 7. Solver

- **Model:** each sensor's arrival time = shot time + that channel's fixed delay + path length ÷ wave speed. The path length includes the plate thickness.
- **Unknowns per shot:** x, y and the shot time. Sensor positions, wave speed and channel delays come from the jig and calibration.
- **Method:** standard nonlinear least squares (Levenberg–Marquardt), starting near the sensor that fired first. It runs in well under a millisecond on the ESP32-S3, so the target sends finished coordinates.
- **Quality check:** send the leftover timing error (residual, in ns) with each shot. A large residual flags a bad trigger. With a 6th sensor, the solver can drop the worst channel automatically.

## 8. Calibration

1. **Positions:** take sensor coordinates from the jig model, in front-view coordinates with the origin at the ring center. Defaults for the 120 mm ring:

    | Sensor | x (mm) | y (mm) |
    | --- | --- | --- |
    | S1 | −70.53 | 97.08 |
    | S2 | 70.53 | 97.08 |
    | S3 | 114.13 | −37.08 |
    | S4 | 0.00 | −120.00 |
    | S5 | −114.13 | −37.08 |

2. **Punch points:** fitting points are P0 at (0, 0), P1 (0, 60), P2 (57.06, 18.54), P3 (35.27, −48.54), P4 (−35.27, −48.54) and P5 (−57.06, 18.54). Held-out points are H1 (87.50, 28.43), H2 (54.08, −74.43) and H4 (−87.50, 28.43). Fire a spring-loaded automatic center punch through each guide 3–5 times.
3. **Punch-face correction:** taps through the jig start on the back face, the same face as the sensors. For these taps, set the plate thickness to zero in the fit. Live shots then use the real thickness.
4. **Fit:** on a laptop (Python/SciPy), fit the wave speed and each channel's fixed delay from the P taps. Save the results to the target over Wi-Fi or LoRa; they're stored in the ESP32's non-volatile memory. Recalibrate for each plate.
5. **Check:** tap the H points. If all of them come out within about 2 mm, you're done.
6. **Live-fire check:** compare reported positions to the measured centers of the bullet splash marks.

## 9. Wireless link and range

**Summary:** the design range is **500 m**. With the default settings and antennas 1–2 m off the ground, the estimated link margin at 500 m over open ground is about **40 dB**, enough for a reliable link with room for brush and people in the path. On flat, open ground the estimated limit is **2–4 km**. These figures come from the link budget below, not from field tests; range-test before relying on them.

- **Boards:** **Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262 LoRa radio + small display, about $20) at both ends. The target board is also the timing and solving computer. US band is 915 MHz.
- **Settings:** SF8, 125 kHz bandwidth, coding rate 4/5, +17 dBm by default (in `OpenGongProto.h`). SF7–9 and +14 to +20 dBm are the design range. Both boards must use the same settings, so reflash both after a change.
- **Antennas:** replace the stock stubs with proper 915 MHz dipoles. Mount the target antenna 1–2 m up, behind cover and out of the line of fire (see *Antenna height and placement*).
- **Packet:** a 20-byte shot packet: shot number, x and y (in 0.1 mm units), residual, sensors used, battery level and a checksum. The receiver acknowledges each shot with a 9-byte ACK, and the target resends up to 3 times.
- **Receiver:** USB serial to a laptop, or Bluetooth to a phone. A simple Python or web app plots the hits, group size and center, and saves each session.

### 9.1 Time on air and shot rate

Time on air at 125 kHz with an 8-symbol preamble, explicit header and CRC:

| | SF7 | **SF8 (default)** | SF9 |
| --- | --- | --- | --- |
| Shot packet (20 B) | 57 ms | **103 ms** | 185 ms |
| ACK (9 B) | 41 ms | **72 ms** | 144 ms |
| Shot + ACK exchange | ~100 ms | **~180 ms** | ~330 ms |
| Receiver sensitivity (SX1262, typical) | about −124 dBm | **about −127 dBm** | about −129 dBm |
| Lowest SNR it can decode | −7.5 dB | **−10 dB** | −12.5 dB |

- **Keeping up with fire:** the target can register a hit every 250 ms (the hold-off). At SF7 or SF8 the link clears each shot before the next can arrive. At SF9 a fast string gets ahead of the radio. The 16-message transmit queue absorbs about 15 shots, and the backlog then drains at about 3 shots/s.
- **When the receiver doesn't answer:** an unacknowledged shot takes 4 attempts, about 1.6 s at SF8, and is then dropped. It is still printed on the target's USB port and shown on its OLED. Shots aren't stored for resending later, and once the queue is full, new shots are dropped.

### 9.2 Link budget (SF8, +17 dBm)

| Item | Value |
| --- | --- |
| Transmit power | +17 dBm |
| Antenna gain, both ends (dipoles) | +4 dBi |
| Cable and connector losses, both ends | −2 dB |
| Receiver sensitivity | −127 dBm |
| **Maximum path loss the link can tolerate** | **≈146 dB** |

Estimated path loss over 500 m at 915 MHz:

| Conditions | Path loss | Margin |
| --- | --- | --- |
| Free space (both antennas high above ground) | 86 dB | 60 dB |
| Flat open ground, both antennas 1.5 m up | ≈101 dB | ≈45 dB |
| Flat open ground, both antennas 1 m up | ≈108 dB | ≈38 dB |

Real ranges add losses the table doesn't include. Each of these costs extra path loss:

- **Brush or trees** in the path: about 10–20 dB.
- **People or vehicles** between the antennas: about 5–10 dB.
- **Steel** (the plate or splash shield) directly between the antennas: 20 dB or more. The link may not work at all.

Keep at least **10 dB** of margin for fading and changes from day to day. Under that rule, the estimated limit on flat, open ground is about 2.5 km with antennas 1 m up and about 4 km at 1.5 m. Rolling terrain, berms and vegetation usually limit range well before that, so plan on about 1 km unless your own test shows more.

### 9.3 Antenna height and placement

- **Height matters most.** At 500 m the radio path needs about 6 m of clearance above the ground at its midpoint, the first Fresnel zone. Antennas 1–2 m up can't provide it, so the signal falls off with ground reflections rather than as in free space. In this case, doubling the height of one antenna gains about 6 dB, and doubling both gains about 12 dB. A 2–3 m pole at the firing line is the cheapest range upgrade.
- **Keep steel out of the path.** The target antenna has to be out of the line of fire, but the plate and shield must not sit between it and the firing line. Mount it above the shield on a short mast, or a few metres to one side behind cover, using a coax run to the board.
- **Vertical and parallel:** stand both dipoles vertically. A mismatch in angle (polarization) between them can cost 10–20 dB.
- **Clear of the body and metal:** keep the receiver antenna off the bench and away from vehicles, and not where people stand.

### 9.4 Choosing a spreading factor

- **SF8 (default):** a good balance for 500 m to 1 km.
- **SF7:** for short ranges with a strong signal (SNR above about +5 dB). It gives faster exchanges and less airtime.
- **SF9:** gives about 2.5 dB more sensitivity than SF8, for longer or blocked paths. Raising the antenna height usually gains more and doesn't slow down shot reporting.
- **Transmit power:** raising it from +17 to +20 dBm gains 3 dB, and lowering it to +14 dBm saves battery when there's plenty of margin.

### 9.5 Regional rules

- **US (902–928 MHz):** +17 dBm with ordinary dipoles is well under the 1 W limit, but the operating rules depend on how the device is certified. Check the rules that apply to your setup.
- **EU (868 MHz):** set `kLoraFreqMHz` to 868. The common 868.0–868.6 MHz band allows **25 mW ERP (+14 dBm)** and a **1% duty cycle**. Lower `kLoraPowerDbm` to 14 or less to stay within it. At SF8, 1% (36 s of airtime per hour) allows about 300 acknowledged shots per hour, which is ample for normal shooting.

### 9.6 Range test

Before live fire:

1. Set up both boards in their final positions.
2. At the receiver, type `sim P0` into its serial monitor. The command is relayed to the target, and the target sends back a synthetic shot.
3. Read the result:
   - Each shot line from the receiver includes `rssi` and `snr`.
   - The target's `stats` command shows the counts of sent, acked and failed shots, plus the last RSSI and SNR.
4. Check whether the link is healthy:

| Check | Healthy link at SF8 |
| --- | --- |
| SNR | 0 dB or better, which is at least 10 dB above the −10 dB decode limit |
| Failed shots | None |
| RSSI | Above about −115 dBm |

5. Repeat with people standing in their usual positions.
6. If the margin is thin:
   - First, raise the antennas.
   - Then, move to SF9 or a higher transmit power.

## 10. Safety (read before shooting)

- **Plate rating:** commonly cited guidance for 5.56 on AR500 is at least 3/8 in plate, at least 100 yd with M193-type ammunition, and **no steel-core or penetrator rounds** such as M855 or M855A1. Follow your plate maker's rating, which takes precedence.
- **Angle:** hang the plate with its face tilted 15–20° downward to send bullet splash into the ground. The coordinates are in the plate's own plane, so tilt doesn't affect them.
- **Splash protection:** bullet fragments spray outward along the plate face. Keep **all electronics and cables fully behind the plate** under a steel shield such as 1/4 in steel angle, and route cables down and back.
- **Hanging:** chains or straps are fine. Rubber-isolated hangers shorten ringing and allow a faster re-arm.

## 11. Main risks and fixes

- **Triggering on different waves (biggest risk):** an impact sends a weak fast wave and a strong slow wave through the plate. If near sensors catch the fast wave and far sensors only the slow one, a shot can be off by centimeters. Fixes: a low threshold, enough gain that every channel catches the fast wave, and the residual check to flag shots where this happens.
- **Strong-versus-weak trigger delay:** a weak signal crosses the threshold a little later than a strong one. At 5 mm this should stay within budget; if not, add the peak-detector correction (section 5).
- **False triggers:** from fragments hitting the stand or shots on nearby steel. Require at least 4 channels to fire within the 200 µs window, and reject shots with large residuals.
- **Sensor debonding** from repeated shock: watch each channel's residuals over time and re-glue a sensor whose numbers drift.
- **Plate cratering:** damage changes how waves travel locally. Inspect the plate and replace it when worn.

## 12. Bill of materials (approximate)

| Item | Qty | ~Cost |
| --- | --- | --- |
| Heltec WiFi LoRa 32 V3 (target + receiver) | 2 | $40 |
| 12–15 mm piezo discs | 20 | $5 |
| OPA2350 dual op-amp | 3 | $9 |
| TLV3502 dual comparator (or LMV393 to save money) | 3 | $3–9 |
| Trim pot, BAV99 diodes, resistors, capacitors | — | $5 |
| Custom PCB (JLCPCB/PCBWay, 5 pcs) or perfboard | 1 | $5–15 |
| Shielded cable, connectors, epoxy | — | $10 |
| 915 MHz dipole antennas | 2 | $10 |
| Neodymium magnets, filament for jig and cover | — | $5 |
| USB power bank | 1 | $0–20 |
| **Total** | | **~$80–110** (plus power bank and steel shield) |

A cheap USB oscilloscope (about $60–80) is optional but very helpful for seeing the sensor signals during bring-up.

## 13. Build plan

1. **Bench test:** glue 2–3 sensors to a scrap AR500 offcut. Strike it with a center punch and look at the signals with a scope. Confirm the fast first arrival is visible on every sensor.
2. **Breadboard 2 channels** of the front end. Check that trigger times repeat within about 200 ns for repeated strikes at the same spot.
3. **Print the jig:** the test bar first, then the ring and the adapter for your plate shape. Test-fit it, then glue the 5 sensors to the real plate (4.1).
4. **Build the 5-channel board** and write the ESP32-S3 capture and solver firmware.
5. **Calibrate** with the punch guides and check against the held-out points.
6. **Add the LoRa link** and receiver app, then range-test at 500 m.
7. **Live fire** at a rated distance and compare reported positions to the splash marks.