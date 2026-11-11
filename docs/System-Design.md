# OpenGong — 5.56 Impact Multilateration System Design

Nov 11, 2026

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

Sensor placement from a 3D-printed jig (about ±0.5 mm) and changes in wave speed with temperature (under 0.5 mm) are small next to these figures. **Fix the wave speed from calibration** rather than solving it on every shot; with 5 sensors, solving it per shot was unreliable in simulation.

## 3. System architecture

**Signal chain:** piezo sensor → protection + high-pass filter + buffer → threshold comparator → ESP32-S3 hardware capture pin → position solver → LoRa radio (915 MHz) → about 500 m → receiver board → USB or Bluetooth → laptop or phone.

Each shot produces one timestamp per sensor; no waveform is recorded. The ESP32's built-in ADC is too slow to record these signals usefully, but recording isn't needed at 5 mm.

## 4. Sensors, harness and mechanical

- **Sensors:** 12–15 mm bare brass piezo discs (about $0.30 each). Smaller discs give a sharper arrival. Buy 20 so you have spares.
- **Bonding:** sand the steel to bright metal and glue the brass side down with a **thin, rigid** layer of cyanoacrylate or hard-setting epoxy (e.g., JB Weld). Keep foam, tape and plastic out of the path between steel and sensor.
- **Layout:** 5 sensors evenly spaced on a 140 mm radius, inside your 12 in (152 mm) circle. The scoring zone is inside the ring, and accuracy falls off outside it.
- **Placement jig (3D printed, PETG):** a ring with open-bottom pockets so each disc sits on bare steel, with about 0.15 mm clearance and three small locating bumps per pocket. Two stops on one plate edge and one on the adjacent edge register it to the plate, and that corner is your **calibration origin**. Don't drill or weld the AR500; heat ruins its hardness. Glue the sensors through the jig, let the glue cure, then remove the jig.
- **Printing the jig:** a 12 in ring won't fit most beds, so print it in 3–4 segments joined with dowel pins. Print a 200 mm test bar first, measure it with calipers, and scale the model to correct shrinkage.
- **Protective cover:** a separate printed piece with sensor caps, cable channels and foam pads, held to the plate by embedded neodymium magnets. Pot each sensor's solder joints in silicone.
- **Cabling:** shielded twisted pair or thin coax, under 30 cm from each sensor to the amplifier box behind the plate.
- **Plate thickness:** the sensors are on the back, so the solver includes the plate thickness in each sensor's path length.

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

1. **Positions:** take sensor coordinates from the jig design, measured from the calibration origin corner.
2. **Punch points:** add 5–10 holes at known positions to the jig, or print a separate edge-registered punch template. Fire a spring-loaded automatic center punch through each one 3–5 times.
3. **Fit:** on a laptop (Python/SciPy), fit the wave speed and each channel's fixed delay from those taps. Save the results to the target over Wi-Fi or LoRa; they're stored in the ESP32's non-volatile memory.
4. **Check:** tap a few holes not used in the fit. If all of them come out within about 2 mm, you're done.
5. **Live-fire check:** compare reported positions to the measured centers of the bullet splash marks.

## 9. Wireless link (500 m)

- **Boards:** **Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262 LoRa radio + small display, about $20) at both ends. The target board is also the timing and solving computer. US band is 915 MHz.
- **Settings:** spreading factor 7–9, 125 kHz bandwidth, +14 to +20 dBm. A 20–30 byte packet takes about 50–200 ms to send. 500 m line-of-sight is well within LoRa's range.
- **Antennas:** replace the stock stubs with proper 915 MHz dipoles. Mount the target antenna 1–2 m up, behind cover and out of the line of fire.
- **Packet:** shot number, x and y (in 0.1 mm units), residual, sensors used, battery level and a checksum. The receiver acknowledges each shot, and the target resends up to 3 times.
- **Receiver:** USB serial to a laptop, or Bluetooth to a phone. A simple Python or web app plots the hits, group size and center, and saves each session.

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
3. **Print and test-fit the jig**, then glue the 5 sensors to the real plate.
4. **Build the 5-channel board** and write the ESP32-S3 capture and solver firmware.
5. **Calibrate** with the punch template and check against held-out holes.
6. **Add the LoRa link** and receiver app, then range-test at 500 m.
7. **Live fire** at a rated distance and compare reported positions to the splash marks.
