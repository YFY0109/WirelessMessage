# WirelessMessage Project

## 项目概述

This project implements a wireless communication terminal using ESP32, a 4x4 matrix keypad, an SSD1315 OLED display, and an HC-12 wireless module. It includes a custom nine-grid input method (IME) for Chinese characters and supports encrypted communication.

该项目实现了一个无线通信终端，使用 ESP32、4x4 矩阵键盘、SSD1315 OLED 显示屏和 HC-12 无线模块。它包括一个自定义的九宫格中文输入法（IME），并支持加密通信。

## Key Components / 关键组件

- **ESP32**: Main microcontroller. 主控芯片。
- **HC-12 Module**: Wireless communication module. 无线通信模块。
- **SSD1315 OLED**: Display for UI. 用于用户界面的显示屏。
- **4x4 Keypad**: Input device for user interaction. 用户交互的输入设备。
- **SPIFFS**: Filesystem for persistent storage of settings and message history. 用于持久化存储设置和消息历史的文件系统。

## Developer Workflows / 开发者工作流

### Build and Upload / 构建与上传

This project uses PlatformIO for building and uploading firmware to the ESP32.
该项目使用 PlatformIO 构建和上传固件到 ESP32。

1. Install PlatformIO: [PlatformIO Installation Guide](https://platformio.org/install).
   安装 PlatformIO：[PlatformIO 安装指南](https://platformio.org/install)。
2. Build the project:
   构建项目：
   ```bash
   pio run
   ```
3. Upload the firmware to the ESP32:
   上传固件到 ESP32：
   ```bash
   pio run --target upload
   ```
4. Monitor serial output:
   监控串口输出：
   ```bash
   pio device monitor
   ```

### Testing / 测试

- **Unit Tests / 单元测试**: Located in `test/test_main.cpp`. Run with:
  单元测试位于`test/test_main.cpp`，运行命令：
  ```bash
  pio test
  ```
- **Encryption Tests / 加密测试**: Run the Python script:
  加密测试运行 Python 脚本：
  ```bash
  python test/encryption_test.py
  ```

### Debugging / 调试

- Use `debug.h` to enable or disable debug prints globally.
  使用`debug.h`全局启用或禁用调试输出。
- Serial monitor baud rate: `115200`.
  串口监视器波特率：`115200`。

## Project-Specific Conventions / 项目特定约定

### File Persistence / 文件持久化

- **Message History / 消息历史**: Stored in `/history.txt`.
  存储于`/history.txt`。
- **Settings / 设置**: Stored in `/rcv_settings.txt`.
  存储于`/rcv_settings.txt`。

### Input Method / 输入法

- Pinyin-to-Chinese mapping is loaded from `data/pinyin.json`.
  拼音到汉字的映射从`data/pinyin.json`加载。
- Default input mode: Chinese (MODE_CHS).
  默认输入模式：中文（MODE_CHS）。

### HC-12 Configuration / HC-12 配置

- Default baud rate: `38400`.
  默认波特率：`38400`。
- AT commands are used for configuration (e.g., `AT+RB` to read baud rate).
  使用 AT 指令进行配置（例如，`AT+RB`读取波特率）。

### UI Behavior / 用户界面行为

- OLED refresh interval: `80ms`.
  OLED 刷新间隔：`80ms`。
- Idle timeout: `120 seconds` (enters low-power mode).
  空闲超时：`120秒`（进入低功耗模式）。

## External Dependencies / 外部依赖

- **ArduinoJson**: Used for parsing `pinyin.json`.
  用于解析`pinyin.json`。
- **U8g2**: OLED display library.
  OLED 显示屏库。
- **Keypad**: Matrix keypad library.
  矩阵键盘库。
- **SPIFFS**: Filesystem for ESP32.
  ESP32 的文件系统。

## Examples / 示例

### Adding a New HC-12 Command / 添加新的 HC-12 指令

To add a new AT command for the HC-12 module:
为 HC-12 模块添加新的 AT 指令：

1. Define the command in `HC12_Module.cpp`.
   在`HC12_Module.cpp`中定义指令。
2. Use `sendATCommand("<COMMAND>")` to send the command.
   使用`sendATCommand("<COMMAND>")`发送指令。
3. Handle the response appropriately.
   适当处理响应。

### Debugging Input Method / 调试输入法

To debug pinyin-to-character mapping:
调试拼音到汉字的映射：

1. Ensure `data/pinyin.json` is correctly formatted.
   确保`data/pinyin.json`格式正确。
2. Use `DEBUG_PRINTLN` to log candidate generation in `input_method.cpp`.
   使用`DEBUG_PRINTLN`记录`input_method.cpp`中的候选生成。

---

For further questions, refer to the source files or contact the repository owner.
如有进一步问题，请参考源文件或联系仓库所有者。
