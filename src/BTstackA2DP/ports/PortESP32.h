// ESP32 port: uses the internal Bluetooth controller through the VHCI interface
// (no Bluedroid) and runs btstack in its own FreeRTOS task.
// Classic Bluetooth is only available on the original ESP32.
#pragma once

#if defined(ARDUINO_ARCH_ESP32)

#include "sdkconfig.h"

#if !(defined(CONFIG_BT_CLASSIC_ENABLED) || defined(CONFIG_CLASSIC_BT_ENABLED))
#error "BlueA2DP: A2DP needs Classic Bluetooth, which this ESP32 variant (or sdkconfig) does not provide. Use an original ESP32."
#endif

// Tell the Arduino core that Classic BT is used, so it does not release the
// Bluetooth memory at boot (Arduino-ESP32 3.3+).
#if __has_include("esp32-hal-alloc-bt-classic-mem.h")
#include "esp32-hal-alloc-bt-classic-mem.h"
#define BLUEA2DP_HAS_BT_MODE_API 1
#endif

#include <stdio.h>
#include <string.h>

#include "esp32-hal-bt.h"
#include "esp_bt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "btstack_config.h"
#include <btstack/btstack_event.h>
#include <btstack/btstack_memory.h>
#include <btstack/btstack_ring_buffer.h>
#include <btstack/btstack_run_loop.h>
#include <btstack/btstack_run_loop_freertos.h>
#include <btstack/btstack_tlv.h>
#include <btstack/classic/btstack_link_key_db.h>
#include <btstack/classic/btstack_link_key_db_tlv.h>
#include <btstack/hci.h>
#include <btstack/hci_transport.h>

#ifndef BLUEA2DP_TASK_STACK_SIZE
#define BLUEA2DP_TASK_STACK_SIZE 8192
#endif
#ifndef BLUEA2DP_TASK_PRIORITY
#define BLUEA2DP_TASK_PRIORITY 5
#endif
#ifndef BLUEA2DP_TASK_CORE
#define BLUEA2DP_TASK_CORE 0
#endif

// btstack's FreeRTOS run loop needs a millisecond clock (weak: can be overridden)
extern "C" __attribute__((weak)) uint32_t hal_time_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

namespace blue_a2dp {

class PortESP32 {
 public:
  typedef bool (*SetupFn)(void* ctx);

  static const char* name() { return "esp32-vhci"; }

  /// First call: creates the btstack task, initializes btstack, runs 'setup' in that
  /// task and powers on the controller. Blocks until done. Later calls re-enable
  /// the controller after stop().
  static bool start(SetupFn setup, void* ctx) {
    State& st = s();
    if (st.task) {
      post(&power_on, nullptr);
      return true;
    }
    st.setup = setup;
    st.setup_ctx = ctx;
    st.ok = false;
    st.ready = xSemaphoreCreateBinary();
    if (!st.ready) return false;
    BaseType_t rc = xTaskCreatePinnedToCore(&task_main, "btstack", BLUEA2DP_TASK_STACK_SIZE,
                                            nullptr, BLUEA2DP_TASK_PRIORITY, &st.task,
                                            BLUEA2DP_TASK_CORE);
    if (rc != pdPASS) {
      st.task = nullptr;
      return false;
    }
    xSemaphoreTake(st.ready, portMAX_DELAY);
    if (!st.ok) {
      // the task has ended itself
      st.task = nullptr;
    }
    return st.ok;
  }

  /// Power down the controller (the task stays alive, start() powers it up again)
  static void stop() {
    if (s().task) post(&power_off, nullptr);
  }

  /// Run fn(ctx) in the btstack task (asynchronously)
  static void post(void (*fn)(void*), void* ctx) {
    btstack_run_loop_freertos_execute_code_on_main_thread(fn, ctx);
  }

  static void lock() {}
  static void unlock() {}

