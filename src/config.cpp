#include "config.h"

// Keyboard pins
byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};

// Key layout (4x4)
char keys[ROWS][COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}};

// HC-12 defaults
int HC12_BAUD_RATE = 38400;
const int HC12_SET_PIN = 4;

// I2C pins for OLED
const int I2C_SDA_PIN = 22;
const int I2C_SCL_PIN = 23;

// Files
const char *HISTORY_FILE = "/history.txt";
const char *SETTINGS_FILE = "/rcv_settings.txt";

// Special map used in main UI
const char *specialMap[10] = {
    " ",
    "~",
    "!@#",
    "#$%",
    "^&*",
    "()_+",
    "-=[]",
    "{};:'",
    "<>,.?",
    "+/\\|"};

// Keymaps (T9 and pinyin mapping)
const char *keymap[10] = {
    "",
    "",
    "abc",
    "def",
    "ghi",
    "jkl",
    "mno",
    "pqrs",
    "tuv",
    "wxyz"};

const char *pinyinKeymap[10] = {
    "",
    "",
    "abc",
    "def",
    "ghi",
    "jkl",
    "mno",
    "pqrs",
    "tuv",
    "wxyz"};

// Frequency file
const String FREQ_FILE = "/frequency.txt";
const int MAX_FREQ_ENTRIES = 500;

// HC-12 settings menu
const char *settingsMenu[] = {
    "Get Version",
    "Get All Params",
    "Get Baud",
    "Set Baud",
    "Get Channel",
    "Set Channel",
    "Get Mode",
    "Set Mode",
    "Get Power",
    "Set Power",
    "Sleep",
    "Factory Reset",
    "Exit"};

const int SETTINGS_MENU_COUNT = sizeof(settingsMenu) / sizeof(settingsMenu[0]);
