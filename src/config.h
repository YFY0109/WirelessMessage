#include <Arduino.h>

#pragma region Keyboard Pin

// 4x4矩阵键盘行引脚（从上到下：1/2/3/A、4/5/6/B、7/8/9/C、*/0/#/D）
// 按照物理接线顺序：
const byte ROWS = 4;
byte rowPins[ROWS] = {1, 2, 42, 41};

// 4x4矩阵键盘列引脚（从左到右：每行的1/4/7/*、2/5/8/0、3/6/9/#、A/B/C/D）
// 按照物理接线顺序：
const byte COLS = 4;
byte colPins[COLS] = {40, 39, 38, 37};

#pragma endregion

#pragma region HC12 Set

// --- HC-12 模块设置 ---
// HC-12的默认波特率为38400
int HC12_BAUD_RATE = 38400;
#define HC12_SET_PIN 12 // HC-12 SET引脚
#define HC12_UART_NUM 2 // 使用的UART编号（1或2）
#define HC12_RX_PIN 10  // RX引脚，连接到HC-12的TXD
#define HC12_TX_PIN 11  // TX引脚，连接到HC-12的RXD

#pragma endregion

#pragma region OLED Pin

// --- OLED显示屏设置 ---
// 使用硬件I2C。自定义I2C引脚：

#define SSD1315_SCL_PIN 35
#define SSD1315_SDA_PIN 36

#pragma endregion