 private:
  struct State {
    TaskHandle_t task = nullptr;
    SemaphoreHandle_t ready = nullptr;
    bool ok = false;
    SetupFn setup = nullptr;
    void* setup_ctx = nullptr;
    // VHCI transport
    void (*packet_handler)(uint8_t, uint8_t*, uint16_t) = nullptr;
    SemaphoreHandle_t ring_mutex = nullptr;
    btstack_ring_buffer_t ring;
    uint8_t ring_storage[HCI_HOST_ACL_PACKET_NUM * (2 + 1 + HCI_ACL_HEADER_SIZE + HCI_HOST_ACL_PACKET_LEN) +
                         HCI_HOST_SCO_PACKET_NUM * (2 + 1 + HCI_SCO_HEADER_SIZE + HCI_HOST_SCO_PACKET_LEN) +
                         4 * (2 + 1 + HCI_EVENT_BUFFER_SIZE)];
    uint8_t rx_buffer[HCI_INCOMING_PRE_BUFFER_SIZE + HCI_INCOMING_PACKET_BUFFER_SIZE];
    btstack_context_callback_registration_t tx_ready_reg;
    btstack_context_callback_registration_t rx_ready_reg;
    hci_transport_t transport;
    bool transport_ready = false;
    // NVS
    nvs_handle_t nvs = 0;
    bool nvs_active = false;
    btstack_tlv_t tlv;
  };

  static State& s() {
    static State state;
    return state;
  }

  static void power_on(void*) { hci_power_control(HCI_POWER_ON); }
  static void power_off(void*) { hci_power_control(HCI_POWER_OFF); }

  static void task_main(void*) {
    State& st = s();
    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_freertos_get_instance());
    hci_init(transport(), nullptr);
    init_tlv();
    bool ok = st.setup ? st.setup(st.setup_ctx) : true;
    st.ok = ok;
    xSemaphoreGive(st.ready);
    if (!ok) {
      vTaskDelete(nullptr);
      return;
    }
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    vTaskDelete(nullptr);
  }

  // ---- persistent pairing information (NVS) --------------------------------------
  static void key_for_tag(uint32_t tag, char* key) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) key[i] = hex[(tag >> (4 * (7 - i))) & 0x0f];
    key[8] = 0;
  }
  static int tlv_get_tag(void*, uint32_t tag, uint8_t* buffer, uint32_t buffer_size) {
    State& st = s();
    if (!st.nvs_active) return 0;
    char key[9];
    key_for_tag(tag, key);
    size_t size = 0;
    if (nvs_get_blob(st.nvs, key, nullptr, &size) != ESP_OK) return 0;
    if (size > buffer_size) return 0;
    if (nvs_get_blob(st.nvs, key, buffer, &size) != ESP_OK) return 0;
    return (int)size;
  }
  static int tlv_store_tag(void*, uint32_t tag, const uint8_t* data, uint32_t size) {
    State& st = s();
    if (!st.nvs_active) return 0;
    char key[9];
    key_for_tag(tag, key);
    if (nvs_set_blob(st.nvs, key, data, size) != ESP_OK) return 1;
    return nvs_commit(st.nvs) == ESP_OK ? 0 : 1;
  }
  static void tlv_delete_tag(void*, uint32_t tag) {
    State& st = s();
    if (!st.nvs_active) return;
    char key[9];
    key_for_tag(tag, key);
    nvs_erase_key(st.nvs, key);
    nvs_commit(st.nvs);
  }
  static void init_tlv() {
    State& st = s();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      err = nvs_flash_init();
    }
    st.nvs_active = (err == ESP_OK) && nvs_open("btstack", NVS_READWRITE, &st.nvs) == ESP_OK;
    st.tlv.get_tag = &tlv_get_tag;
    st.tlv.store_tag = &tlv_store_tag;
    st.tlv.delete_tag = &tlv_delete_tag;
    btstack_tlv_set_instance(&st.tlv, nullptr);
    hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(&st.tlv, nullptr));
  }

  // ---- VHCI transport -------------------------------------------------------------
  static void tx_ready(void*) {
    State& st = s();
    uint8_t event[] = {HCI_EVENT_TRANSPORT_PACKET_SENT, 0};
    if (st.packet_handler) st.packet_handler(HCI_EVENT_PACKET, event, sizeof(event));
  }

  static void deliver_packets(void*) {
    State& st = s();
    uint8_t* const rx = &st.rx_buffer[HCI_INCOMING_PRE_BUFFER_SIZE];
    xSemaphoreTake(st.ring_mutex, portMAX_DELAY);
    while (btstack_ring_buffer_bytes_available(&st.ring)) {
      uint32_t n;
      uint8_t len_tag[2];
      btstack_ring_buffer_read(&st.ring, len_tag, 2, &n);
      uint32_t len = little_endian_read_16(len_tag, 0);
      btstack_ring_buffer_read(&st.ring, rx, len, &n);
      xSemaphoreGive(st.ring_mutex);
      if (st.packet_handler) st.packet_handler(rx[0], &rx[1], len - 1);
      xSemaphoreTake(st.ring_mutex, portMAX_DELAY);
    }
    xSemaphoreGive(st.ring_mutex);
  }

  // VHCI callbacks run in the controller task
  static void host_send_available_cb(void) {
    btstack_run_loop_execute_on_main_thread(&s().tx_ready_reg);
  }

  static int host_recv_cb(uint8_t* data, uint16_t len) {
    State& st = s();
    xSemaphoreTake(st.ring_mutex, portMAX_DELAY);
    if (btstack_ring_buffer_bytes_free(&st.ring) < (uint32_t)len + 2) {
      xSemaphoreGive(st.ring_mutex);
      return 0;  // dropped
    }
    uint8_t len_tag[2];
    little_endian_store_16(len_tag, 0, len);
    btstack_ring_buffer_write(&st.ring, len_tag, 2);
    btstack_ring_buffer_write(&st.ring, data, len);
    xSemaphoreGive(st.ring_mutex);
    btstack_run_loop_execute_on_main_thread(&st.rx_ready_reg);
    return 0;
  }

  static void t_init(const void*) {
    State& st = s();
    if (!st.ring_mutex) st.ring_mutex = xSemaphoreCreateMutex();
  }

  static bool controller_start() {
#ifdef BLUEA2DP_HAS_BT_MODE_API
    return btStartMode(BT_MODE_CLASSIC_BT);
#else
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) return true;
    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE &&
        esp_bt_controller_init(&cfg) != ESP_OK)
      return false;
    return esp_bt_controller_enable(ESP_BT_MODE_BTDM) == ESP_OK;
