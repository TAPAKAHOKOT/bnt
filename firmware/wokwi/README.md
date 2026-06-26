# Wokwi wiring diagram

Editable GUI version of the BNT breadboard wiring (mirrors `firmware/README.md`
and the pins in `firmware/src/main.cpp`).

ESP32 DevKit + tactile button are native Wokwi parts. INMP441 and MAX98357 are
not in the Wokwi library, so they are provided here as **custom chips** — plain
labeled blocks with the correct pin names (no audio emulation, wiring view only).

## Open in Wokwi

1. Go to https://wokwi.com → **New Project** → pick **ESP32**.
2. Create the two custom chips. For each one, click the **+** (Add file) and add
   both files with the exact names:
   - `inmp441.chip.json` and `inmp441.chip.c`
   - `max98357.chip.json` and `max98357.chip.c`

   Paste the contents from this folder.
3. Open the `diagram.json` tab and replace its contents with the `diagram.json`
   from this folder.
4. (Optional) Paste your firmware into `sketch.ino` to also simulate logic.

The four parts appear wired up; drag parts/wires in the GUI to rearrange, then
copy the updated `diagram.json` back here if you want to keep changes.

## Pin map (must match `main.cpp`)

| Signal      | ESP32 | INMP441 | MAX98357 |
|-------------|-------|---------|----------|
| Button      | GPIO13 (INPUT_PULLUP) → button → GND | — | — |
| I2S in SCK  | GPIO26 | SCK | — |
| I2S in WS   | GPIO25 | WS  | — |
| I2S in SD   | GPIO33 | SD  | — |
| Mic L/R     | GND    | L/R → GND (left channel) | — |
| Mic power   | 3V3    | VDD | — |
| I2S out BCLK| GPIO27 | — | BCLK |
| I2S out LRC | GPIO22 | — | LRC |
| I2S out DIN | GPIO21 | — | DIN |
| Amp enable  | GPIO14 | — | SD (HIGH = on) |
| Amp power   | VIN/5V | — | Vin (3V3 also works) |
| Grounds     | GND    | GND | GND |
