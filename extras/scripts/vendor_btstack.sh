#!/usr/bin/env bash
# Vendors the subset of BlueKitchen btstack needed for A2DP/AVRCP (Classic, SBC)
# into this library's src/ directory.
#
# Usage: extras/scripts/vendor_btstack.sh [path/to/btstack]
#
# Arduino only puts <library>/src on the include path, so btstack's own layout
# (src/, src/classic, src/ble) is kept and the platform and 3rd-party codec files
# are flattened into src/. Files that only make sense on one platform get a
# preprocessor guard prepended so the Arduino build can compile everything in src/.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB="$(cd "$HERE/../.." && pwd)"
SRC_BTSTACK="${1:-${BTSTACK_DIR:-$(ls -d "$HOME"/.arduino15/packages/rp2040/hardware/rp2040/*/pico-sdk/lib/btstack 2>/dev/null | tail -1)}}"

if [[ ! -f "$SRC_BTSTACK/src/hci.c" ]]; then
  echo "btstack sources not found. Pass the path to a btstack checkout." >&2
  exit 1
fi
echo "Vendoring from: $SRC_BTSTACK"

SRC_ROOT="$LIB/src"
DST="$SRC_ROOT/btstack"
# remove previous vendored content (only what this script creates)
rm -rf "${DST:?}"
mkdir -p "$DST"
# files of an older layout, where btstack lived directly in src/
rm -rf "${SRC_ROOT:?}/classic" "${SRC_ROOT:?}/ble"
find "$SRC_ROOT" -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' -o -name '*.inc' \) \
  ! -name 'BlueA2DP.h' ! -name 'btstack_config.h' -delete

