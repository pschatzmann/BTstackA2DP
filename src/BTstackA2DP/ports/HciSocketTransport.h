// HCI transport for Linux through the HCI user channel (AF_BLUETOOTH socket).
//
// The controller must already be set up by the kernel and BlueZ, i.e. hci0 exists
// (on a Raspberry Pi the onboard Bluetooth is brought up by the hciuart / btattach service,
// which also loads the firmware). btstack then takes the adapter over: stop the bluetooth
// service first (sudo systemctl stop bluetooth), otherwise the bind fails with EBUSY.
// After the program ends the adapter is down again: sudo systemctl restart bluetooth.
//
// Needs root (CAP_NET_ADMIN). Selected with PortPosix::set_hci_device(0) or blue_a2dp --hci 0.
#pragma once

#if defined(__linux__)

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <btstack/btstack_run_loop.h>
#include <btstack/hci_transport.h>

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#ifndef BTPROTO_HCI
#define BTPROTO_HCI 1
#endif

namespace blue_a2dp {

class HciSocketTransport {
 public:
  /// The transport description for hci_init(); the config must be a hci_transport_config_linux_t
  static const hci_transport_t* instance() {
    static const hci_transport_t transport = {
        "HCI_SOCKET_USER",
        &init,
        &open,
        &close,
        &register_packet_handler,
        &can_send_packet_now,
        &send_packet,
        &set_baudrate,
        &reset_link,
        &set_sco_config,
    };
    return &transport;
  }

 private:
  // same layout as struct sockaddr_hci in <bluetooth/hci.h>
  struct SockAddrHci {
    unsigned short hci_family;
    unsigned short hci_dev;
    unsigned short hci_channel;
  };
  static const unsigned short HCI_CHANNEL_USER_ = 1;

  struct State {
    int device_id = 0;
    int fd = -1;
    btstack_data_source_t data_source;
    void (*handler)(uint8_t packet_type, uint8_t* packet, uint16_t size) = nullptr;
    uint8_t rx_buffer[2048];
    uint8_t tx_buffer[2048];
  };

  static State& s() {
    static State state;
    return state;
  }

  static void init(const void* transport_config) {
    const hci_transport_config_linux_t* config = (const hci_transport_config_linux_t*)transport_config;
    if (config && config->type == HCI_TRANSPORT_CONFIG_LINUX) s().device_id = config->device_id;
  }

  static int open(void) {
    State& st = s();
    if (st.fd >= 0) return 0;
    st.fd = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
    if (st.fd < 0) {
      fprintf(stderr, "BTstackA2DP: socket(AF_BLUETOOTH) failed: %s\n", strerror(errno));
      return -1;
    }
    SockAddrHci addr;
    memset(&addr, 0, sizeof(addr));
    addr.hci_family = AF_BLUETOOTH;
    addr.hci_dev = (unsigned short)st.device_id;
    addr.hci_channel = HCI_CHANNEL_USER_;
    if (bind(st.fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      int err = errno;
      fprintf(stderr, "BTstackA2DP: cannot take hci%d (user channel): %s\n", st.device_id,
              strerror(err));
      if (err == EBUSY)
        fprintf(stderr, "  the adapter is used by BlueZ: sudo systemctl stop bluetooth\n");
      else if (err == ENODEV)
        fprintf(stderr, "  there is no hci%d: check `hciconfig -a` / `ls /sys/class/bluetooth`\n",
                st.device_id);
      else if (err == EPERM || err == EACCES)
        fprintf(stderr, "  needs root: run with sudo\n");
      ::close(st.fd);
      st.fd = -1;
      return -1;
    }
    btstack_run_loop_set_data_source_fd(&st.data_source, st.fd);
    btstack_run_loop_set_data_source_handler(&st.data_source, &on_readable);
    btstack_run_loop_enable_data_source_callbacks(&st.data_source, DATA_SOURCE_CALLBACK_READ);
    btstack_run_loop_add_data_source(&st.data_source);
    return 0;
  }

  static int close(void) {
    State& st = s();
    if (st.fd < 0) return 0;
    btstack_run_loop_remove_data_source(&st.data_source);
    ::close(st.fd);  // closing the user channel brings the adapter down again
    st.fd = -1;
    return 0;
  }

  static void register_packet_handler(void (*handler)(uint8_t packet_type, uint8_t* packet,
                                                      uint16_t size)) {
    s().handler = handler;
  }

  // one read() returns exactly one HCI packet, prefixed with its H4 packet type
  static void on_readable(btstack_data_source_t* ds, btstack_data_source_callback_type_t type) {
    (void)ds;
    State& st = s();
    if (type != DATA_SOURCE_CALLBACK_READ || st.fd < 0) return;
    ssize_t n = read(st.fd, st.rx_buffer, sizeof(st.rx_buffer));
    if (n < 1) {
      if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
      fprintf(stderr, "BTstackA2DP: HCI read failed: %s\n", n < 0 ? strerror(errno) : "closed");
      return;
    }
    if (st.handler) st.handler(st.rx_buffer[0], &st.rx_buffer[1], (uint16_t)(n - 1));
  }

  static int can_send_packet_now(uint8_t packet_type) {
    (void)packet_type;
    return 1;  // the kernel queues the packet; btstack keeps track of the controller's credits
  }

  static int send_packet(uint8_t packet_type, uint8_t* packet, int size) {
    State& st = s();
    if (st.fd < 0 || size + 1 > (int)sizeof(st.tx_buffer)) return -1;
    st.tx_buffer[0] = packet_type;
    memcpy(&st.tx_buffer[1], packet, size);
    ssize_t n = write(st.fd, st.tx_buffer, size + 1);
    return n == size + 1 ? 0 : -1;
  }

  static int set_baudrate(uint32_t baudrate) {
    (void)baudrate;
    return 0;  // no UART behind the socket, so there is no baud rate to set
  }

  static void reset_link(void) {}

  static void set_sco_config(uint16_t voice_setting, int num_connections) {
    (void)voice_setting;
    (void)num_connections;
  }
};

}  // namespace blue_a2dp

#endif  // __linux__
