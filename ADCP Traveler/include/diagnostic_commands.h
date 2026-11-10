#pragma once
#include <cstddef>
#include <cstring>
class DiagnosticCommands {
 public:
  enum Command { NONE, INVALID, STOP, LOG_ON, LOG_OFF, ADAPT_ON, ADAPT_OFF, ADAPT_RESET, IMU_CHECK,
                 ADAPT_START_ON, ADAPT_START_OFF, ADAPT_LOAD_ON, ADAPT_LOAD_OFF };
  Command feed(char c) {
    if (c == '\r') return NONE;
    if (c != '\n') {
      if (size_ + 1 < sizeof(line_) && !overflow_) line_[size_++] = c;
      else overflow_ = true;
      return NONE;
    }
    line_[size_] = 0;
    Command command = INVALID;
    if (!overflow_) {
      if (!std::strcmp(line_, "STOP")) command = STOP;
      else if (!std::strcmp(line_, "LOG ON")) command = LOG_ON;
      else if (!std::strcmp(line_, "LOG OFF")) command = LOG_OFF;
      else if (!std::strcmp(line_, "ADAPT ON")) command = ADAPT_ON;
      else if (!std::strcmp(line_, "ADAPT OFF")) command = ADAPT_OFF;
      else if (!std::strcmp(line_, "ADAPT RESET")) command = ADAPT_RESET;
      else if (!std::strcmp(line_, "ADAPT START ON")) command = ADAPT_START_ON;
      else if (!std::strcmp(line_, "ADAPT START OFF")) command = ADAPT_START_OFF;
      else if (!std::strcmp(line_, "ADAPT LOAD ON")) command = ADAPT_LOAD_ON;
      else if (!std::strcmp(line_, "ADAPT LOAD OFF")) command = ADAPT_LOAD_OFF;
      else if (!std::strcmp(line_, "IMU CHECK")) command = IMU_CHECK;
    }
    size_ = 0; overflow_ = false;
    return command;
  }
 private:
  char line_[48] = {};
  size_t size_ = 0;
  bool overflow_ = false;
};
