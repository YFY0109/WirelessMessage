# Copilot Instructions for WirelessMessage Project

## Project Overview

This project implements a wireless communication terminal using ESP32, a 4x4 matrix keypad, an SSD1315 OLED display, and an HC-12 wireless module. It includes a custom nine-grid input method (IME) for Chinese characters and supports encrypted communication.

### Key Components

- **ESP32**: Main microcontroller.
- **HC-12 Module**: Wireless communication module.
- **SSD1315 OLED**: Display for UI.
- **4x4 Keypad**: Input device for user interaction.
- **SPIFFS**: Filesystem for persistent storage of settings and message history.

### Code Structure

- `src/main.cpp`: Entry point, orchestrates hardware initialization, UI, and communication logic.
- `src/HC12_Module.cpp`: Encapsulates HC-12 AT command handling and communication.
- `src/input_method/input_method.cpp`: Implements the Chinese IME logic, including pinyin-to-character mapping.
- `src/config.h` and `src/config.cpp`: Centralized configuration for hardware pins, defaults, and constants.
- `test/`: Contains unit tests (`test_main.cpp`) and encryption tests (`encryption_test.py`).

## Developer Workflows

### Build and Upload

This project uses PlatformIO for building and uploading firmware to the ESP32.

1. Install PlatformIO: [PlatformIO Installation Guide](https://platformio.org/install).
2. Build the project:
   ```bash
   pio run
   ```
3. Upload the firmware to the ESP32:
   ```bash
   pio run --target upload
   ```
4. Monitor serial output:
   ```bash
   pio device monitor
   ```

### Testing

- **Unit Tests**: Located in `test/test_main.cpp`. Run with:
  ```bash
  pio test
  ```
- **Encryption Tests**: Run the Python script:
  ```bash
  python test/encryption_test.py
  ```

### Debugging

- Use `debug.h` to enable or disable debug prints globally.
- Serial monitor baud rate: `115200`.

## Project-Specific Conventions

### File Persistence

- **Message History**: Stored in `/history.txt`.
- **Settings**: Stored in `/rcv_settings.txt`.

### Input Method

- Pinyin-to-Chinese mapping is loaded from `data/pinyin.json`.
- Default input mode: Chinese (MODE_CHS).

### HC-12 Configuration

- Default baud rate: `38400`.
- AT commands are used for configuration (e.g., `AT+RB` to read baud rate).

### UI Behavior

- OLED refresh interval: `80ms`.
- Idle timeout: `120 seconds` (enters low-power mode).

## External Dependencies

- **ArduinoJson**: Used for parsing `pinyin.json`.
- **U8g2**: OLED display library.
- **Keypad**: Matrix keypad library.
- **SPIFFS**: Filesystem for ESP32.

## Examples

### Adding a New HC-12 Command

To add a new AT command for the HC-12 module:

1. Define the command in `HC12_Module.cpp`.
2. Use `sendATCommand("<COMMAND>")` to send the command.
3. Handle the response appropriately.

### Debugging Input Method

To debug pinyin-to-character mapping:

1. Ensure `data/pinyin.json` is correctly formatted.
2. Use `DEBUG_PRINTLN` to log candidate generation in `input_method.cpp`.

---

For further questions, refer to the source files or contact the repository owner.
