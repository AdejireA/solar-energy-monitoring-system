# Testing and Empirical Validation

This document records the empirical testing procedures, validation methods, and observational findings from laboratory bench testing and an extended operational soak test.

---

## 1. Testing Methodology and Validation Boundaries

Testing was conducted across three distinct phases:
1. **Bench Testing:** Controlled validation on an electronics bench using an artificial resistive/LED load, systematic Ohm's Law cross-checks, and connection/polarity troubleshooting.
2. **Historical Extended Soak Testing:** Field deployment on the live solar installation over an uninterrupted ~25-hour cycle (25–26 August 2026) under the earlier hardware configuration (including the earlier ~3.76 kΩ DS18B20 pull-up).
3. **Final DS18B20 Hardware Correction:** Physical installation of the final 1.5 kΩ pull-up resistor and subsequent validation testing confirming issue resolution.

> **Validation Claim Boundary:** The soak test demonstrates end-to-end subsystem integration, communication resilience, and physically plausible telemetry tracking under operational conditions. These results **do not constitute a certified metrological calibration study** against reference standards. Sensor accuracy figures cited in documentation refer to component manufacturer datasheet ratings, not accredited laboratory calibration.

---

## 2. Phase 1: Bench Testing

Before connection to high-capacity LiFePO4 batteries or live solar arrays, the hardware and firmware pipeline was tested on an electronics bench using a low-voltage DC power supply and an LED/resistive dummy load:

### 2.1 Ohm's Law Cross-Check
To verify sensor reporting integrity independently of firmware conversions, calculated current was systematically verified against raw shunt voltage using Ohm's Law:
$$I = \frac{V_{shunt}}{R_{shunt}} = \frac{V_{shunt}}{0.0015\ \Omega}$$

### 2.2 Fault Detection During Bench Setup
- **Intermittent Wiring Fault:** During bench testing, Ohm's law checks exposed a discrepancy between the INA226 reported current register and the discrete shunt drop. Inspection revealed a loose screw terminal on the Kelvin sense lead. Once tightened, readings reconverged.
- **Sense Wire Polarity Swap:** Initial bench runs showed negative current during intentional forward current flow. Polarity was corrected by swapping the two Kelvin sense leads at the shunt terminals.
- **VBUS Reference Correction:** Initial bench wiring mistakenly jumpered `VBUS` to the low-side shunt's `IN+` pad, yielding near-zero voltage. Re-routing `VBUS` directly to the power supply positive terminal restored correct bus voltage tracking.

---

## 3. Phase 2: Historical Extended System Soak Test (25–26 August 2026)

An extended operational soak test was conducted on the full solar installation from **16:25 UTC on 25 August 2026** through **17:16 UTC on 26 August 2026** (approximately 24 hours, 51 minutes). This test reflected the **earlier prototype hardware configuration**, specifically featuring the earlier 1-Wire pull-up resistor.

Telemetry was published by the ESP32 at 300-second (5-minute) intervals over TLS MQTT (port 8883) and captured by an independent Python subscriber logging to disk. Over 100 consecutive data points were successfully received and archived.

### 3.1 Observational Findings

| Subsystem | Metric | Observed Range | Physical Behavior / Interpretation |
|---|---|---|---|
| **Solar PV Array** | Bus Voltage | 0.5 V to 25.0 V | Tracked daylight cycle: 22–24 V in late afternoon; progressively dropped as sunlight faded (22.7 V at 17:31 UTC $\rightarrow$ 14.0 V $\rightarrow$ 9.5 V $\rightarrow$ 5.2 V $\rightarrow$ 2.0 V $\rightarrow$ <1 V by ~18:16 UTC); stabilized near 0.5 V overnight; climbed at dawn from 17.8 V (07:46 UTC) to 20.4 V $\rightarrow$ 22.4 V $\rightarrow$ 23–25 V by midday. |
| **Solar PV Array** | Current | 0.0 A to 5.32 A | 0.0 A overnight; ranged between 2.8 A and 5.32 A during daylight hours on 26 August. |
| **Solar PV Array** | Power | 0.0 W to 120.0 W | Zero overnight; peaked between 49 W and 120 W during midday insolation. |
| **Battery Bank** | Bus Voltage | 13.28 V to 13.52 V | Remained stable within the expected operating plateau for a 12.8 V nominal LiFePO4 bank (13.28 V overnight under load, rising to 13.52 V during peak daylight charging). |
| **AC Load** | RMS Current | 0.30 A to 0.98 A | Remained within plausible household appliance operating ranges throughout the test. |
| **Panel Temp** | Temperature | 22.0 °C to 34.1 °C *(valid samples only)* | Valid samples tracked ambient day/night thermal patterns, peaking at 34.1 °C during midday. (See communication reliability note below). |

