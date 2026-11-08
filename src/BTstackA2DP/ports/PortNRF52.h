// nRF52840 port for mbed based Arduino cores (e.g. Arduino Nano 33 BLE).
//
// The nRF52840 has no Classic Bluetooth radio, so A2DP needs an external HCI controller
// on a UART (H4 protocol), for example a CSR8811 module. btstack runs in its own mbed
// thread and uses the btstack embedded run loop (see nrf52_hal.cpp for the hal_* glue).
//
// Wiring: module TX -> nRF52 RX pin, module RX -> nRF52 TX pin, optional RTS/CTS.
// The UART must run at the baud rate of the module (default 921600, which is needed for
// A2DP audio; 115200 is too slow for SBC). Call set_uart() before start() if the pins or
// the baud rate differ from the defaults.
//
// Limitations: pairing information is kept in RAM only (no flash storage yet).
#pragma once

#if defined(ARDUINO_ARCH_NRF52840) && defined(ARDUINO_ARCH_MBED)

#include <Arduino.h>
#include <mbed.h>
#include <stdlib.h>

#include <btstack/btstack_memory.h>
#include <btstack/btstack_run_loop.h>
#include <btstack/btstack_run_loop_embedded.h>
#include <btstack/btstack_uart.h>
#include <btstack/classic/btstack_link_key_db_memory.h>
#include <btstack/hci.h>
#include <btstack/hci_transport.h>
#include <btstack/hci_transport_h4.h>

namespace blue_a2dp {

class PortNRF52 {
 public:
  typedef bool (*SetupFn)(void* ctx);

  static const char* name() { return "nrf52840-uart-hci"; }

  /// UART of the external Bluetooth module (Arduino pin numbers, -1 = not used).
  /// Call before start(). Serial1 is the default on the Nano 33 BLE.
  static void set_uart(int tx, int rx, int rts = -1, int cts = -1,
                       uint32_t baudrate = 921600) {
    State& st = s();
    st.tx = tx;
    st.rx = rx;
    st.rts = rts;
    st.cts = cts;
    st.baudrate = baudrate;
  }

  /// Starts the btstack thread, runs 'setup' in it and powers on the controller.
  /// Blocks until the setup is done.
  static bool start(SetupFn setup, void* ctx) {
    State& st = s();
    if (st.running) {  // restart after stop()
      post(&power_on, nullptr);
      return true;
    }
    if (st.thread) {  // previous start failed: the thread has finished
      st.thread->join();
      delete st.thread;
      st.thread = nullptr;
    }
    if (!st.ready) st.ready = new rtos::Semaphore(0, 1);
    if (!st.mutex) st.mutex = new rtos::Mutex();
    st.setup = setup;
    st.setup_ctx = ctx;
    st.ok = false;
    st.thread = new rtos::Thread(osPriorityNormal, THREAD_STACK_SIZE);
    if (st.thread->start(mbed::callback(&thread_main)) != osOK) {
      delete st.thread;
      st.thread = nullptr;
      return false;
    }
    st.ready->acquire();
    st.running = st.ok;
    return st.ok;
  }

  /// Powers down the controller (the thread stays alive, start() powers it up again)
  static void stop() {
    if (s().running) post(&power_off, nullptr);
  }

  /// Runs fn(ctx) in the btstack thread (asynchronously). Safe from any thread.
  static void post(void (*fn)(void*), void* ctx) {
    State& st = s();
    st.mutex->lock();
    if (st.queue_count < QUEUE_SIZE) {
      st.queue[(st.queue_head + st.queue_count) % QUEUE_SIZE] = {fn, ctx};
      st.queue_count++;
    }
    st.mutex->unlock();
  }

  /// Recursive lock (mbed mutex)
  static void lock() { s().mutex->lock(); }
  static void unlock() { s().mutex->unlock(); }

 private:
  static const uint32_t THREAD_STACK_SIZE = 16 * 1024;
  static const uint8_t QUEUE_SIZE = 16;

  struct Call {
    void (*fn)(void*);
    void* ctx;
  };

  struct State {
    // configuration
    int tx = PIN_SERIAL_TX, rx = PIN_SERIAL_RX, rts = -1, cts = -1;
    uint32_t baudrate = 921600;
    // thread and synchronization
    rtos::Thread* thread = nullptr;
    rtos::Semaphore* ready = nullptr;
    rtos::Mutex* mutex = nullptr;
    bool running = false, ok = false;
    SetupFn setup = nullptr;
    void* setup_ctx = nullptr;
    // queue of calls from post(), drained by the btstack thread
    Call queue[QUEUE_SIZE];
    uint8_t queue_head = 0, queue_count = 0;
  };

  static State& s() {
    static State state;
    return state;
  }

  static void power_on(void*) { hci_power_control(HCI_POWER_ON); }
  static void power_off(void*) { hci_power_control(HCI_POWER_OFF); }

  // ---- btstack thread ----------------------------------------------------------------

  static void thread_main() {
    State& st = s();
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_embedded_get_instance());

