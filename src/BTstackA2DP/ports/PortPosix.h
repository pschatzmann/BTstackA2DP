// Desktop port (Linux / macOS): btstack posix run loop in its own thread.
// HCI transport defaults to a USB Bluetooth dongle via libusb (define
// BLUEA2DP_USE_LIBUSB when building); a different transport can be installed with
// PortPosix::set_transport().
#pragma once

#if defined(__linux__) || defined(__APPLE__)

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btstack_config.h"
#include <btstack/btstack_memory.h>
#include <btstack/btstack_run_loop.h>
#include <btstack/btstack_run_loop_posix.h>
#include <btstack/btstack_tlv.h>
#include <btstack/btstack_tlv_posix.h>
#include <btstack/classic/btstack_link_key_db_memory.h>
#include <btstack/classic/btstack_link_key_db_tlv.h>
#include <btstack/hci.h>
#include <btstack/hci_transport.h>
#ifdef BLUEA2DP_DEBUG
#include <btstack/hci_dump.h>
#include <btstack/hci_dump_posix_stdout.h>
#endif
#ifdef BLUEA2DP_USE_LIBUSB
#include <btstack/hci_transport_usb.h>
#endif

namespace blue_a2dp {

class PortPosix {
 public:
  typedef bool (*SetupFn)(void* ctx);

  static const char* name() { return "posix"; }

  /// Use another HCI transport than libusb (call before start)
  static void set_transport(const hci_transport_t* transport,
                            const void* config = nullptr) {
    s().transport = transport;
    s().transport_config = config;
  }

#ifdef BLUEA2DP_USE_LIBUSB
  /// Select the USB Bluetooth controller by bus and port path (see `lsusb -t`), e.g.
  /// bus 1, port 2 -> set_usb_path(1, {2}). Without it the first controller is used,
  /// which may be your system's built-in Bluetooth. Call before start().
  static void set_usb_path(uint8_t bus, const uint8_t* ports, int len) {
    hci_transport_usb_set_bus_and_path(bus, len, (uint8_t*)ports);
  }
  /// Same, from a string like "1:2" or "1:3.1" (bus:port.port...)
  static bool set_usb_path(const char* spec) {
    int bus = 0, n = 0;
    uint8_t ports[7];
    const char* p = strchr(spec, ':');
    if (!p) return false;
    bus = atoi(spec);
    for (p++; *p && n < 7; n++) {
      ports[n] = (uint8_t)atoi(p);
      while (*p && *p != '.') p++;
      if (*p == '.') p++;
    }
    if (n == 0) return false;
    set_usb_path((uint8_t)bus, ports, n);
    return true;
  }
#endif

  /// Path of the file used to store pairing information. nullptr: keep in RAM only.
  static void set_bonding_file(const char* path) {
    s().tlv_path = path;
    s().tlv_path_set = true;
  }

  /// Initializes btstack in a new thread and runs 'setup' in that context before
  /// the controller is powered on. Blocks until the setup is done.
  static bool start(SetupFn setup, void* ctx) {
    State& st = s();
    if (st.running) {  // restart after stop()
      post(&power_on, nullptr);
      return true;
    }
    st.setup = setup;
    st.setup_ctx = ctx;
    st.ready = false;
    st.ok = false;
    if (pthread_create(&st.thread, nullptr, &thread_main, nullptr) != 0)
      return false;
    st.running = true;
    pthread_mutex_lock(&st.mutex);
    while (!st.ready) pthread_cond_wait(&st.cond, &st.mutex);
    pthread_mutex_unlock(&st.mutex);
    if (!st.ok) {
      pthread_join(st.thread, nullptr);
      st.running = false;
    }
    return st.ok;
  }

  /// Power down the controller (the thread stays alive, start() powers it up again)
  static void stop() {
    if (s().running) post(&power_off, nullptr);
  }

  /// Run fn(ctx) in the btstack thread (asynchronously)
  static void post(void (*fn)(void*), void* ctx) {
    Call* c = (Call*)malloc(sizeof(Call));
    if (!c) return;
    c->fn = fn;
    c->ctx = ctx;
    c->reg.callback = &run_call;
    c->reg.context = c;
    btstack_run_loop_execute_on_main_thread(&c->reg);
  }

  static void lock() {}
  static void unlock() {}

 private:
  struct Call {
    btstack_context_callback_registration_t reg;
    void (*fn)(void*);
    void* ctx;
  };

  struct State {
    pthread_t thread;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    bool running = false, ready = false, ok = false;
    SetupFn setup = nullptr;
    void* setup_ctx = nullptr;
    const hci_transport_t* transport = nullptr;
    const void* transport_config = nullptr;
    const char* tlv_path = nullptr;
    bool tlv_path_set = false;
    char tlv_path_buffer[512];
    btstack_tlv_posix_t tlv_context;
  };

  static State& s() {
    static State state;
    return state;
  }

  static void run_call(void* p) {
    Call* c = (Call*)p;
    void (*fn)(void*) = c->fn;
    void* ctx = c->ctx;
    free(c);
    fn(ctx);
  }

  static void power_on(void*) { hci_power_control(HCI_POWER_ON); }
  static void power_off(void*) { hci_power_control(HCI_POWER_OFF); }

  static void signal_ready(bool ok) {
    State& st = s();
    pthread_mutex_lock(&st.mutex);
    st.ok = ok;
    st.ready = true;
    pthread_cond_signal(&st.cond);
    pthread_mutex_unlock(&st.mutex);
  }

  static void* thread_main(void*) {
    State& st = s();
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_posix_get_instance());

    const hci_transport_t* transport = st.transport;
#ifdef BLUEA2DP_USE_LIBUSB
    if (!transport) transport = hci_transport_usb_instance();
#endif
    if (!transport) {
      fprintf(stderr,
              "BlueA2DP: no HCI transport. Build with -DBLUEA2DP_USE_LIBUSB or "
              "call PortPosix::set_transport()\n");
      signal_ready(false);
      return nullptr;
    }
#ifdef BLUEA2DP_DEBUG
    hci_dump_init(hci_dump_posix_stdout_get_instance());  // HCI packets and btstack log on stdout
#endif
    hci_init(transport, st.transport_config);

    // pairing information
    const char* path = st.tlv_path;
    if (!st.tlv_path_set) {
      const char* home = getenv("HOME");
      if (home) {
        snprintf(st.tlv_path_buffer, sizeof(st.tlv_path_buffer),
                 "%s/.blue_a2dp_bonding.tlv", home);
        path = st.tlv_path_buffer;
      }
    }
    if (path) {
      const btstack_tlv_t* tlv = btstack_tlv_posix_init_instance(&st.tlv_context, path);
      btstack_tlv_set_instance(tlv, &st.tlv_context);
      hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(tlv, &st.tlv_context));
    } else {
      hci_set_link_key_db(btstack_link_key_db_memory_instance());
    }

    bool ok = st.setup ? st.setup(st.setup_ctx) : true;
    signal_ready(ok);
    if (!ok) return nullptr;

    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    return nullptr;
  }
};

typedef PortPosix Port;

}  // namespace blue_a2dp

#endif  // __linux__ || __APPLE__
