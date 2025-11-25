#pragma once
#include "config.h"
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
