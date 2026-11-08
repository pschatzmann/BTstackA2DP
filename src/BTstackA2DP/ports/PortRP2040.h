// RP2040 / RP2350 port (Raspberry Pi Pico W, Pico 2 W). The arduino-pico core ships
// btstack and the CYW43 driver and runs btstack in the cyw43 async context. Select
//   Tools > IP/Bluetooth Stack > "IPv4 + Bluetooth" (or similar)
// in the Arduino IDE. This port only adds the glue.
#pragma once

#if defined(ARDUINO_ARCH_RP2040)

#if !defined(ENABLE_CLASSIC)
#error "BTstackA2DP: enable Bluetooth in the Arduino IDE (Tools > IP/Bluetooth Stack) and select a Pico W / Pico 2 W board."
#endif

#include <Arduino.h>
#include <BluetoothLock.h>

#include <btstack/btstack_run_loop.h>
#include <btstack/hci.h>

namespace blue_a2dp {

class PortRP2040 {
 public:
  typedef bool (*SetupFn)(void* ctx);

  static const char* name() { return "rp2040-cyw43"; }

  /// Runs 'setup' (profile initialization) and powers on the controller.
  static bool start(SetupFn setup, void* ctx) {
    BluetoothLock l;
    if (!s().initialized) {
      if (setup && !setup(ctx)) return false;
      s().initialized = true;
    }
    hci_power_control(HCI_POWER_ON);
    return true;
  }

  static void stop() {
    BluetoothLock l;
    hci_power_control(HCI_POWER_OFF);
  }

  /// Run fn(ctx) with the btstack lock held. The lock is recursive, so this is also
  /// fine when called from a btstack callback.
  static void post(void (*fn)(void*), void* ctx) {
    BluetoothLock l;
    fn(ctx);
  }

  static void lock() { async_context_acquire_lock_blocking(cyw43_arch_async_context()); }
  static void unlock() { async_context_release_lock(cyw43_arch_async_context()); }

 private:
  struct State {
    bool initialized = false;
  };
  static State& s() {
    static State state;
    return state;
  }
};

typedef PortRP2040 Port;

}  // namespace blue_a2dp

#endif  // ARDUINO_ARCH_RP2040
