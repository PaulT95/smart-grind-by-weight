# Grind Knob Tracking & Thermal Drift Calibration

This guide covers the **KY-024 Hall-effect sensor** integration for real-time grind knob tracking, the **6-point dial calibration** procedure, the physics behind raw ADC drift, and how to use the automated **Thermal Drift Calibration** tool.

## Video example
media\Digital_knob_installation\Demo_knob.mp4

---

## 1. Overview & Operating Principle

The grinder knob tracking system measures the physical distance between a small neodymium magnet mounted on the grind adjustment mechanism and a **KY-024 linear Hall-effect sensor** (featuring a 49E linear Hall IC) connected to ESP32 ADC1 (GPIO 5).

* **Resolution:** Tracks micro-movements (~0.1 mm mechanical travel corresponds to ~0.1 on the grind dial).
* **Piecewise Linear Interpolation:** Maps ADC readings across a 6-point calibration table spanning Dial 0.0 (burr touch) to Dial 5.0 (coarse).
* **Display Integration:**
  * **Ready Screen (Knob Tab):** Arc indicator, large dial setting, grind recommendation badge, and live ADC value.
  * **Menu (Grind Knob Page):** Full diagnostic status with Live Position, Raw ADC, Temperature-Compensated ADC, Die Temperature, and calibration controls.

---

## 2. 6-Point Dial Calibration Procedure

Calibrating the knob establishes the mapping between raw magnetic readings and physical grind dial positions.

### When to calibrate:
* After assembling or reinstalling the sensor or knob magnet.
* If you adjust your grinder's zero point (burr touch).

### Step-by-Step:
1. Power on the grinder and navigate to **Menu → Grind Knob** (found under the Tools section).
2. **Point 1 (Dial 0.0 - Touch):**
   * Turn the grinder dial until the burrs just start to touch (chirp point).
   * Tap **READ** next to **Dial 0.0 (Touch)**. The row will show `ADC: <value>` in green.
3. **Points 2 through 6 (Dial 1.0 to Dial 5.0):**
   * Rotate the dial to position `1.0`. Tap **READ** next to **Dial 1.0**.
   * Repeat for `2.0`, `3.0`, `4.0`, and `5.0`.
4. Once at least 2 points are set, knob calibration is marked valid and live tracking immediately becomes active on both the Menu and the Ready screen carousel.

> 💡 **Tip:** Calibration values are automatically saved to non-volatile flash storage (`hall` NVS namespace) and persist across power cycles.

---

## 3. The Physics of Drift & Why It Happens

If you monitor raw ADC counts immediately after turning on a cold grinder, you may notice a slow, gradual drift (for example, from `2442` down to `2437` over the first 10–15 minutes maybe even more).

### Why does this occur potentially? I tried to read around diffent post and reddist and I guess there are multiple reasons.
1. **Board & Casing Warm-Up:** The ESP32-S3 microcontroller, AMOLED display backlight, and power regulators consume power and generate heat upon boot. Electronics reach thermal equilibrium after approximately 10 to 15 minutes.
2. **Magnetic Flux vs. Temperature:** Neodymium magnets have a reversible temperature coefficient of roughly $-0.11\%/^\circ\text{C}$ (their field strength drops slightly as temperature rises).
3. **Sensor Quiescent Shift:** Linear Hall sensors (49E) have a quiescent null-voltage drift over temperature.
4. **Internal ADC Reference:** The ESP32-S3 internal voltage reference shifts slightly with die temperature.

A shift of 5 ADC counts is only **~4 mV** out of 3.3V (~0.12%), which is completely normal in precision electronics. As we used the internal 12bits adc of the esp32, however, small knob movements cover a narrow ADC span (approx. 3 counts per 0.1 dial step), an uncompensated 5-count thermal drop would shift the displayed dial reading by ~0.15–0.2! 

---

## 4. Temperature-Based Drift Compensation

### Why time-based correction fails
Correcting drift based purely on "elapsed time since power-on" has critical flaws:
* **The Warm Restart Problem:** If you make an espresso, turn the grinder off, and turn it back on 3 minutes later, the grinder is already hot. A time-based curve would reset to $t=0$, falsely assuming the machine is cold and doubling the dial error in the wrong direction.
* **Ambient Shifts:** A hot summer afternoon vs. a cold winter morning alters the starting baseline.

