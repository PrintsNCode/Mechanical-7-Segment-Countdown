# Mechanical 7-Segment Countdown Clock — Setup Guide

This repository contains the **ESP32 firmware and electronics documentation** for two mechanical display configurations. Choose the firmware matching your **single-digit (7-servo)** or **dual-digit (14-servo)** model. Each build uses one ESP32, one PCA9685 and a separate power supply for the servos.

**Printable parts are distributed on the creator's model-download platforms (MakerWorld, MakerOnline and other platforms used by the author), not in this GitHub repository.** See [the download links below](#9-where-to-download-the-mechanical-model).

## 1. Choose your version

| | Single digit | Dual digit |
|---|---|---|
| Numerical range | 0–9 | 00–99 |
| MF90 micro servos | 7 | 14 |
| Servo outputs | CH0–CH6 | CH0–CH13 |
| Firmware | `Countdown_Single_7_Servos.ino` | `Countdown_Dual_14_Servos.ino` |
| Wi-Fi network | `RELOJ_0_9` | `RELOJ_SERVOS` |
| NVS calibration namespace | `reloj1` | `reloj` |

The two firmware versions keep their own calibration data, so the single-digit firmware does not overwrite the dual-digit calibration namespace.

## 2. Components

- **1× ESP32 development board** (classic ESP32 with GPIO21 and GPIO22 exposed).
- **1× PCA9685 16-channel PWM/servo driver**.
- **7× or 14× MF90 micro servos**, depending on the model.
- **1× regulated 5 V power supply**, rated for your actual servo load. The dual-digit original build uses **5 V / 10 A**; a different build may require more or less current depending on servo behaviour and mechanical resistance.
- Jumper wires for I²C logic, servo leads and suitable wiring for the servo power supply.
- Servo horns, screws and other mechanical fasteners as required by the printed parts.
- The corresponding mechanical parts (download separately from the model-sharing platforms).


## 3. Wiring

The **ESP32 controls only the PCA9685** over I²C. The **PCA9685 generates servo signals**. The servos draw their power from the **external 5 V supply**, not from the ESP32.

| ESP32 / power supply | PCA9685 | Purpose |
|---|---|---|
| ESP32 **3V3** | **VCC** | PCA9685 logic power (3.3 V) |
| ESP32 **GND** | **GND** | Common signal ground |
| ESP32 **GPIO21** | **SDA** | I²C data |
| ESP32 **GPIO22** | **SCL** | I²C clock |
| External supply **+5 V** | Screw terminal **V+** | Servo power |
| External supply **− / GND** | Screw terminal **GND** | Servo power return / common ground |

```text
 ESP32 (powered over USB)      PCA9685                     Servos
  3V3 -----------------------> VCC
  GND -----------------------> GND                         Black/brown -> GND
  GPIO21 --------------------> SDA    CH0 ... CH6/CH13 ---> Red --------> V+
  GPIO22 --------------------> SCL                         Yellow/orange -> PWM

 External regulated 5 V supply
  +5V -----------------------> V+ (green screw terminal)
  GND -----------------------> GND (green screw terminal)

 IMPORTANT: ESP32 GND and power supply GND are electrically common.
```

**Do not connect the servo 5 V power rail to the ESP32's 3V3 pin.** Do not power multiple servos from the ESP32 USB/5V pin. On most PCA9685 breakouts, **OE can remain unconnected** because it is pulled low by default.

Servo headers on the common PCA9685 breakout are marked **GND / V+ / PWM**; follow the markings, not the physical colour alone. Double-check polarity before applying power.

## 4. Segment-to-channel mapping

Segment convention:

```text
         A
       -----
   F  |     |  B
       --G--
   E  |     |  C
       -----
         D
```

| Segment | Single digit | Dual left digit | Dual right digit |
|---|---:|---:|---:|
| A | CH4 | CH4 | CH10 |
| B | CH6 | CH6 | CH11 |
| C | CH3 | CH3 | CH9 |
| D | CH2 | CH2 | CH13 |
| E | CH0 | CH0 | CH12 |
| F | CH1 | CH1 | CH8 |
| G | CH5 | CH5 | CH7 |

**The single-digit firmware deliberately follows the original LEFT-digit channel arrangement.** If you wire your single-digit model differently, update `singleMap[7]` near the start of its `.ino` file before uploading. The dual firmware follows the channel layout of the original working 14-servo build.

## 5. Upload firmware using Arduino IDE

