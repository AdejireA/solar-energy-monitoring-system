# Hardware Specifications and Implementation

This document details the hardware components, electrical wiring topology, pin mapping, and conditioning circuits for the ESP32 solar energy monitoring prototype.

---

## 1. System Overview and Monitored Installation

The monitoring system is an observational data-acquisition unit deployed on an off-grid solar installation comprising:
- **Solar PV Array:** Parallel-configured photovoltaic panels operating up to ~25 V open-circuit.
- **Battery Storage:** 12.8 V nominal Lithium Iron Phosphate (LiFePO4) battery bank.
- **Inverter:** All-in-one solar inverter integrating charge controller and AC inverter functionality.
- **AC Load:** Standard 230 V / 50 Hz household electrical loads supplied by the inverter.

> **System Scope Notice:** The monitoring system is strictly an observational telemetry unit. It performs no charge management, switching, protective disconnection, or active inverter control, and must not be used as a Battery Management System (BMS) or certified billing meter.

---

## 2. Component Bill of Materials (BOM)

| Component | Part / Model | Specifications | Role in System |
|---|---|---|---|
| Microcontroller | ESP32-WROOM-32 | Dual-core Tensilica Xtensa 32-bit LX6 @ 240 MHz, 520 KB SRAM, 802.11 b/g/n Wi-Fi, 12-bit SAR ADCs | Master controller, sensor acquisition, NTP synchronization, JSON serialization, and TLS MQTT publishing |
| DC Power Monitors (×2) | INA226 Breakout Boards | I2C interface, 16-bit delta-sigma ADC, 0–36 V common-mode bus voltage, ±81.92 mV differential shunt range | On-chip bus voltage, shunt voltage, current, and power calculation for Battery and Solar PV circuits |
| Precision Shunts (×2) | FL-2 External DC Shunts | 50 A rated current, 75 mV drop at rated current ($R_{shunt} = 0.0015\ \Omega$), Kelvin sense terminals | Low-side current-to-voltage conversion on battery and solar negative rails |
| AC Current Sensor | SCT-013-050 | Non-invasive split-core current transformer, 50 A input, 1.0 V RMS output at rated current, internal burden resistor | Inverter AC output line current measurement |
| Temperature Sensor | DS18B20 | Waterproof probe, Maxim 1-Wire interface, 9–12 bit selectable resolution, -55 °C to +125 °C range (±0.5 °C accuracy from -10 °C to +85 °C) | Solar panel surface temperature measurement |
| 1-Wire Pull-Up Resistor | 1.5 kΩ Metal Film Resistor | 1.5 kΩ, 1/4 W (final physical build) | Bus pull-up for 1-Wire communication over extended cable run |
| AC Bias Conditioning | Voltage Divider + Decoupling | 2 × 10 kΩ resistors (1% tolerance) + 10 µF electrolytic capacitor | Shifts SCT-013 AC swing to a stable 1.65 V DC midpoint for ESP32 ADC |
| Power Supply | USB 5 V DC Adapter | 5 V DC, 1 A minimum | Supplies ESP32 via micro-USB port; onboard regulator outputs 3.3 V for sensors |

---

## 3. Microcontroller Pin Mapping

The pin assignments below reflect the **final firmware configuration**:

| ESP32 Pin | Connected Subsystem / Signal | Bus / Interface | Direction | Description |
|---|---|---|---|---|
| **GPIO21** | INA226 (Solar 0x40) SDA<br>INA226 (Battery 0x41) SDA | I2C Data | Bidirectional | Shared I2C data bus (`Wire.begin(21, 22)`) |
| **GPIO22** | INA226 (Solar 0x40) SCL<br>INA226 (Battery 0x41) SCL | I2C Clock | Output | Shared I2C clock bus |
| **GPIO5** | DS18B20 DATA | 1-Wire | Bidirectional | Dallas 1-Wire bus with 1.5 kΩ pull-up to 3.3 V |
| **GPIO35** | SCT-013 Signal Conditioning Output | ADC1 CH7 | Input (Analog) | 12-bit ADC reading (`analogReadResolution(12)`). Dedicated input-only pin on ADC1 |
| **3.3V** | INA226 VCC (both), DS18B20 VDD, Bias Divider High | Power | Output | Regulated 3.3 V logic and sensor supply rail |
| **GND** | Battery Negative Post (Common Ground), Sensor GNDs | Power / Reference | Ground | Shared DC system reference point on source side of shunts |

