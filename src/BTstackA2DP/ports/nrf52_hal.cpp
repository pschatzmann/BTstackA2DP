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

#endif  // ARDUINO_ARCH_NRF52840 && ARDUINO_ARCH_MBED
