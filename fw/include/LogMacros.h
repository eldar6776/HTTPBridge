#ifndef LOG_MACROS_H
#define LOG_MACROS_H

#include <Arduino.h>

// 1. Definicija nivoa logiranja
#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_DEBUG 3

// 2. Ovdje postavljate željeni nivo za cijeli projekt
// Može se mijenjati ručno ovdje ili preko build flagova
#ifndef CURRENT_LOG_LEVEL
#define CURRENT_LOG_LEVEL LOG_LEVEL_DEBUG
#endif

// 3. Makro definicije

// --- ERROR ---
#if CURRENT_LOG_LEVEL >= LOG_LEVEL_ERROR
  // Dodaje prefix [LOG ERROR] i koristi printf formatiranje
  #define LOG_ERROR(...)    do { Serial.print("[LOG ERROR] "); Serial.printf(__VA_ARGS__); } while(0)
  #define LOG_ERROR_LN(...) do { Serial.print("[LOG ERROR] "); Serial.printf(__VA_ARGS__); Serial.println(); } while(0)
#else
  #define LOG_ERROR(...)
  #define LOG_ERROR_LN(...)
#endif

// --- INFO ---
#if CURRENT_LOG_LEVEL >= LOG_LEVEL_INFO
  // Dodaje prefix [LOG INFO] i koristi printf formatiranje
  #define LOG_INFO(...)   do { Serial.print("[LOG INFO] "); Serial.printf(__VA_ARGS__); } while(0)
  #define LOG_INFO_LN(...)   do { Serial.print("[LOG INFO] "); Serial.printf(__VA_ARGS__); Serial.println(); } while(0)
#else
  #define LOG_INFO(...)
  #define LOG_INFO_LN(...)
#endif

// --- DEBUG ---
// Debug makroi nemaju prefiks jer se često koriste za ispis raw podataka (byte po byte)
#if CURRENT_LOG_LEVEL >= LOG_LEVEL_DEBUG
  #define LOG_DEBUG(...)      Serial.print(__VA_ARGS__)
  #define LOG_DEBUG_F(...)    Serial.printf(__VA_ARGS__)
  #define LOG_DEBUG_HEX(x)    Serial.print(x, HEX)
  #define LOG_DEBUG_LN(...)   Serial.println(__VA_ARGS__)
#else
  #define LOG_DEBUG(...)
  #define LOG_DEBUG_F(...)
  #define LOG_DEBUG_HEX(x)
  #define LOG_DEBUG_LN(...)
#endif

#endif // LOG_MACROS_H
