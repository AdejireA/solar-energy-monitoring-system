# Troubleshooting and Hardware Debugging History

This document details hardware and firmware engineering challenges encountered during development, along with their root-cause diagnoses, corrective modifications, and verified final status.

---

## 1. INA226 Bus Voltage Reference Misconfiguration

- **Historical Problem:** During initial bench and installation testing, the INA226 bus voltage reading was erratic, fluctuating between 0 V and a few volts without correlating to the true battery or solar array potential measured by an external multimeter.
- **Diagnosis:** The system uses low-side current sensing, placing the shunt in the circuit's negative conductor near ground potential. The INA226 `VBUS` pin had initially been jumpered directly to the shunt's `IN+` pad. Because the shunt sits at or near ground potential, `VBUS` was measuring the millivolt drop across the shunt relative to ground rather than the true supply rail potential.
- **Change Made:** The jumper from `VBUS` to `IN+` was removed. A dedicated insulated wire was routed directly from the positive supply terminal (battery positive post for battery INA226; PV positive combiner bus for solar INA226) to the `VBUS` terminal on the respective INA226 module.
- **Final Status:** **Resolved.** Following the wiring change, reported bus voltages matched external multimeter measurements within the INA226's specified ±0.1% accuracy tolerance across all operating conditions.

---

## 2. INA226 Sense Wire Polarity and Sign Convention

- **Historical Problem:** Initial bench testing of the battery circuit reported negative current values during known forward charging conditions.
- **Diagnosis:** The INA226 determines current sign strictly by the differential polarity between its `IN+` and `IN-` inputs:
  $$V_{shunt} = V_{IN+} - V_{IN-}$$
  The Kelvin sense leads from the shunt were physically inverted relative to the INA226 terminal headers.
- **Change Made:** The two Kelvin sense wires were physically transposed at the shunt's small sense screw terminals. 
- **Final Status:** **Resolved on bench; noted in production.** While the bench configuration was corrected physically, the final firmware enforces a programmatic rule:
  ```cpp
  bool charging = (batteryCurrent > 0.05);
  ```
  In production documentation, current sign is documented strictly according to the firmware rule rather than an inferred electrochemical convention.

---

## 3. DS18B20 Long-Wire 1-Wire Communication Failures

- **Symptom:** Frequent `-127.0 °C` invalid readings (the DallasTemperature library disconnected error code). During the earlier 24-hour outdoor soak test, approximately 93% of temperature readings failed.
- **Diagnosis:** 1-Wire communication instability associated with the long sensor connection (running from the rooftop solar panels down to the ESP32 enclosure) and the earlier pull-up configuration. In-circuit measurement revealed the earlier pull-up resistor had an effective resistance of 3.76 kΩ. Combined with the high parasitic capacitance of the long cable run, the RC time constant was too large, causing slow rise times that violated 1-Wire bit-timing windows.
- **Change Made:** The pull-up resistor was replaced with a **1.5 kΩ** resistor in the final physical build to sharpen transition edges and accelerate line charging.
- **Final Status:** **Resolved.** Subsequent testing after installation of the final 1.5 kΩ pull-up confirmed that the previously observed DS18B20 communication failures and -127 °C readings were resolved. (In accordance with engineering claim boundaries, no quantitative post-change reliability percentages or sample counts are claimed without surviving records; the 93% failure rate applies strictly to the pre-change hardware).

---

## 4. ESP32 ADC2 Wi-Fi Resource Conflict

- **Historical Problem:** Early firmware prototyping of the SCT-013 AC current sensor produced erratic, noisy, or zero-valued ADC readings whenever the ESP32 attempted to sample the waveform while connected to Wi-Fi.
- **Diagnosis:** The ESP32 features two internal 12-bit SAR ADCs: ADC1 and ADC2. ADC2 pins (GPIO0, 2, 4, 12–15, 25–27) share internal hardware multiplexers and SAR logic with the 802.11 b/g/n Wi-Fi subsystem. Whenever the Wi-Fi radio is active, the ESP32 hardware locks ADC2, causing `analogRead()` on ADC2 pins to fail or return corrupted values.
- **Change Made:** The SCT-013 signal conditioning circuit was reallocated to an ADC1 channel:
  - In initial revisions: Reassigned to GPIO34 (ADC1_CH6).
  - In final firmware: Standardized on **GPIO35** (ADC1_CH7), a dedicated input-only pin unaffected by Wi-Fi activity or internal pull-up/down switching.
- **Final Status:** **Resolved.** AC waveform sampling on GPIO35 operates reliably and concurrently with continuous Wi-Fi and MQTT transmission.

---

## 5. Network Time Protocol (NTP) Synchronisation Timeout

- **Historical Problem:** When deployed on restricted or mobile hotspot Wi-Fi networks, the ESP32 would occasionally hang indefinitely during initial boot, failing to progress to MQTT publishing or sensor sampling.
- **Diagnosis:** The boot routine called `configTime(0, 0, "pool.ntp.org", ...)` and blocked in a `while` loop waiting for system time to update. On networks where outbound UDP traffic on port 123 (NTP) was filtered or firewalled, the NTP packet never returned, causing an infinite stall.
- **Change Made:** The NTP synchronization routine was bounded to a maximum of 20 polling attempts (10 seconds total) with a timeout escape:
  ```cpp
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 100000 && attempts < 20) {
    delay(500);
    now = time(nullptr);
    attempts++;
  }
  if (now < 100000) {
    Serial.println("Time sync failed, continuing without accurate time");
  }
  ```
- **Final Status:** **Resolved.** If NTP synchronization fails, the system logs a diagnostic message and proceeds to publish telemetry using default RTC timestamps rather than locking up the device.
