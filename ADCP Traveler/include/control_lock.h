#ifndef CONTROL_LOCK_H
#define CONTROL_LOCK_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Network callbacks and the main loop share motion state. Recursive acquisition
// lets status serialization run inside a command without exposing partial state.
class ControlLock {
 public:
  explicit ControlLock(SemaphoreHandle_t mutex) : mutex_(mutex) {
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
  }
  ~ControlLock() { xSemaphoreGiveRecursive(mutex_); }
  ControlLock(const ControlLock&) = delete;
  ControlLock& operator=(const ControlLock&) = delete;
 private:
  SemaphoreHandle_t mutex_;
};

#endif