#endif
  }

  static int t_open() {
    State& st = s();
    btstack_ring_buffer_init(&st.ring, st.ring_storage, sizeof(st.ring_storage));
    if (!controller_start()) return -1;
    static esp_vhci_host_callback_t vhci_cb;
    vhci_cb.notify_host_send_available = &host_send_available_cb;
    vhci_cb.notify_host_recv = &host_recv_cb;
    esp_vhci_host_register_callback(&vhci_cb);
    return 0;
  }

  static int t_close() {
    esp_bt_controller_disable();
    return 0;
  }

  static void t_register_packet_handler(void (*handler)(uint8_t, uint8_t*, uint16_t)) {
    s().packet_handler = handler;
  }

  static int t_can_send_packet_now(uint8_t) { return esp_vhci_host_check_send_available(); }

  static int t_send_packet(uint8_t packet_type, uint8_t* packet, int size) {
    // HCI_OUTGOING_PRE_BUFFER_SIZE (1) leaves room for the H4 packet type
    size++;
    packet--;
    *packet = packet_type;
    esp_vhci_host_send_packet(packet, size);
    return 0;
  }

  static const hci_transport_t* transport() {
    State& st = s();
    if (!st.transport_ready) {
      st.tx_ready_reg.callback = &tx_ready;
      st.tx_ready_reg.context = nullptr;
      st.rx_ready_reg.callback = &deliver_packets;
      st.rx_ready_reg.context = nullptr;
      memset(&st.transport, 0, sizeof(st.transport));
      st.transport.name = "esp32-vhci";
      st.transport.init = &t_init;
      st.transport.open = &t_open;
      st.transport.close = &t_close;
      st.transport.register_packet_handler = &t_register_packet_handler;
      st.transport.can_send_packet_now = &t_can_send_packet_now;
      st.transport.send_packet = &t_send_packet;
      st.transport_ready = true;
    }
    return &st.transport;
  }
};

typedef PortESP32 Port;

}  // namespace blue_a2dp

#endif  // ARDUINO_ARCH_ESP32
