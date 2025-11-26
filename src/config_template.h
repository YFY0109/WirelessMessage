// Centralized configuration header
#ifndef WM_CONFIG_H
#define WM_CONFIG_H

#include <Arduino.h>

// Enable debug by default; toggle here to disable global debug prints
#define ENABLE_DEBUG

// --- Keyboard / Matrix ---
constexpr byte ROWS = 4;
constexpr byte COLS = 4;
extern byte rowPins[ROWS];
extern byte colPins[COLS];
extern char keys[ROWS][COLS];

// --- HC-12 ---
// Default baud (may be changed at runtime)
extern int HC12_BAUD_RATE;
extern const int HC12_SET_PIN;

// HC-12 instance is declared in main; headers may extern it if needed

// --- I2C / OLED ---
extern const int I2C_SDA_PIN;
extern const int I2C_SCL_PIN;

// --- Display / Timing ---
constexpr unsigned long DISPLAY_INTERVAL = 80;          // ms
constexpr unsigned long SLEEP_DISPLAY_INTERVAL = 1000;  // ms
constexpr unsigned long IDLE_TIMEOUT_MS = 120000;       // ms
constexpr unsigned long INCOMING_MSG_DISPLAY_MS = 3000; // ms

// --- Filesystem ---
extern const char *HISTORY_FILE;
extern const char *SETTINGS_FILE;

// --- UI / Chat ---
// Defaults; runtime variables remain in main.cpp
constexpr size_t DEFAULT_MAX_MESSAGE_HISTORY = 50;
constexpr int DEFAULT_CHAT_PAGE_SIZE = 3;

// --- Chat navigation / UI timing ---
constexpr unsigned long CHAT_NAV_INITIAL_DELAY = 500;   // ms 首次长按延迟
constexpr unsigned long CHAT_NAV_REPEAT = 120;          // ms 重复间隔
constexpr unsigned long CHAT_NAV_JUMP_THRESHOLD = 1500; // ms 长按阈值，跳到最早/最新
constexpr unsigned long CHAT_JUMP_MSG_MS = 900;         // 提示显示时长
constexpr unsigned long RECV_SHORTCUT_WINDOW = 1200;    // ms 快捷按键窗口

// --- Special maps / keymaps ---
extern const char *specialMap[10];
extern const char *keymap[10];
extern const char *pinyinKeymap[10];

// --- Symbols / Timing ---
constexpr unsigned long SYMBOL_TIMEOUT = 800;     // ms
constexpr unsigned long KEYMAP_DISPLAY_MS = 3000; // ms

// --- Frequency / persistence ---
extern const String FREQ_FILE;
extern const int MAX_FREQ_ENTRIES;

// --- HC-12 Settings UI ---
extern const char *settingsMenu[];
extern const int SETTINGS_MENU_COUNT;
constexpr unsigned long SETTINGS_MSG_MS = 1500;

// Keep additional configuration here as needed

#endif // WM_CONFIG_H