    static hci_transport_config_uart_t transport_config;
    transport_config.type = HCI_TRANSPORT_CONFIG_UART;
    transport_config.baudrate_init = st.baudrate;
    transport_config.baudrate_main = 0;  // same speed
    transport_config.flowcontrol = st.rts >= 0 && st.cts >= 0;
    transport_config.device_name = nullptr;
    transport_config.parity = BTSTACK_UART_PARITY_OFF;
    hci_init(hci_transport_h4_instance_for_uart(uart_driver()), &transport_config);
    hci_set_link_key_db(btstack_link_key_db_memory_instance());

    st.ok = st.setup ? st.setup(st.setup_ctx) : true;
    st.ready->release();
    if (!st.ok) return;

    hci_power_control(HCI_POWER_ON);
    for (;;) {
      drain_queue();
      uart_poll();
      btstack_run_loop_embedded_execute_once();  // sleeps until the next interrupt
    }
  }

  static void drain_queue() {
    State& st = s();
    Call local[QUEUE_SIZE];
    uint8_t count = 0;
    st.mutex->lock();
    while (st.queue_count > 0) {
      local[count++] = st.queue[st.queue_head];
      st.queue_head = (st.queue_head + 1) % QUEUE_SIZE;
      st.queue_count--;
    }
    st.mutex->unlock();
    for (uint8_t i = 0; i < count; i++) local[i].fn(local[i].ctx);
  }

  // ---- UART (btstack_uart_t) ------------------------------------------------------------
  // The UART is interrupt driven (Arduino UART class, 256 byte RX ring buffer). Received
  // bytes are copied into the H4 buffer by uart_poll(), which runs in the btstack thread.

  struct UartState {
    ::UART* uart = nullptr;
    uint32_t baudrate = 921600;
    void (*block_received)(void) = nullptr;
    void (*block_sent)(void) = nullptr;
    uint8_t* rx_buffer = nullptr;
    uint16_t rx_len = 0, rx_pos = 0;
    bool rx_pending = false;
    bool tx_done = false;
  };

  static UartState& u() {
    static UartState uart_state;
    return uart_state;
  }

  static int uart_init(const btstack_uart_config_t* config) {
    u().baudrate = config->baudrate;
    return 0;
  }

  static int uart_open(void) {
    UartState& us = u();
    State& st = s();
    if (!us.uart) us.uart = new ::UART(st.tx, st.rx, st.rts, st.cts);
    us.uart->begin(us.baudrate);
    return 0;
  }

  static int uart_close(void) {
    if (u().uart) u().uart->end();
    return 0;
  }

  static void uart_set_block_received(void (*handler)(void)) { u().block_received = handler; }
  static void uart_set_block_sent(void (*handler)(void)) { u().block_sent = handler; }

  static int uart_set_baudrate(uint32_t baudrate) {
    u().baudrate = baudrate;
    if (u().uart) u().uart->begin(baudrate);
    return 0;
  }

  static int uart_set_parity(int parity) { return parity == BTSTACK_UART_PARITY_OFF ? 0 : -1; }
  static int uart_set_flowcontrol(int flowcontrol) { return flowcontrol ? -1 : 0; }

  static void uart_receive_block(uint8_t* buffer, uint16_t len) {
    UartState& us = u();
    us.rx_buffer = buffer;
    us.rx_len = len;
    us.rx_pos = 0;
    us.rx_pending = true;
  }

  static void uart_send_block(const uint8_t* buffer, uint16_t length) {
    UartState& us = u();
    if (us.uart) us.uart->write(buffer, length);  // blocks until sent
    us.tx_done = true;  // block_sent is called from uart_poll()
  }

  static void uart_poll() {
    UartState& us = u();
    if (us.rx_pending && us.uart) {
      while (us.rx_pos < us.rx_len && us.uart->available() > 0) {
        us.rx_buffer[us.rx_pos++] = (uint8_t)us.uart->read();
      }
      if (us.rx_pos == us.rx_len) {
        us.rx_pending = false;
        if (us.block_received) us.block_received();
      }
    }
    if (us.tx_done) {
      us.tx_done = false;
      if (us.block_sent) us.block_sent();
    }
  }

  static const btstack_uart_t* uart_driver() {
    static const btstack_uart_t driver = {
        &uart_init,
        &uart_open,
        &uart_close,
        &uart_set_block_received,
        &uart_set_block_sent,
        &uart_set_baudrate,
        &uart_set_parity,
        &uart_set_flowcontrol,
        &uart_receive_block,
        &uart_send_block,
        nullptr,  // get_supported_sleep_modes: no eHCILL / H5
        nullptr,  // set_sleep
        nullptr,  // set_wakeup_handler
        nullptr,  // set_frame_received
        nullptr,  // set_frame_sent
        nullptr,  // receive_frame
        nullptr,  // send_frame
    };
    return &driver;
  }
};

typedef PortNRF52 Port;

}  // namespace blue_a2dp

#endif  // ARDUINO_ARCH_NRF52840 && ARDUINO_ARCH_MBED
