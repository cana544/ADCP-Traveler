#include "wifi_signal_reporter.h"

#include <WiFi.h>
#include <esp_wifi.h>

#include <cstring>

namespace WifiSignalReporter {
void sendResponse(WebServer& server) {
  wifi_sta_list_t stationList;
  memset(&stationList, 0, sizeof(stationList));

  esp_err_t result = esp_wifi_ap_get_sta_list(&stationList);
  if (result != ESP_OK) {
    server.send(500, "application/json",
                "{\"connected\":false,\"rssi\":null,\"quality\":0,"
                "\"label\":\"unavailable\"}");
    return;
  }

  if (stationList.num == 0) {
    server.send(200, "application/json",
                "{\"connected\":false,\"rssi\":null,\"quality\":0,"
                "\"label\":\"no device\"}");
    return;
  }

  const int rssi = stationList.sta[0].rssi;
  int quality = 0;

  if (rssi >= -55) {
    quality = 4;
  } else if (rssi >= -67) {
    quality = 3;
  } else if (rssi >= -75) {
    quality = 2;
  } else if (rssi >= -85) {
    quality = 1;
  }

  String label = "weak";
  if (quality >= 4) {
    label = "excellent";
  } else if (quality == 3) {
    label = "good";
  } else if (quality == 2) {
    label = "fair";
  }

  String response = "{\"connected\":true,\"rssi\":";
  response += String(rssi);
  response += ",\"quality\":";
  response += String(quality);
  response += ",\"label\":\"";
  response += label;
  response += "\"}";
  server.send(200, "application/json", response);
}
}  // namespace WifiSignalReporter