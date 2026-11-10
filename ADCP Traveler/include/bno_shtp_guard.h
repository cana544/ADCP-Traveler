#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// The pinned SH-2 core trusts advertised strings/report lengths. Validate the
// complete diagnostic subset before allowing its callbacks to inspect a packet.
// Fragmented SHTP cargos are deliberately unsupported; UART byte fragmentation
// remains supported by BnoUartTransport.
class BnoShtpGuard {
 public:
  bool accept(const uint8_t* packet, size_t size) {
    if (size < 4 || size > 384 || (packet[1] & 0x80) ||
        (size_t(packet[0]) | (size_t(packet[1]) << 8)) != size || packet[2] >= 8)
      return reject();
    const uint8_t* payload = packet + 4;
    const size_t length = size - 4;
    if (packet[2] == 0) {
      BnoShtpGuard candidate = *this;
      if (!candidate.advertisement(payload, length)) return reject();
      *this = candidate; return true;
    }
    switch (roles_[packet[2]]) {
      case EXECUTABLE: return length == 1 ? true : reject();
      case CONTROL: return reports(payload, length, false) ? true : reject();
      case NORMAL: return reports(payload, length, true) ? true : reject();
      case GYRO_RV:
        // Core's gyro-RV loop never advances if its advertised length is zero.
        return reportLength_[0x2A] == 14 && length > 0 && length % 14 == 0 ? true : reject();
      default: return reject();
    }
  }
  uint32_t errors() const { return errors_; }
 private:
  enum Role : uint8_t { UNKNOWN, EXECUTABLE, CONTROL, NORMAL, GYRO_RV };
  Role roles_[8] = {};
  uint8_t reportLength_[256] = {};
  uint32_t errors_ = 0;
  bool reject() { ++errors_; return false; }
  static uint16_t u16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
  static uint32_t u32(const uint8_t* p) {
    return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  }
  static bool stringFits(const uint8_t* value, size_t length, size_t destination) {
    const void* end = std::memchr(value, 0, length);
    return end && size_t(static_cast<const uint8_t*>(end) - value) < destination;
  }
  bool advertisement(const uint8_t* payload, size_t length) {
    if (!length || payload[0] != 0) return false;
    size_t cursor = 1;
    uint32_t guid = 0;
    uint8_t channel = 0;
    while (cursor < length) {
      if (length - cursor < 2) return false;
      const uint8_t tag = payload[cursor++], n = payload[cursor++];
      if (n > length - cursor) return false;
      const uint8_t* value = payload + cursor; cursor += n;
      switch (tag) {
        case 1: if (n != 4) return false; guid = u32(value); break;
        case 2: case 3: case 4: case 5:
          // Core subtracts four twice for transfer writes. Prevent underflow
          // or a zero-size transmit loop after a malformed advertisement.
          if (n != 2 || u16(value) < (tag == 4 ? 9 : 5)) return false;
          break;
        case 6: case 7:
          if (n != 1 || value[0] >= 8) return false;
          channel = value[0]; break;
        case 8:
          if (!stringFits(value, n, 32)) return false;
          break;
        case 9:
          if (!stringFits(value, n, 32)) return false;
          if (channel == 0 && (guid != 0 || std::strcmp(reinterpret_cast<const char*>(value), "command")))
            return false;
          roles_[channel] = UNKNOWN;
          if (guid == 1 && !std::strcmp(reinterpret_cast<const char*>(value), "device")) roles_[channel] = EXECUTABLE;
          if (guid == 2) {
            if (!std::strcmp(reinterpret_cast<const char*>(value), "control")) roles_[channel] = CONTROL;
            else if (!std::strcmp(reinterpret_cast<const char*>(value), "inputNormal") ||
                     !std::strcmp(reinterpret_cast<const char*>(value), "inputWake")) roles_[channel] = NORMAL;
            else if (!std::strcmp(reinterpret_cast<const char*>(value), "inputGyroRv")) roles_[channel] = GYRO_RV;
          }
          break;
        case 0x80:
          if ((guid == 0 && !stringFits(value, n, 8)) || (guid == 2 && !stringFits(value, n, 16))) return false;
          break;
        case 0x81:
          if (guid == 2) {
            if (n % 2 || n > 128) return false;
            std::memset(reportLength_, 0, sizeof(reportLength_));
            for (size_t i = 0; i < n; i += 2) {
              if (!value[i + 1]) return false;
              reportLength_[value[i]] = value[i + 1];
            }
          }
          break;
        default: break;
      }
    }
    return true;
  }
  bool reports(const uint8_t* payload, size_t size, bool input) const {
    size_t cursor = 0;
    while (cursor < size) {
      const uint8_t id = payload[cursor], length = reportLength_[id];
      if (!length || length > size - cursor) return false;
      if (input) {
        // Exact lengths for requested reports and SH-2 timing/flush records.
        uint8_t expected = 0;
        if (id == 1 || id == 2 || id == 4) expected = 10;
        else if (id == 8) expected = 12;
        else if (id == 0xFA || id == 0xFB) expected = 5;
        else if (id == 0xEF) expected = 2;
        if (!expected || length != expected || length > 16) return false;
      } else {
        if ((id == 0xF1 && length < 16) || (id == 0xFC && length < 17)) return false;
      }
      cursor += length;
    }
    return size > 0;
  }
};
