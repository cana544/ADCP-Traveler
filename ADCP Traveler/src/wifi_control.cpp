#include "wifi_control.h"

#include <WebServer.h>
#include <WiFi.h>

namespace WifiControl {
const char *const AP_SSID = "ESP32-LED-Control";
const char *const AP_PASSWORD = "password";

namespace {
WebServer server(80);
uint8_t onboardLedPin = 2;
bool ledOn = false;

void setLed(bool on) {
  ledOn = on;
  digitalWrite(onboardLedPin, on ? HIGH : LOW);
}

void sendLedStateResponse() {
  const String state = ledOn ? "on" : "off";
  server.send(200, "text/plain", "LED is " + state + "\n");
}

void handleRoot() {
  String response;
  response += "ESP32 LED control\n";
  response += "GET /led/on\n";
  response += "GET /led/off\n";
  response += "GET /led/status\n";
  server.send(200, "text/plain", response);
}

void handleLedOn() {
  setLed(true);
  sendLedStateResponse();
}

void handleLedOff() {
  setLed(false);
  sendLedStateResponse();
}

void handleLedStatus() {
  sendLedStateResponse();
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found\n");
}
}  // namespace

void begin(uint8_t ledPin) {
  onboardLedPin = ledPin;

  pinMode(onboardLedPin, OUTPUT);
  setLed(false);

  WiFi.mode(WIFI_AP);

  const bool started = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (!started) {
    Serial.println("Failed to start access point");
    return;
  }

  Serial.print("Access point started. SSID: ");
  Serial.println(AP_SSID);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/led/on", handleLedOn);
  server.on("/led/off", handleLedOff);
  server.on("/led/status", handleLedStatus);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("HTTP server started");
}

void update() {
  server.handleClient();
}

bool isLedOn() {
  return ledOn;
}
}  // namespace WifiControl
