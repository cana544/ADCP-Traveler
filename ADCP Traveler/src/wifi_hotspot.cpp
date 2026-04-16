#include "wifi_hotspot.h"

#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "config.h"

namespace {
WebServer server(80);
WifiHotspot* activeHotspot = nullptr;
}  // namespace

WifiHotspot::WifiHotspot()
    : onboardLedPin_(2), ledOn_(false), currentPwm_(0), lastPwm_(255) {}

void WifiHotspot::setLed(bool on) {
  ledOn_ = on;
  if (on) {
    // Turn on: restore to last brightness
    currentPwm_ = lastPwm_;
    ledcWrite(0, lastPwm_);
  } else {
    // Turn off: save current brightness, then turn off
    if (currentPwm_ > 0) {
      lastPwm_ = currentPwm_;
    }
    currentPwm_ = 0;
    ledcWrite(0, 0);
  }
}

void WifiHotspot::setPwm(uint8_t value) {
  currentPwm_ = value;
  ledOn_ = (value > 0);
  if (value > 0) {
    lastPwm_ = value;  // Remember this brightness for next on
  }
  ledcWrite(0, value);
}

void WifiHotspot::sendLedStateResponse() {
  const String state = ledOn_ ? "on" : "off";
  String response =
      "{\"state\":\"" + state + "\",\"pwm\":" + String(currentPwm_) + "}";
  server.send(200, "application/json", response);
}

void WifiHotspot::sendWifiSignalResponse() {
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

bool WifiHotspot::serveFile(const char* path, const char* contentType) {
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    Serial.print("Missing SPIFFS file: ");
    Serial.println(path);
    return false;
  }

  server.streamFile(file, contentType);
  file.close();
  return true;
}

namespace {
void redirectToRoot() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}
}  // namespace

void WifiHotspot::handleRoot() {
  if (serveFile("/index.html", "text/html")) {
    return;
  }

  String response;
  response += "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  response +=
      "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  response +=
      "<title>ESP32 Control</title></head><body "
      "style='font-family:Arial,sans-serif;";
  response += "padding:24px;line-height:1.6;'>";
  response += "<h1>ESP32 control page not found</h1>";
  response +=
      "<p>The ESP32 web server is running, but <code>index.html</code> was not "
      "found in SPIFFS.</p>";
  response +=
      "<p>Upload the filesystem image from PlatformIO, then refresh "
      "<code>http://192.168.4.1/</code>.</p>";
  response +=
      "<p>Available API routes: <code>/led/on</code>, <code>/led/off</code>, "
      "<code>/led/status</code>.</p>";
  response += "</body></html>";
  server.send(200, "text/html", response);
}

void WifiHotspot::handleLedOn() {
  setLed(true);
  sendLedStateResponse();
}

void WifiHotspot::handleLedOff() {
  setLed(false);
  sendLedStateResponse();
}

void WifiHotspot::handleLedStatus() { sendLedStateResponse(); }

void WifiHotspot::handleWifiSignal() { sendWifiSignalResponse(); }

void WifiHotspot::handleLedPwm() {
  const String uri = server.uri();
  int slashIndex = uri.lastIndexOf('/');
  if (slashIndex == -1) {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
    return;
  }

  String valueStr = uri.substring(slashIndex + 1);
  int pwmValue = valueStr.toInt();

  if (pwmValue < 0 || pwmValue > 255) {
    server.send(400, "application/json", "{\"error\":\"PWM must be 0-255\"}");
    return;
  }

  setPwm((uint8_t)pwmValue);
  sendLedStateResponse();
}

void WifiHotspot::handleNotFound() {
  const String uri = server.uri();

  if (server.method() == HTTP_GET &&
      (uri == "/generate_204" || uri == "/hotspot-detect.html" ||
       uri == "/connecttest.txt" || uri == "/ncsi.txt" || uri == "/fwlink")) {
    redirectToRoot();
    return;
  }

  if (server.method() == HTTP_GET && uri.startsWith("/led/pwm/")) {
    handleLedPwm();
    return;
  }

  if (server.method() == HTTP_GET && uri.indexOf('.') == -1) {
    handleRoot();
    return;
  }

  server.send(404, "text/plain", "Not found\n");
}

void WifiHotspot::begin(uint8_t ledPin) {
  onboardLedPin_ = ledPin;
  activeHotspot = this;

  pinMode(onboardLedPin_, OUTPUT);

  // Configure LEDC PWM for the LED
  ledcSetup(0, 5000, 8);  // Channel 0, 5kHz frequency, 8-bit resolution (0-255)
  ledcAttachPin(onboardLedPin_, 0);

  setLed(false);

  WiFi.mode(WIFI_AP);

  const bool started =
      WiFi.softAP(Config::Wifi::AP_SSID, Config::Wifi::AP_PASSWORD);
  if (!started) {
    Serial.println("Failed to start access point");
    return;
  }

  Serial.print("Access point started. SSID: ");
  Serial.println(Config::Wifi::AP_SSID);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    return;
  }

  server.on("/", []() { activeHotspot->handleRoot(); });
  server.on("/index.html", []() { activeHotspot->handleRoot(); });
  server.on("/style.css", HTTP_GET,
            []() { activeHotspot->serveFile("/style.css", "text/css"); });
  server.on("/script.js", HTTP_GET, []() {
    activeHotspot->serveFile("/script.js", "application/javascript");
  });
  server.on("/generate_204", HTTP_GET, []() { redirectToRoot(); });
  server.on("/hotspot-detect.html", HTTP_GET, []() { redirectToRoot(); });
  server.on("/connecttest.txt", HTTP_GET, []() { redirectToRoot(); });
  server.on("/ncsi.txt", HTTP_GET, []() { redirectToRoot(); });
  server.on("/fwlink", HTTP_GET, []() { redirectToRoot(); });
  server.on("/led/on", []() { activeHotspot->handleLedOn(); });
  server.on("/led/off", []() { activeHotspot->handleLedOff(); });
  server.on("/led/status", []() { activeHotspot->handleLedStatus(); });
  server.on("/wifi/signal", []() { activeHotspot->handleWifiSignal(); });
  server.onNotFound([]() { activeHotspot->handleNotFound(); });
  server.begin();

  Serial.println("HTTP server started");
}

void WifiHotspot::update() { server.handleClient(); }

bool WifiHotspot::isLedOn() const { return ledOn_; }
