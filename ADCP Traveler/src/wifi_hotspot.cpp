#include "wifi_hotspot.h"

#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "wifi_signal_reporter.h"

namespace {
WebServer server(80);
WifiHotspot* activeHotspot = nullptr;
}  // namespace

WifiHotspot::WifiHotspot() : onboardLedPin_(2), ledController_() {}

void WifiHotspot::setLed(bool on) { ledController_.setLed(on); }

void WifiHotspot::setPwm(uint8_t value) { ledController_.setPwm(value); }

void WifiHotspot::sendLedStateResponse() {
  const String state = ledController_.isOn() ? "on" : "off";
  String response = "{\"state\":\"" + state +
                    "\",\"pwm\":" + String(ledController_.currentPwm()) + "}";
  server.send(200, "application/json", response);
}

void WifiHotspot::sendWifiSignalResponse() {
  WifiSignalReporter::sendResponse(server);
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

  if (server.method() == HTTP_GET && uri.startsWith("/led/pwm/")) {
    handleLedPwm();
    return;
  }

  server.send(404, "text/plain", "Not found\n");
}

void WifiHotspot::begin(uint8_t ledPin) {
  onboardLedPin_ = ledPin;
  activeHotspot = this;

  ledController_.begin(onboardLedPin_);

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
  server.on("/led/on", []() { activeHotspot->handleLedOn(); });
  server.on("/led/off", []() { activeHotspot->handleLedOff(); });
  server.on("/led/status", []() { activeHotspot->handleLedStatus(); });
  server.on("/wifi/signal", []() { activeHotspot->handleWifiSignal(); });
  server.onNotFound([]() { activeHotspot->handleNotFound(); });
  server.begin();

  Serial.println("HTTP server started");
}

void WifiHotspot::update() { server.handleClient(); }

bool WifiHotspot::isLedOn() const { return ledController_.isOn(); }