# ---- core (src/*.c|h) ------------------------------------------------------
for f in "$SRC_BTSTACK"/src/*.[ch]; do
  b="$(basename "$f")"
  [[ "$b" == *.h ]] && { cp "$f" "$DST/$b"; continue; }
  case "$b" in
    # not needed for A2DP, or need external dependencies
    btstack_audio_generator.c|btstack_audio_player.c|btstack_audio_song.c|\
    btstack_lc3*|btstack_hid*|btstack_hsm.c|btstack_fsm.c|\
    hci_transport_h5.c|hci_transport_em9304_spi.c|btstack_uart_slip_wrapper.c|\
    btstack_slip.c|btstack_chunk_buffer.c|btstack_tlv_builder.c|btstack_base64_*|\
    hci_dump_buffered.c|hci_dump_dispatch.c|btstack_ltv_builder.c|\
    hci_event_builder.c|btstack_sample_rate_compensation.c)
      continue;;
  esac
  cp "$f" "$DST/$b"
done

# ---- classic ---------------------------------------------------------------
mkdir -p "$DST/classic"
cp "$SRC_BTSTACK"/src/classic/*.h "$DST/classic/"
for b in a2dp a2dp_sink a2dp_source avdtp avdtp_acceptor avdtp_initiator avdtp_sink \
         avdtp_source avdtp_util avrcp avrcp_controller avrcp_target \
         avrcp_media_item_iterator btstack_link_key_db_memory btstack_link_key_db_tlv \
         btstack_sbc_bluedroid btstack_sbc_decoder_bluedroid btstack_sbc_encoder_bluedroid \
         btstack_sbc_plc device_id_server sdp_client sdp_server sdp_util; do
  cp "$SRC_BTSTACK/src/classic/$b.c" "$DST/classic/$b.c"
done

# ---- ble (headers only needed because btstack.h may include them) ---------
mkdir -p "$DST/ble"
cp "$SRC_BTSTACK"/src/ble/*.h "$DST/ble/"
# (no BLE sources: this library only does Classic Bluetooth)

# ---- SBC codec (3rd-party/bluedroid) flattened into src/ -------------------
for d in decoder encoder; do
  cp "$SRC_BTSTACK/3rd-party/bluedroid/$d/include/"*.h "$DST/"
  cp "$SRC_BTSTACK/3rd-party/bluedroid/$d/srce/"*.c "$DST/"
  cp "$SRC_BTSTACK/3rd-party/bluedroid/$d/srce/"*.inc "$DST/" 2>/dev/null || true
done
# AES (used by btstack_crypto.c)
cp "$SRC_BTSTACK/3rd-party/rijndael/rijndael."[ch] "$DST/"
# XML parser header (btstack.h includes pbap_client.h, which needs it)
cp "$SRC_BTSTACK/3rd-party/yxml/yxml.h" "$DST/"

# ---- platform files (guarded) ----------------------------------------------
PLATFORM_FILES=(
  "embedded/hal_cpu.h" "embedded/hal_time_ms.h" "embedded/hal_tick.h"
  "embedded/hal_uart_dma.h" "embedded/hal_flash_bank.h" "embedded/hal_audio.h"
  "embedded/hal_led.h" "embedded/hal_stdin.h"
  "embedded/btstack_run_loop_embedded.h"
  "freertos/btstack_run_loop_freertos.h"
  "posix/btstack_run_loop_posix.h" "posix/btstack_tlv_posix.h"
  "posix/hci_dump_posix_stdout.h" "posix/hci_dump_posix_fs.h"
)
for p in "${PLATFORM_FILES[@]}"; do cp "$SRC_BTSTACK/platform/$p" "$DST/"; done

guard() { # guard <file> <condition>
  local f="$1" cond="$2"
  { echo "#if $cond"; cat "$f"; echo; echo "#endif"; } > "$f.tmp" && mv "$f.tmp" "$f"
}

cp "$SRC_BTSTACK/platform/freertos/btstack_run_loop_freertos.c" "$DST/"
cp "$SRC_BTSTACK/platform/posix/btstack_run_loop_posix.c" "$DST/"
cp "$SRC_BTSTACK/platform/posix/btstack_tlv_posix.c" "$DST/"
cp "$SRC_BTSTACK/platform/posix/hci_dump_posix_stdout.c" "$DST/"
cp "$SRC_BTSTACK/platform/posix/hci_dump_posix_fs.c" "$DST/"
cp "$SRC_BTSTACK/platform/libusb/hci_transport_h2_libusb.c" "$DST/"
cp "$SRC_BTSTACK/platform/embedded/btstack_run_loop_embedded.c" "$DST/"

# Platform conditions (kept in one place so the guards are consistent)
POSIX_COND='defined(__linux__) || defined(__APPLE__)'
FREERTOS_COND='defined(ARDUINO_ARCH_ESP32)'
for f in btstack_run_loop_posix.c btstack_tlv_posix.c hci_dump_posix_stdout.c hci_dump_posix_fs.c; do
  guard "$DST/$f" "$POSIX_COND"
done
guard "$DST/hci_transport_h2_libusb.c" "($POSIX_COND) && defined(BLUEA2DP_USE_LIBUSB)"
guard "$DST/btstack_run_loop_freertos.c" "$FREERTOS_COND"
# generic embedded run loop is unused (ports bring their own); drop it
rm -f "$DST/btstack_run_loop_embedded.c"

# ---- core files: skip on RP2040 (the arduino-pico core ships btstack) ------
CORE_COND='!defined(ARDUINO_ARCH_RP2040) || defined(BLUEA2DP_FORCE_VENDORED_BTSTACK)'
while IFS= read -r -d '' f; do
  case "$(basename "$f")" in
    btstack_run_loop_posix.c|btstack_tlv_posix.c|hci_dump_posix_stdout.c|hci_dump_posix_fs.c|\
    hci_transport_h2_libusb.c|btstack_run_loop_freertos.c) continue;;
  esac
  head -n1 "$f" | grep -q "CORE_COND_GUARD" && continue
  guard "$f" "$CORE_COND /* CORE_COND_GUARD */"
done < <(find "$DST" -type f -name '*.c' -print0)

