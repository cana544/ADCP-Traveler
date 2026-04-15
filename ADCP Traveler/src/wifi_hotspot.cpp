#include "wifi_hotspot.h"
#include "config.h"

#include <SPIFFS.h>
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
  server.send(200, "application/json", "{\"state\":\"" + state + "\"}");
}

bool WifiHotspot::serveFile(const char *path, const char *contentType) {
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
  response += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  response += "<title>ESP32 Control</title></head><body style='font-family:Arial,sans-serif;";
  response += "padding:24px;line-height:1.6;'>";
  response += "<h1>ESP32 control page not found</h1>";
  response += "<p>The ESP32 web server is running, but <code>index.html</code> was not found in SPIFFS.</p>";
  response += "<p>Upload the filesystem image from PlatformIO, then refresh <code>http://192.168.4.1/</code>.</p>";
  response += "<p>Available API routes: <code>/led/on</code>, <code>/led/off</code>, <code>/led/status</code>.</p>";
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

void WifiHotspot::handleLedStatus() {
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
  server.on("/style.css",
            HTTP_GET,
            []() { activeHotspot->serveFile("/style.css", "text/css"); });
  server.on("/script.js",
            HTTP_GET,
            []() { activeHotspot->serveFile("/script.js", "application/javascript"); });
  server.on("/generate_204", HTTP_GET, []() { redirectToRoot(); });
  server.on("/hotspot-detect.html", HTTP_GET, []() { redirectToRoot(); });
  server.on("/connecttest.txt", HTTP_GET, []() { redirectToRoot(); });
  server.on("/ncsi.txt", HTTP_GET, []() { redirectToRoot(); });
  server.on("/fwlink", HTTP_GET, []() { redirectToRoot(); });
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
