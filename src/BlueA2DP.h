// BlueA2DP - A2DP sink/source for Arduino on top of BlueKitchen btstack.
// Header-only C++ API; works on ESP32, RP2040 (Pico W) and the desktop (Linux/macOS).
#pragma once

#include "BlueA2DP/A2DPTypes.h"
#include "BlueA2DP/BluetoothA2DPSink.h"
#include "BlueA2DP/BluetoothA2DPSource.h"

// Only Frame is exported to the global namespace (like in ESP32-A2DP). All other types
// stay in blue_a2dp:: so that they cannot clash with e.g. arduino-audio-tools.
using blue_a2dp::Frame;
