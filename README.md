# OpenGong — Multilateration Electronic Target

Five piezo sensors bonded to the back of an AR500 steel plate locate 5.56×45 mm bullet impacts to within about 5 mm by time-difference-of-arrival multilateration. An ESP32-S3 (Heltec WiFi LoRa 32 V3) timestamps each sensor in hardware, solves the impact position, and sends the coordinates over LoRa to a receiver up to ~500 m away.

## Documents

- [System design](docs/System-Design.md): architecture, accuracy budget, sensors, solver, calibration, radio link, safety, bill of materials and build plan.
- [Wiring diagram](docs/Wiring-Diagram.md): system wiring, per-channel circuit, shared bias/threshold/power, Heltec pin map and channel-to-part map.
- [Placement jig](Placement%20Jig/README.md): the OpenSCAD sensor and punch-guide jig for rectangular and round plates, covering parameters, export, printing, assembly and calibration points.
- [Firmware](firmware/README.md): ESP32-S3 target (hardware capture, solver, on-board calibration, LoRa) and receiver firmware, with build, bring-up and calibration steps.

![System wiring](docs/images/system-wiring.svg)

## Safety

Follow your plate maker's rating for 5.56 (commonly at least 3/8 in AR500, at least 100 yd, no steel-core or penetrator rounds). Keep all electronics and cables fully behind the plate under a steel shield.
