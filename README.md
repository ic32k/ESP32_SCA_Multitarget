# ESP32-C3 SCA Multitarget v0.5 🛡️⚡

A specialized firmware for the **ESP32-C3 Mini (0.42" OLED)** designed for practicing **Side-Channel Analysis (SCA)** and **Fault Injection (FI)**. This project transforms the ESP32-C3 into a versatile target for Electromagnetic Analysis (EMA) and Glitch attacks.

Developed by **[@ic32k](https://github.com/ic32k)**.

---

## Overview

This project is tailored for security researchers and enthusiasts to practice:
*   **Simple/Differential Power Analysis (SPA/DPA)** & **Electromagnetic Analysis (EMA)**: Using near-field probes (EMF), LNAs, and capture devices like **ChipWhisperer** or **PicoGlitcher**.
*   **Fault Injection (FI) / Glitching**: Testing susceptibility to Voltage or EM glitches using tools like **ChipShouter**, **PicoEMP**, or **FaultyCat**.

The firmware provides a real-time OLED interface and a full UART CLI for selecting targets and monitoring execution.

---

## Hardware Setup

### Device: ESP32-C3 Mini 0.42" OLED
The ESP32-C3 is a RISC-V based SoC, making it an interesting target for architectural side-channel research.

### Pinout Mapping
Refer to the following pinout for connections:
![ESP32-C3 Mini Pinout](https://ae-pic-a1.aliexpress-media.com/kf/Saffc363f38ab48febae45e1dbce87eaan.jpg_640x640q75.jpg_.avif)

| Function | Pin | Description |
| :--- | :--- | :--- |
| **UART RX** | GPIO 20 | Connect to your UART adapter (e.g., Bus Pirate / FTDI) |
| **UART TX** | GPIO 21 | Connect to your UART adapter (e.g., Bus Pirate / FTDI) |
| **Trigger Out** | GPIO 7 | **CRITICAL**: Connect to your Oscilloscope/ChipWhisperer trigger input. |
| **Button** | GPIO 9 | Used for manual menu navigation and execution. |
| **LED** | GPIO 8 | Internal Blue LED (Status indicator). |
| **OLED SDA** | GPIO 5 | Hardwired to internal display. |
| **OLED SCL** | GPIO 6 | Hardwired to internal display. |

---

## 🛠️ Connection Schematics

### 1. Leakage Capture (EMA/SPA)
To capture electromagnetic leaks during AES operations:
1.  Place an **EM Probe** over the ESP32-C3 SoC (Decap is recommended but not required).
2.  Connect the probe to an **LNA** (Low Noise Amplifier).
3.  Connect the LNA output to your **Oscilloscope** or **ChipWhisperer**.
4.  Connect **GPIO 7 (Trigger)** to the external trigger of your capture device.

### 2. Fault Injection (Glitch)
To perform Voltage or EM Glitching:
1.  **Voltage Glitch**: Intercept the VCC line (Vcore) and use an external MOSFET (e.g., from a PicoGlitcher) to pull it to GND briefly.
2.  **EM Glitch**: Place the **PicoEMP / ChipShouter** coil over the SoC.
3.  Use **GPIO 7 (Trigger)** to time the glitch precisely when the `glitchTest()` loop starts.

---

## Exercises & Menu Options

The firmware includes 10 modes, selectable via the onboard button or UART commands (`n` for Next, `s` for Select, or `0-9` for direct jump).

### Glitch Testing
*   **[0] G(a) - Glitch Auto**: Runs a continuous loop of a critical counter. The `TRIGGER_PIN` goes HIGH during the loop. If the counter is corrupted, the OLED displays "GLITCH!".
*   **[1] G(m) - Glitch Manual**: Same as above, but requires a button press or UART command for each iteration.

### AES Side-Channel (EMA/SPA)
*   **[2] SH(a) - AES HW Auto**: Uses the ESP32-C3 **Hardware Accelerator** for AES-128. Ideal for studying hardware implementation leaks.
*   **[3] SH(m) - AES HW Manual**: Manual trigger for Hardware AES.
*   **[4] SS(a) - AES SW Auto**: Uses a **Software Tiny-AES** implementation. Software implementations are typically much "louder" and easier to analyze for SPA/DPA.
*   **[5] SS(m) - AES SW Manual**: Manual trigger for Software AES.

### Password Timing Attacks
*   **[6] PV(m) - Password Vuln Manual**: A vulnerable password check using `strcmp`-style logic (terminates on first wrong byte). Perfect for **Timing Attacks**.
*   **[7] PV(j) - Password Vuln Jitter**: Same as above, but with added random **Jitter (Noise)** to frustrate simple timing analysis.
*   **[8] PS(m) - Password Safe Manual**: A **Constant-Time** password check. Even if bytes are wrong, the execution time remains the same.
*   **[9] PS(j) - Password Safe Jitter**: The most secure mode, combining constant-time logic with random jitter.

---

## UART Control Interface

The menu is fully controllable via the Serial Monitor (115200 baud):
*   **`n` / `N`**: Move to the next menu option.
*   **`s` / `S`**: Select/Start the current exercise.
*   **`0` - `9`**: Jump directly to a specific exercise.
*   **Enter Passwords**: When prompted, type the password directly into the terminal.
*   **Exit**: Long press the physical button (>1.5s) or send `s` in certain modes to return to the main menu.

---

## License & Credits

*   **Author**: ic32k
*   **Framework**: Arduino / ESP-IDF
*   **Libraries**: U8g2 (OLED), mbedtls (Hardware AES).

*Disclaimer: This project is for educational purposes only. Use it responsibly on hardware you own.*
