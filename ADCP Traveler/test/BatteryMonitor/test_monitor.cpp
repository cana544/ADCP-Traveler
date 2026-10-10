#include <cassert>
#include <cmath>
#include "battery_monitor.h"
#include "config.h"
#include <limits>
uint32_t clockMs=0, adcMv=2211, reads=0;
void sample(BatteryMonitor& monitor) {
  clockMs += 1000;
  for (int i=0;i<16;++i) { monitor.update(); clockMs+=2; }
}
int main() {
  BatteryMonitor monitor; monitor.begin();
  assert(!monitor.valid());
  assert(std::fabs(BatteryMonitor::voltageFromMillivolts(2210.5263f)-12.6f)<0.001f);
  assert(std::fabs(BatteryMonitor::estimatePercent(12.15f)-70)<0.001f);
  assert(BatteryMonitor::estimatePercent(15)==100);
  assert(BatteryMonitor::estimatePercent(8)==0);
  assert(BatteryMonitor::estimatePercent(11.4f)==20);
  for (const auto& point : Config::Battery::CHARGE_TABLE)
    assert(std::fabs(BatteryMonitor::estimatePercent(point.voltage)-point.percent)<0.001f);
  for (float v=6;v<15;v+=0.01f) {
    const float percent=BatteryMonitor::estimatePercent(v);
    assert(percent>=0 && percent<=100);
  }
  assert(BatteryMonitor::estimatePercent(std::numeric_limits<float>::quiet_NaN())==0);
  sample(monitor); assert(monitor.valid()); assert(reads==16);
  monitor.update(); assert(reads==16);  // No second batch until next second.
  assert(std::fabs(monitor.voltage()-12.6027f)<0.001f);
  adcMv=2000; sample(monitor);
  assert(monitor.voltage()>11.4f && monitor.voltage()<12.6027f);
  adcMv=0; sample(monitor); assert(!monitor.valid());
  adcMv=2211; sample(monitor); assert(monitor.valid());
  assert(std::fabs(monitor.voltage()-12.6027f)<0.001f);
  adcMv=3100; sample(monitor); assert(!monitor.valid());
  clockMs=0xfffffff0; adcMv=2211; sample(monitor); sample(monitor); assert(monitor.valid());
}
