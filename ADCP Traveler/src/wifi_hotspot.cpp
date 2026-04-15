#include "wifi_hotspot.h"
#include "config.h"

#include <WebServer.h>
#include <WiFi.h>

namespace {
WebServer server(80);
WifiHotspot *activeHotspot = nullptr;
}  // namespace

WifiHotspot::WifiHotspot() : onboardLedPin_(2), ledOn_(false) {}

void WifiHotspot::setLed(bool on) {
  ledOn_ = on;
  digitalWrite(onboardLedPin_, on ? HIGH : LOW);
}

void WifiHotspot::sendLedStateResponse() {
  const String state = ledOn_ ? "on" : "off";
  server.send(200, "text/plain", "LED is " + state + "\n");
}

void WifiHotspot::handleRoot() {
  String response;
  response += "ESP32 LED control\n";
  response += "GET /led/on\n";
  response += "GET /led/off\n";
  response += "GET /led/status\n";
  server.send(200, "text/plain", response);
}

void WifiHotspot::handleLedOn() {
  setLed(true);
  sendLedStateResponse();
}

void WifiHotspot::handleLedOff() {
  setLed(false);
  sendLedStateResponse();
}

void WifiHotspot::handleLedStatus() {
  sendLedStateResponse();
}

void WifiHotspot::handleNotFound() {
  server.send(404, "text/plain", "Not found\n");
}

void WifiHotspot::begin(uint8_t ledPin) {
  onboardLedPin_ = ledPin;
  activeHotspot = this;

  pinMode(onboardLedPin_, OUTPUT);
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

  server.on("/", []() { activeHotspot->handleRoot(); });
  server.on("/led/on", []() { activeHotspot->handleLedOn(); });
  server.on("/led/off", []() { activeHotspot->handleLedOff(); });
  server.on("/led/status", []() { activeHotspot->handleLedStatus(); });
  server.onNotFound([]() { activeHotspot->handleNotFound(); });
  server.begin();

  Serial.println("HTTP server started");
}

void WifiHotspot::update() {
  server.handleClient();
}

bool WifiHotspot::isLedOn() const {
  return ledOn_;
}