# ---- includes: make every #include relative to the file -------------------------------
# Arduino only has <library>/src on the include path, so btstack's own style
# (#include "classic/a2dp.h" from anywhere) cannot work in src/btstack. btstack_config.h
# is the exception: it lives in src/ and is found through the include path.
python3 - "$DST" <<'PY'
import os, re, sys
root = sys.argv[1]
names = {}
for d, _, fs in os.walk(root):
    for f in fs:
        names[os.path.relpath(os.path.join(d, f), root)] = True
inc = re.compile(r'^(\s*#\s*include\s*)[<"]([^">]+)[>"]', re.M)
changed = 0
for d, _, fs in os.walk(root):
    for f in fs:
        if not f.endswith(('.c', '.h', '.inc')): continue
        path = os.path.join(d, f)
        text = open(path, encoding='utf-8', errors='surrogateescape').read()
        def fix(m):
            name = m.group(2)
            if name == 'btstack_config.h': return m.group(0)
            if os.path.exists(os.path.join(d, name)):
                return m.group(1) + '"' + name + '"'
            if name in names:
                rel = os.path.relpath(os.path.join(root, name), d)
                return m.group(1) + '"' + rel + '"'
            return m.group(0)
        new = inc.sub(fix, text)
        if new != text:
            open(path, 'w', encoding='utf-8', errors='surrogateescape').write(new)
            changed += 1
print("rewrote includes in", changed, "files")
PY

# ---- local patches ----------------------------------------------------------------------
# libusb transport: give the controller back to the OS (btusb / BlueZ) when the transport is
# closed. Upstream only reattaches the kernel driver in its error paths, which leaves the dongle
# without driver until it is replugged.
python3 - "$DST/hci_transport_h2_libusb.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = '            libusb_release_interface(handle, 1);\n#endif\n            log_info("Libusb shutdown complete");'
new = ('            libusb_release_interface(handle, 1);\n#endif\n'
       '#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__CYGWIN__) && !defined(__FreeBSD__)\n'
       '            libusb_attach_kernel_driver(handle, 0);  /* BlueA2DP patch: reattach the OS driver */\n'
       '#endif\n            log_info("Libusb shutdown complete");')
if old not in s:
    sys.exit("vendor_btstack.sh: libusb patch does not apply, btstack changed?")
open(p, 'w').write(s.replace(old, new, 1))
PY

# libusb: skip libusb_set_configuration when the device is already configured (it stalls
# after the driver detach with CSR clones)
python3 - "$DST/hci_transport_h2_libusb.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = '    const int configuration = 1;\n    log_info("setting configuration %d...", configuration);\n    r = libusb_set_configuration(aHandle, configuration);'
new = ('    const int configuration = 1;\n    // BlueA2DP patch: skip when already configured (stalls after the driver detach)\n'
       '    int current = 0;\n    r = libusb_get_configuration(aHandle, &current);\n'
       '    if (r == 0 && current == configuration) {\n        r = 0;\n    } else {\n'
       '    log_info("setting configuration %d...", configuration);\n'
       '    r = libusb_set_configuration(aHandle, configuration);\n    }')
if old not in s:
    sys.exit("libusb configuration patch does not apply, btstack changed?")
open(p, 'w').write(s.replace(old, new, 1))
PY

# ---- provenance ------------------------------------------------------------
{
  echo "btstack source: $SRC_BTSTACK"
  grep -h "BTSTACK_VERSION\|btstack_version" "$SRC_BTSTACK/src/btstack_version.h" 2>/dev/null | head -3 || true
  ( cd "$SRC_BTSTACK" && git log -1 --format='commit %H %cd' 2>/dev/null ) || true
  date -u +"vendored: %Y-%m-%d"
} > "$LIB/extras/BTSTACK_VERSION.txt"

echo "done: $(find "$DST" -name '*.c' | wc -l) C files, $(du -sh "$DST" | cut -f1) in $DST"
