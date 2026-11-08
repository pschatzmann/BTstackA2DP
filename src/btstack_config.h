// btstack configuration for BTstackA2DP: Classic Bluetooth, A2DP (SBC) + AVRCP only.
//
// RP2040: the arduino-pico core ships btstack together with its own
// btstack_config.h, which must win. Everything else uses the configuration below.
#ifndef BTSTACKA2DP_BTSTACK_CONFIG_H
#define BTSTACKA2DP_BTSTACK_CONFIG_H

#if defined(ARDUINO_ARCH_RP2040) && !defined(BTSTACKA2DP_FORCE_VENDORED_BTSTACK)

#include_next <btstack_config.h>

#else

// ---- features ---------------------------------------------------------------
#define ENABLE_CLASSIC
#define ENABLE_LOG_ERROR
#ifdef BTSTACKA2DP_DEBUG
#define ENABLE_LOG_INFO  // protocol traces
#endif
#define ENABLE_SOFTWARE_AES128
#define HAVE_ASSERT

// ---- HCI buffers ------------------------------------------------------------
#define HCI_ACL_PAYLOAD_SIZE (1691 + 4)
#define HCI_HOST_ACL_PACKET_LEN 1024
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL

// ---- memory pools (single A2DP + AVRCP connection) ---------------------------
#define MAX_NR_AVDTP_CONNECTIONS 1
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 3  // SBC + up to 2 optional codecs
#define MAX_NR_AVRCP_CONNECTIONS 2
#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_L2CAP_CHANNELS 4
#define MAX_NR_L2CAP_SERVICES 3
#define MAX_NR_SERVICE_RECORD_ITEMS 4
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 4
#define NVM_NUM_LINK_KEYS 16
#define NVM_NUM_DEVICE_DB_ENTRIES 16

// ---- platform specifics ------------------------------------------------------
#if defined(ARDUINO_ARCH_ESP32)
  // esp_vhci_host_send_packet() needs 1 byte in front of the packet for the H4 type
  #define HCI_OUTGOING_PRE_BUFFER_SIZE 1
  #define HAVE_FREERTOS_INCLUDE_PREFIX
  #define HAVE_FREERTOS_TASK_NOTIFICATIONS
#elif defined(ARDUINO_ARCH_NRF52840) && defined(ARDUINO_ARCH_MBED)
  // nRF52840 (mbed): embedded run loop driven by hal_time_ms(); no malloc in btstack itself
  #define HAVE_EMBEDDED_TIME_MS
#elif defined(__linux__) || defined(__APPLE__)
  #define HAVE_MALLOC
  #define HAVE_POSIX_FILE_IO
  #define HAVE_POSIX_TIME
  #define HCI_INCOMING_PRE_BUFFER_SIZE 14
  #define ENABLE_PRINTF_HEXDUMP
#endif

#endif  // RP2040 vs. vendored
#endif  // BTSTACKA2DP_BTSTACK_CONFIG_H
