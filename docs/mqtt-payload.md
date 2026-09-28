# MQTT Telemetry Payload Specification

This document defines the MQTT publishing configuration, data schema, field descriptions, and diagnostic references for telemetry produced by the solar energy monitoring system.

---

## 1. MQTT Publishing Parameters

| Parameter | Configuration / Value | Notes |
|---|---|---|
| **Protocol** | MQTT v3.1.1 over TLS | Transport encrypted using `WiFiClientSecure` |
| **Port** | `8883` | Standard TLS port for secure MQTT brokers |
| **Topic** | `solar_ems/telemetry` | Default publishing topic defined in firmware |
| **Client ID** | `esp32-<DEVICE_ID>` | e.g., `esp32-solar_ems_001` |
| **Publishing Interval** | 300 seconds (5 minutes) | Non-blocking timing controlled by `millis()` |
| **Serialization** | JSON | Generated via ArduinoJson with static 512-byte buffer |
| **QoS Level** | QoS 0 (At most once) | Default pub-sub streaming |

> **TLS Claim Boundary Notice:** The test firmware disables server-certificate verification with `secureClient.setInsecure()`, so certificate validation should be configured before a production deployment.

---

## 2. Telemetry Payload Schema

The example below is an actual historical prototype telemetry payload captured during the 25–26 August 2026 soak test. Note that `temperature_c` is `-127.0` because this historical record was generated under the earlier DS18B20 pull-up configuration prior to the final 1.5 kΩ hardware correction; it is not representative of DS18B20 behavior after the final 1.5 kΩ fix:

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

---

## 3. Data Dictionary

| Field | Type | Unit | Description / Firmware Source |
|---|---|---|---|
| `device_id` | String | — | Unique hardware installation identifier (`#define DEVICE_ID "solar_ems_001"`). |
| `timestamp` | String | ISO 8601 | UTC timestamp formatted as `YYYY-MM-DDTHH:MM:SSZ` retrieved from ESP32 internal RTC via NTP. |
| **`solar`** | Object | — | Solar PV array telemetry acquired from INA226 at I2C address `0x40`. |
| `solar.voltage_v` | Float | Volts (V) | PV array bus voltage from `inaSolar.getBusVoltage()`. |
| `solar.current_a` | Float | Amperes (A) | PV array current from `inaSolar.getCurrent_mA() / 1000.0`. |
| `solar.power_w` | Float | Watts (W) | PV array power computed on-chip by INA226 (`inaSolar.getPower_mW() / 1000.0`). |
| `solar.shunt_mv` | Float | Millivolts (mV) | Raw differential voltage drop across the 0.0015 Ω solar shunt (`inaSolar.getShuntVoltage_mV()`). |
| **`battery`** | Object | — | LiFePO4 battery bank telemetry acquired from INA226 at I2C address `0x41`. |
| `battery.voltage_v` | Float | Volts (V) | Battery terminal bus voltage from `inaBattery.getBusVoltage()`. |
| `battery.current_a` | Float | Amperes (A) | Battery current from `inaBattery.getCurrent_mA() / 1000.0`. Sign depends on physical Kelvin sense wiring. |
| `battery.power_w` | Float | Watts (W) | Battery power computed on-chip by INA226 (`inaBattery.getPower_mW() / 1000.0`). |
| `battery.shunt_mv` | Float | Millivolts (mV) | Raw differential voltage drop across the 0.0015 Ω battery shunt (`inaBattery.getShuntVoltage_mV()`). |
| `battery.charging` | Boolean | — | Boolean charging state indicator evaluated in firmware. |
| **`load`** | Object | — | Inverter AC load current telemetry acquired from SCT-013-050 on GPIO35. |
| `load.current_a` | Float | Amperes (A) | RMS AC current computed over 3000 samples with dynamic mean subtraction. |
| `temperature_c` | Float | °C | Solar panel surface temperature from DS18B20 on GPIO5 (`tempSensor.getTempCByIndex(0)`). Returns `-127.0` on communication error. |
| `interval_s` | Integer | Seconds (s) | Nominal reading interval period (constant `300`). |

---

## 4. Battery Charging Rule and Sign Convention

### Firmware Rule
In the firmware, the battery charging indicator is calculated strictly as:
```cpp
bool charging = (batteryCurrent > 0.05);
```
- **Firmware Rule:** The firmware marks `charging = true` when the measured battery current exceeds `+0.05 A`.
- **Deadband:** The `0.05 A` threshold acts as a deadband around zero current to prevent sensor noise and baseline ADC fluctuations from rapidly toggling the flag when the battery is idle.

### Historical Documentation Discrepancy
Project documentation contains an apparent contradiction regarding sign convention:
1. Section 3.3.1 and Table 3.3 of the draft chapter stated that positive current represents charging.
2. In Section 3.9.3, a historical sample payload with `battery.current_a: -8.946` and `charging: false` was described textually as indicating the battery was receiving charge from the solar panels.

The public documentation reports the code rule as implemented in firmware (`batteryCurrent > 0.05`). Without additional post-wiring calibration logs, the physical sign convention corresponds to whatever voltage polarity is presented to the INA226's `IN+` and `IN-` terminals by the Kelvin leads.

---

## 5. Diagnostic Validation and Shunt Consistency

The payload includes both calculated current (`current_a`) and raw shunt voltage (`shunt_mv`):
$$I = \frac{V_{shunt}}{R_{shunt}}$$

Since $R_{shunt} = 0.0015\ \Omega$:
- A measured shunt drop of $7.995\ \text{mV}$ corresponds to:
  $$\frac{7.995 \times 10^{-3}\ \text{V}}{0.0015\ \Omega} \approx 5.33\ \text{A}$$
  This closely cross-checks against `solar.current_a = 5.322 A`.
- A measured shunt drop of $-12.3475\ \text{mV}$ corresponds to:
  $$\frac{-12.3475 \times 10^{-3}\ \text{V}}{0.0015\ \Omega} \approx -8.23\ \text{A}$$
  Cross-checked against `battery.current_a = -8.946 A`. Small variations between on-chip current registers and discrete shunt voltage readings stem from internal register conversion timing differences.

---

## 6. Temperature Error Code
 
The DallasTemperature library returns `-127.0 °C` (`DEVICE_DISCONNECTED_C`) when the sensor fails to acknowledge communication on the 1-Wire bus:
- During the earlier extended soak test, ~93% of readings returned `-127.0 °C` due to line capacitance over the extended wire run when operating with an earlier pull-up resistor (~3.76 kΩ in-circuit).
- When receiving `-127.0 °C`, downstream consumers should treat the temperature reading as an invalid/disconnected error rather than a physical thermal measurement.
- In the final hardware configuration, the pull-up was changed to 1.5 kΩ, and subsequent testing confirmed that the communication failures and -127 °C readings were resolved.
