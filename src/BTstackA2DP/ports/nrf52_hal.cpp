// hal_* functions that the btstack embedded run loop needs on the nRF52840 (mbed) port.
// They must be defined once, so they live in a .cpp file. Empty on other platforms.
#if defined(ARDUINO_ARCH_NRF52840) && defined(ARDUINO_ARCH_MBED)

#include <Arduino.h>
#include <mbed.h>

#include "btstack/hal_cpu.h"
#include "btstack/hal_time_ms.h"

extern "C" {

void hal_cpu_disable_irqs(void) { __disable_irq(); }

void hal_cpu_enable_irqs(void) { __enable_irq(); }

// The run loop calls this with interrupts disabled and no pending work. The next
// interrupt (RTOS tick, UART RX) wakes the thread up.
void hal_cpu_enable_irqs_and_sleep(void) {
  __enable_irq();
  __WFI();
}

uint32_t hal_time_ms(void) { return (uint32_t)millis(); }

}  // extern "C"

// mbed sends stdout (printf: BTSTACKA2DP_LOG, asserts, fault reports) to its STDIO UART, which
// on the Nano 33 BLE uses D0/D1, the pins of the HCI module: the text would end up in the HCI
// link. Send it to the USB serial port instead. Weak, so a sketch can still choose another
// stream with REDIRECT_STDOUT_TO(...).
namespace mbed {
__attribute__((weak)) FileHandle* mbed_override_console(int fd) {
  (void)fd;
  return static_cast<FileHandle*>(Serial);
}
__attribute__((weak)) FileHandle* mbed_target_override_console(int fd) {
  (void)fd;
  return static_cast<FileHandle*>(Serial);
}
}  // namespace mbed

#endif  // ARDUINO_ARCH_NRF52840 && ARDUINO_ARCH_MBED