1. Install **Arduino IDE 2.x** on Windows, macOS or Linux.
2. In **File → Preferences**, add the Espressif boards manager URL:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Open **Boards Manager**, search for **esp32** and install **esp32 by Espressif Systems**.
4. Open **Library Manager**, search for and install **Adafruit PWM Servo Driver Library**. Accept required dependencies such as Adafruit BusIO when prompted.
5. Open the `.ino` firmware that matches your mechanical display version. Arduino IDE may ask you to create a matching sketch folder; accept.
6. Connect your ESP32 by USB and choose **ESP32 Dev Module** (for the classic board shown in the original build), and select the correct serial port.
7. **Important when preserving an existing calibration:** set **Tools → Erase All Flash Before Sketch Upload → Disabled**. Keep the same partition scheme; a full flash erase removes stored NVS data. For a new build, calibration will be performed after uploading.
8. Click **Upload**. If the board asks you to enter bootloader mode, follow its BOOT/EN button prompts.
9. Open **Serial Monitor** at **115200 baud** to view loaded channel calibration, startup status and network IP.

There are no Wi-Fi credentials to enter in the source code. The ESP32 creates its own access point:

| | Single digit | Dual digit |
|---|---|---|
| Wi-Fi SSID | `RELOJ_0_9` | `RELOJ_SERVOS` |
| Wi-Fi password | `12345678` | `12345678` |
| Web address | `http://192.168.4.1` | `http://192.168.4.1` |

Connect a smartphone or computer to the selected ESP32 Wi-Fi network. Your device may warn that this network has no internet access; remain connected and open the web address in a browser.

## 6. First-time calibration (required for every new build)

1. Start with **one servo connected**, and verify that the corresponding segment can move freely. Keep hands clear and avoid mechanical hard stops.
2. Open **Calibration** in the web interface.
3. Select the channel and move it in **small increments** from a safe middle setting. The displayed “degrees” are software commands, not a measured output angle.
4. Set the **hidden segment position** and click **Save OFF**.
5. Set the **visible segment position** and click **Save ON**.
6. Repeat for all **7** or **14** servos. Every channel needs both ON and OFF positions before number and countdown modes are enabled.
7. Test each ON/OFF transition, especially mirrored or adjacent segments, before running the dance feature or direct-speed mode.
8. Open **Calibration backup → Generate backup text** and save that text outside the ESP32.

You can copy calibration values between channels, but **doing so overwrites the target channel's saved values**. Because servos may be mounted in opposite directions, copied angles can cause binding. Back up first and test every servo separately.

## 7. Using the web controls

- **Numbers:** choose any number in the supported range; **All ON** shows `8` (single) or `88` (dual); **All OFF** hides all segments.
- **Countdown:** enter the starting number, movement speed and seconds per step. Stop when needed. The single-digit timer counts down to `0`; the dual-digit timer counts down to `00`.
- **Dial:** rotate the circular control on mobile or with a mouse to choose a number. Tap to start a countdown.
- **Dance:** plays short randomized patterns using calibrated ON/OFF positions only; ensure clearance before starting.
- **Calibration:** move each servo, save ON/OFF, copy values, generate a backup or restore a previous backup.

**Speed 100:** sends the target PWM commands directly, staggered slightly between groups. This does **not** raise the physical rated speed of the servo. Actual movement time depends on servo type, load and supply voltage. Do not force a mechanism to move faster by increasing supply voltage beyond its specification.

## 8. Calibration and safety notes

- **Dual-digit existing calibration:** the double firmware uses the same ESP32 Preferences namespace `reloj` and keys `off0`…`off13`, `on0`…`on13` as the original build. Normal sketch uploading without a full flash erase preserves these values under the same partition layout.
- **Single-digit calibration:** stored separately as `reloj1` using `off0`…`off6`, `on0`…`on6`.
- To transfer calibration to **another ESP32**, use the web backup/restore feature. The single and dual backups contain **different numbers of pairs** and are not interchangeable.
- If servos jitter or the ESP32 resets under load, test supply voltage at the PCA9685 and check wiring, current capability, mechanical friction and grounding.
- The ESP32 is not physically connected to each servo signal; all servo signals come from the PCA9685.


## 9. Where to download the mechanical model

This GitHub repository contains **firmware and technical documentation only**, with **no 3MF or STL files**. Download the single-digit or dual-digit mechanical designs separately from the author's publication pages:

- [MakerWorld](<https://makerworld.com/es/models/3464388-single-dual-digit-0-9-00-99-esp32#profileId-3953675>)
- [MakerOnline](<https://www.makeronline.com/en/model/Single%20%26%20Dual%20Digit%20(0%E2%80%939%20%2F%2000%E2%80%9399)%20%7C%20ESP32/341311.html?trackModuleType=10>)
- [Other author platform](<https://space.snapmaker.com/en/model/44552-single-dual-digit-09-0099-esp32#profileId-48146>)