> **Development History Note:** Earlier drafts and schematics referenced GPIO4 for the DS18B20 and GPIO34 for the SCT-013. The final operational firmware standardizes on **GPIO5** for 1-Wire and **GPIO35** for the SCT-013. Both GPIO34 and GPIO35 belong to ADC1 and do not conflict with the Wi-Fi subsystem.

---

## 4. DC Subsystem Wiring (INA226 and Low-Side Shunts)

### 4.1 Topology and Shunt Placement
Both DC measurement channels (Battery and Solar) utilize **low-side current sensing**:
- The shunt is placed in series with the **negative conductor** of the circuit.
- Battery Shunt: Located between the Battery negative post and the Inverter battery negative terminal.
- Solar Shunt: Located between the PV array negative combiner bus and the Inverter PV negative terminal.

### 4.2 INA226 I2C Addressing
The two INA226 modules share the I2C bus on GPIO21 (SDA) and GPIO22 (SCL):
- **Solar PV INA226:** Address **`0x40`** (A0 = GND, A1 = GND).
- **Battery INA226:** Address **`0x41`** (A0 = VS+, A1 = GND, bridged address jumper).

*(Note: Earlier project documentation listed Battery at 0x40 and Solar at 0x41; the final firmware explicitly establishes 0x40 for Solar and 0x41 for Battery).*

### 4.3 Sense Wire Wiring (Kelvin Connections)
Each shunt features two large main busbar terminals for the heavy power conductors and two small Kelvin sense screw terminals:
- Sense leads are run as twisted pairs directly from the Kelvin screws to the INA226 `IN+` and `IN-` header pins.
- Kelvin connections prevent current-dependent IR voltage drops across the high-current lugs from introducing measurement offsets.

### 4.4 Bus Voltage Reference (VBUS)
- The INA226 `VBUS` pin measures the total bus voltage relative to the module's `GND` pin.
- Because low-side sensing places the shunt near 0 V, connecting `VBUS` to the shunt's `IN+` terminal results in near-zero volt readings.
- **Correct Connection:** The `VBUS` terminal on the Solar INA226 must be wired directly to the PV positive rail. The `VBUS` terminal on the Battery INA226 must be wired directly to the Battery positive post.

### 4.5 Shunt Resistance and Calibration Parameters
The external shunts are rated 50 A / 75 mV:
$$R_{shunt} = \frac{0.075\ \text{V}}{50\ \text{A}} = 0.0015\ \Omega\ (1.5\ \text{m}\Omega)$$

In firmware, the Rob Tillaart INA226 library calculates internal calibration registers using:
```cpp
inaBattery.setMaxCurrentShunt(50.0, 0.0015);
inaSolar.setMaxCurrentShunt(50.0, 0.0015);
```

---

## 5. AC Load Current Sensing (SCT-013-050)

### 5.1 Operating Principle
The SCT-013-050 is a non-invasive current transformer with an internal burden resistor. When clamped around an AC conductor, it produces an AC voltage output proportional to current:
- Rated Current: 50 A RMS
- Rated Voltage Output: 1.0 V RMS (at 50 A RMS)
- Transformation Ratio: $50\ \text{A} / 1.0\ \text{V} = 50.0\ \text{A/V}$

