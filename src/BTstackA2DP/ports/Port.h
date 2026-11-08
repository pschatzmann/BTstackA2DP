// Selects the platform port. Every port provides blue_a2dp::Port with:
//   static bool start(bool (*setup)(void*), void* ctx);  // init btstack, run setup in its context, power on
//   static void stop();                                    // power off
//   static void post(void (*fn)(void*), void* ctx);        // run fn in the btstack context
//   static void lock(); static void unlock();
#pragma once

#if defined(ARDUINO_ARCH_ESP32)
#include "PortESP32.h"
#elif defined(ARDUINO_ARCH_RP2040)
#include "PortRP2040.h"
#elif defined(__linux__) || defined(__APPLE__)
#include "PortPosix.h"
#else
#error "BTstackA2DP: no port for this platform yet (supported: ESP32, RP2040/RP2350, Linux, macOS)."
#endif
