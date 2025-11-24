#pragma once
// 调试宏定义，注释掉 ENABLE_DEBUG 可关闭所有调试输出
#define ENABLE_DEBUG
#ifdef ENABLE_DEBUG
#include <Arduino.h>
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINT_ARGS(fmt, ...) Serial.printf(fmt, __VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINT_ARGS(fmt, ...)
#endif