### 5.2 Conditioning and Bias Network
The ESP32 analog-to-digital converter accepts unipolar voltages strictly between 0 V and 3.3 V. Because an AC signal alternates symmetrically about zero, feeding raw AC into the ADC would clip the negative half-cycles and damage the microcontroller.

A passive DC bias circuit shifts the AC signal:
```
           +3.3V
             |
            [R1: 10 kΩ]
             |
Midpoint ----+----[C1: 10 µF]----+ GND
 (1.65V)     |                   |
            [R2: 10 kΩ]          |
             |                   |
            GND                  |
             |                   |
       SCT Lead 1                |
                                 |
       SCT Lead 2 --------------+-----> ESP32 GPIO35 (ADC1_CH7)
```

1. **Midpoint Generation:** Equal-value 10 kΩ resistors $R_1$ and $R_2$ divide the 3.3 V rail to establish a virtual ground at:
   $$V_{mid} = 3.3\ \text{V} \times \frac{10\ \text{k}\Omega}{10\ \text{k}\Omega + 10\ \text{k}\Omega} = 1.65\ \text{V}$$
2. **AC Ground Stabilization:** The 10 µF electrolytic capacitor $C_1$ connects between the midpoint and ground, sinking AC currents and providing a low-impedance reference.
3. **Sensor Connection:** One lead of the SCT-013 connects to the 1.65 V virtual ground midpoint; the second lead connects directly to GPIO35.
4. **Dynamic Mean Subtraction in Firmware:** Rather than assuming an exact 1.65 V offset, the firmware computes the arithmetic mean of 3000 samples over each measurement burst and subtracts that mean, eliminating DC drift caused by resistor tolerance or temperature shifts.

### 5.3 Physical Clamp Placement
The split-core clamp is fastened around the **single live (Line) conductor** exiting the inverter's AC distribution terminal. Clamping both Line and Neutral cancel opposing magnetic fluxes, yielding a zero reading.

---

## 6. Temperature Sensing (DS18B20)

### 6.1 Interface and Form Factor
The DS18B20 is housed in a waterproof stainless steel capsule mounted directly to the rear surface of an outdoor solar panel. It communicates over Dallas 1-Wire on **GPIO5**.

### 6.2 Pull-Up Resistor Configuration
- The 1-Wire protocol relies on an open-drain bus architecture: devices pull the line low to transmit bits, while an external pull-up resistor pulls the line back to 3.3 V.
- **Earlier Prototype:** Used a standard pull-up resistor that measured ~3.76 kΩ in-circuit. Over the extended cable run from ground level up to the roof-mounted panel array, line capacitance slowed voltage rise times, leading to severe communication failure rates (~93% returned -127 °C).
- **Final Physical Build:** Uses a **1.5 kΩ pull-up resistor** connected between GPIO5 (DATA) and the 3.3 V rail. The lower resistance provides faster line rise times across parasitic cable capacitance. Subsequent testing after installation of the final 1.5 kΩ pull-up confirmed that the previously observed DS18B20 communication failures and -127 °C readings were resolved.

---

## 7. Common Grounding and Isolation Architecture

Proper grounding is essential in systems combining DC solar generation, high-current battery storage, and switched inverter AC:

1. **DC Grounding Point:**
   - The ESP32 ground pin and both INA226 ground pins are tied to a single common reference point at the **battery's negative post**.
   - All DC ground leads land on the **source side** of the shunts. If any sensor ground were connected downstream on the inverter side of a shunt, the current-dependent voltage drop across that shunt would lift the logic ground, producing erroneous voltage offsets across all sensors.
   - An ohmmeter continuity test on the inverter with power disconnected verified that the Inverter Battery Negative and Inverter PV Input Negative terminals share internal low-impedance bonding (near 0 Ω).
2. **AC Isolation:**
   - The SCT-013 current transformer provides inherent galvanic isolation through magnetic induction.
   - The AC bias conditioning circuit references the ESP32 3.3V and GND rails only, maintaining complete physical isolation from AC mains voltages.