### How the physical temperature model works
The firmware samples the ESP32-S3 **internal on-die temperature sensor** (`temperatureRead()`). It calculates the real-time thermal offset:

$$\text{ADC}_{\text{compensated}} = \text{ADC}_{\text{raw}} - \text{round}\Big(k_{\text{temp}} \times (T_{\text{current}} - T_{\text{ref}})\Big)$$

* $k_{\text{temp}}$: Thermal drift slope in ADC counts per °C (default: `-0.25 cnt/°C`).
* $T_{\text{ref}}$: Reference calibration temperature in °C (default: `25.0°C`).
* $T_{\text{current}}$: Live ESP32 die temperature in °C.

Because this tracks physical temperature:
* **Warm restarts work seamlessly:** The MCU immediately detects it is already at ~45°C and applies the correct steady-state compensation.
* **Long runtimes never (theoretically) diverge:** Once thermal equilibrium is reached, die temperature stabilizes and the reading stays flat.

I know the ideal scenario would be to not have drift or rather using appropriate temperature sensors, but this would likely results in a bulking project. While, at the moment everything is compact within the grinder, nicely fitting. Furthermore, I am not quite sure how much current we can reliably drain from the system without interfering with the main board's ability.
However, this is a starting point for the (coffee) community. Probably smarter people will come up with better approaches.

---

## 5. Automated Drift Calibration (Step-by-Step)

The firmware includes a pre-calibrated default ($k_{\text{temp}} = -0.25\text{ cnt/}^\circ\text{C}$) that works well out of the box. If you want maximum accuracy tailored to your specific enclosure and magnet, you can run the built-in automated calibration tool.

### Sampling Details & Point Count
* **Sampling Rate:** 1 sample every 5 seconds (12 samples per minute).
* **Total Duration:** **15 minutes** (900 seconds).
* **Total Points Collected:**  **180 data points** spanning the entire warm-up curve from room temperature to operating equilibrium.
* **Live Regression:** Every single recorded point is included in the live least-squares fit.
* **Auto-Completion:** At exactly `15:00` (180 points), sampling automatically stops and freezes the completed fit. Even if you walk away and return after 30 minutes, your calibration will be waiting for you.

### The Ideal "Walk-Away" Calibration Routine:
> [!IMPORTANT]
> **Always start from a COLD grinder.**
> The calibration tool needs to observe the system warm up from room temperature to thermal equilibrium to establish a robust slope ($\Delta\text{ADC} / \Delta T$).

1. **Let the grinder cool down completely** (turned off for at least 30–45 minutes).
2. **Turn on the grinder.**
3. Navigate immediately to **Menu → Grind Knob**.
4. Set the dial to any standard position (e.g. `2.5`). **Do not touch or turn the knob during the test.**
5. Scroll down to **Thermal Drift Compensation** and tap **"Calibrate Drift"**.
6. **Walk away for 15 minutes.**
   The ESP32 will automatically:
   * Sample 180 data points (every 5s).
   * Track temperature rise from cold (e.g. 24°C) to steady operating temperature (~45°C).
   * Fit the optimal regression slope in real time across all 180 points.
7. **Come back after 15 minutes:**
   * The screen will show:  
     `✓ Calibration Complete! (180 points collected)`  
     `Fit: -0.25 cnt/C · Tap SAVE to apply`
8. Tap **SAVE**. The firmware immediately saves your custom coefficient and reference temperature to flash and activates drift compensation.

*(If you ever need to abort without saving, tap **CANCEL** at any time).*

---

## 6. Summary of Settings & Diagnostics

| Item | Location | Purpose |
| --- | --- | --- |
| **Raw ADC** | Live Position | Uncompensated raw counts directly from ADC pin GPIO 5. |
| **Comp ADC** | Live Position | Temperature-stabilized counts used for dial calculations. |
| **Die Temp** | Live Position | Live ESP32-S3 internal die temperature in °C. |
| **Enable Comp** | Thermal Drift Comp | Toggle switch to turn temperature compensation ON or OFF. |
| **Coeff** | Thermal Drift Comp | Active drift slope (e.g. `-0.25 cnt/C`). |
| **Ref Temp** | Thermal Drift Comp | Operating temperature baseline at calibration time. |
| **Calibrate Drift**| Thermal Drift Comp | Launches the guided 15-minute live sampling wizard. |
