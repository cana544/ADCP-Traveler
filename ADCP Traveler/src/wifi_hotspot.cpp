#include "wifi_hotspot.h"
#include "config.h"

#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {
WebServer server(80);
WifiHotspot *activeHotspot = nullptr;
constexpr byte kDnsPort = 53;
}  // namespace

WifiHotspot::WifiHotspot() : onboardLedPin_(2), ledOn_(false) {}

void WifiHotspot::setLed(bool on) {
  ledOn_ = on;
  digitalWrite(onboardLedPin_, on ? HIGH : LOW);
}

void WifiHotspot::sendLedStateResponse() {
  const String state = ledOn_ ? "on" : "off";
  server.send(200, "application/json", "{\"state\":\"" + state + "\"}");
}

void WifiHotspot::serveFile(const char *path, const char *contentType) {
  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    server.send(500, "text/plain", "Failed to open file\n");
    return;
  }

  server.streamFile(file, contentType);
  file.close();
}

void WifiHotspot::handleRoot() {
  serveFile("/index.html", "text/html");
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

void WifiHotspot::handleCaptivePortalProbe() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/",
                    true);
  server.send(302, "text/plain", "");
}

void WifiHotspot::handleNotFound() {
  handleCaptivePortalProbe();
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

  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    return;
  }

  dnsServer_.start(kDnsPort, "*", WiFi.softAPIP());

  server.on("/", []() { activeHotspot->handleRoot(); });
  server.on("/index.html", []() { activeHotspot->handleRoot(); });
  server.on("/style.css",
            HTTP_GET,
            []() { activeHotspot->serveFile("/style.css", "text/css"); });
  server.on("/script.js",
            HTTP_GET,
            []() { activeHotspot->serveFile("/script.js", "application/javascript"); });
  server.on("/led/on", []() { activeHotspot->handleLedOn(); });
  server.on("/led/off", []() { activeHotspot->handleLedOff(); });
  server.on("/led/status", []() { activeHotspot->handleLedStatus(); });
  server.on("/generate_204", []() { activeHotspot->handleCaptivePortalProbe(); });
  server.on("/hotspot-detect.html",
            []() { activeHotspot->handleCaptivePortalProbe(); });
  server.on("/connecttest.txt", []() { activeHotspot->handleCaptivePortalProbe(); });
  server.on("/ncsi.txt", []() { activeHotspot->handleCaptivePortalProbe(); });
  server.on("/fwlink", []() { activeHotspot->handleCaptivePortalProbe(); });
  server.onNotFound([]() { activeHotspot->handleNotFound(); });
  server.begin();

  Serial.println("HTTP server started");
}

void WifiHotspot::update() {
  dnsServer_.processNextRequest();
  server.handleClient();
}

bool WifiHotspot::isLedOn() const {
  return ledOn_;
}
