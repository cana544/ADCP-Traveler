#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

// BNO08x UART-SHTP framing. No waits, allocation or access to motor state.
class BnoUartTransport {
 public:
  static constexpr size_t CAPACITY = 384;
  size_t feed(uint8_t c, uint8_t* output, size_t capacity) {
    if (c == 0x7E) {
      size_t result = 0;
      if (inside_ && size_ > 0 && !dropping_) {
        const size_t declared = size_ >= 4 ? ((data_[0] | (uint16_t(data_[1]) << 8)) & 0x7FFF) : 0;
        if (!escaped_ && protocol_ && size_ >= 4 && declared == size_ && size_ <= capacity) {
          std::memcpy(output, data_, size_); result = size_;
        } else ++errors_;
      }
      inside_ = true; protocol_ = escaped_ = dropping_ = false; size_ = 0;
      return result;
    }
    if (!inside_ || dropping_) return 0;
    if (!protocol_) {
      if (c == 1) protocol_ = true;
      else { dropping_ = true; ++errors_; }
      return 0;
    }
    if (escaped_) { c ^= 0x20; escaped_ = false; }
    else if (c == 0x7D) { escaped_ = true; return 0; }
    if (size_ == CAPACITY) { dropping_ = true; ++errors_; return 0; }
    data_[size_++] = c;
    return 0;
  }
  uint32_t errors() const { return errors_; }
  static size_t encode(const uint8_t* payload, size_t size, uint8_t* output, size_t capacity) {
    size_t required = 3;
    for (size_t i = 0; i < size; ++i) required += payload[i] == 0x7E || payload[i] == 0x7D ? 2 : 1;
    if (required > capacity) return 0;
    size_t n = 0; output[n++] = 0x7E; output[n++] = 1;
    for (size_t i = 0; i < size; ++i) {
      uint8_t c = payload[i];
      if (c == 0x7E || c == 0x7D) { output[n++] = 0x7D; c ^= 0x20; }
      output[n++] = c;
    }
    output[n++] = 0x7E;
    return n;
  }
 private:
  uint8_t data_[CAPACITY] = {};
  size_t size_ = 0;
  uint32_t errors_ = 0;
  bool inside_ = false, protocol_ = false, escaped_ = false, dropping_ = false;
};