---

## 4. Temperature Sensor Long-Wire Analysis & Hardware Correction

The engineering chronology for the DS18B20 digital temperature sensor is detailed below:

### 4.1 Observations from the Earlier Soak Test
During the 25–26 August 2026 soak test under the earlier hardware configuration:
- Approximately **93% of received temperature readings returned `-127.0 °C`** (the standard error code indicating no device response on the 1-Wire bus).
- Only approximately **7% of readings returned valid values** (ranging between 22 °C and 34.1 °C).

### 4.2 Root Cause Diagnosis
- The physical wiring ran from the ESP32 enclosure at ground level up to the roof-mounted panel array over a relatively long cable run.
- The installed pull-up resistor in this test measured **3.76 kΩ** in-circuit (nominally 4.7 kΩ affected by component tolerance).
- Over the extended wiring distance, parasitic cable capacitance increased significantly. The 3.76 kΩ pull-up was insufficient to charge line capacitance quickly enough to meet the microsecond-level timing windows required by the Dallas 1-Wire protocol, causing frequent communication dropouts.

### 4.3 Final Hardware Correction and Validation
- **Hardware Modification:** The pull-up resistor on the 1-Wire bus was replaced with a **1.5 kΩ** resistor, establishing the final physical hardware configuration. The lower resistance provides faster rise times across the cable capacitance.
- **Validation Status:** Subsequent testing after installation of the final 1.5 kΩ pull-up confirmed that the previously observed DS18B20 communication failures and -127 °C readings were resolved.
- **Claim Boundary:** In keeping with engineering evidence boundaries, because exact post-change sample counts and durations were not logged in a formal standalone record, no quantitative post-change failure percentage (e.g., 0%) or duration is asserted. The 93% invalid-reading observation applies strictly to the pre-change configuration.

---

## 5. Historical Prototype Telemetry Sample

The sample below is actual historical prototype telemetry captured during active solar generation at **07:56:09 UTC on 26 August 2026** during the extended soak test:

```json
{
  "device_id": "solar_ems_001",
  "timestamp": "2026-08-26T07:56:09Z",
  "solar": {
    "voltage_v": 22.36,
    "current_a": 5.322,
    "power_w": 119.05,
    "shunt_mv": 7.995
  },
  "battery": {
    "voltage_v": 13.40125,
    "current_a": -8.946,
    "power_w": 119.85,
    "shunt_mv": -12.3475,
    "charging": false
  },
  "load": {
    "current_a": 0.761626
  },
  "temperature_c": -127.0,
  "interval_s": 300
}
```

### Analysis of Captured Data Point:
1. **Historical Telemetry Context:** This payload represents historical telemetry captured under the **earlier DS18B20 hardware configuration** prior to the 1.5 kΩ pull-up correction. Consequently, `temperature_c` reads `-127.0 °C`, exemplifying the long-wire communication failure observed before the hardware fix. It must **not** be interpreted as representative of sensor behavior after the final 1.5 kΩ pull-up modification.
2. **Solar Array Telemetry:** Generating $119.05\ \text{W}$ at $22.36\ \text{V}$ and $5.322\ \text{A}$. Shunt voltage ($7.995\ \text{mV}$) confirms current ($7.995 / 0.0015 = 5.33\ \text{A}$).
3. **Battery Bank Telemetry:** Bus voltage at $13.40\ \text{V}$. Current registers $-8.946\ \text{A}$, evaluating `charging: false` per the literal firmware rule (`batteryCurrent > 0.05`).
4. **AC Load Telemetry:** Drawing $0.762\ \text{A}$ RMS from the inverter.
