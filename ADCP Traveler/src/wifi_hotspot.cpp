#include "wifi_hotspot.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "config.h"
#include "wifi_signal_reporter.h"

WifiHotspot* g_activeHotspot = nullptr;

void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (g_activeHotspot) {
    g_activeHotspot->handleWebSocketEvent(client, type, data, len);
  }
}
WifiHotspot::WifiHotspot()
    : onboardLedPin_(2), ledController_(), server_(80), ws_("/ws") {}

void WifiHotspot::setLed(bool on) { ledController_.setLed(on); }

void WifiHotspot::setPwm(uint8_t value) { ledController_.setPwm(value); }

void WifiHotspot::sendLedStateResponse(AsyncWebServerRequest* request) {
  const String state = ledController_.isOn() ? "on" : "off";
  String response = "{\"state\":\"" + state +
                    "\",\"pwm\":" + String(ledController_.currentPwm()) + "}";
  request->send(200, "application/json", response);
}

void WifiHotspot::sendWifiSignalResponse(AsyncWebServerRequest* request) {
  const bool websocketConnected = ws_.count() > 0;
  const int stationCount = WiFi.softAPgetStationNum();

  if (!websocketConnected && stationCount <= 0) {
    request->send(200, "application/json",
                  "{\"connected\":false,\"rssi\":null,\"quality\":0,"
                  "\"label\":\"no device\"}");
    return;
  }

  wifi_sta_list_t stationList;
  memset(&stationList, 0, sizeof(stationList));

  if (esp_wifi_ap_get_sta_list(&stationList) != ESP_OK ||
      stationList.num == 0) {
    request->send(200, "application/json",
                  "{\"connected\":true,\"rssi\":null,\"quality\":0,"
                  "\"label\":\"connected\"}");
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
  request->send(200, "application/json", response);
}

bool WifiHotspot::serveFile(AsyncWebServerRequest* request, const char* path,
                            const char* contentType) {
  if (!SPIFFS.exists(path)) {
    return false;
  }
  request->send(SPIFFS, path, contentType);
  return true;
}

void WifiHotspot::handleRoot(AsyncWebServerRequest* request) {
  if (!serveFile(request, "/index.html", "text/html")) {
    String response = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    response +=
        "<meta name='viewport' content='width=device-width, "
        "initial-scale=1.0'>";
    response += "<title>ESP32 Control</title></head><body>";
    response += "<h1>ESP32 control page not found</h1>";
    response += "<p>Upload SPIFFS data, then refresh http://192.168.4.1/</p>";
    response += "</body></html>";
    request->send(200, "text/html", response);
  }
}

void WifiHotspot::handleLedOn(AsyncWebServerRequest* request) {
  setLed(true);
  sendLedStateResponse(request);
}

void WifiHotspot::handleLedOff(AsyncWebServerRequest* request) {
  setLed(false);
  sendLedStateResponse(request);
}

void WifiHotspot::handleLedStatus(AsyncWebServerRequest* request) {
  sendLedStateResponse(request);
}

void WifiHotspot::handleWifiSignal(AsyncWebServerRequest* request) {
  sendWifiSignalResponse(request);
}

void WifiHotspot::handleLedPwm(AsyncWebServerRequest* request) {
  if (!request->hasParam("value")) {
    request->send(400, "application/json",
                  "{\"error\":\"Missing value parameter\"}");
    return;
  }

  int pwmValue = request->getParam("value")->value().toInt();
  if (pwmValue < 0 || pwmValue > 255) {
    request->send(400, "application/json", "{\"error\":\"PWM must be 0-255\"}");
    return;
  }

  setPwm((uint8_t)pwmValue);
  sendLedStateResponse(request);
}

void WifiHotspot::handleNotFound(AsyncWebServerRequest* request) {
  request->send(404, "text/plain", "Not found");
}

void WifiHotspot::handleWebSocketEvent(AsyncWebSocketClient* client,
                                       AwsEventType type, uint8_t* data,
                                       size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client %u connected\n", client->id());
      break;

    case WS_EVT_DATA: {
      // Parse incoming JSON command
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, data, len);

      if (error) {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
        return;
      }

      const char* cmd = doc["cmd"];
      if (!cmd) {
        return;
      }

      if (strcmp(cmd, "pwm") == 0 && doc.containsKey("value")) {
        uint8_t pwmValue = doc["value"];
        setPwm(pwmValue);

        // Send state update back to client
        DynamicJsonDocument response(256);
        response["state"] = ledController_.isOn() ? "on" : "off";
        response["pwm"] = ledController_.currentPwm();

        String responseStr;
        serializeJson(response, responseStr);
        client->text(responseStr);

      } else if (strcmp(cmd, "on") == 0) {
        setLed(true);

        DynamicJsonDocument response(256);
        response["state"] = "on";
        response["pwm"] = ledController_.currentPwm();

        String responseStr;
        serializeJson(response, responseStr);
        client->text(responseStr);

      } else if (strcmp(cmd, "off") == 0) {
        setLed(false);

        DynamicJsonDocument response(256);
        response["state"] = "off";
        response["pwm"] = ledController_.currentPwm();

        String responseStr;
        serializeJson(response, responseStr);
        client->text(responseStr);

      } else if (strcmp(cmd, "status") == 0) {
        DynamicJsonDocument response(256);
        response["state"] = ledController_.isOn() ? "on" : "off";
        response["pwm"] = ledController_.currentPwm();

        String responseStr;
        serializeJson(response, responseStr);
        client->text(responseStr);
      }
      break;
    }

    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client %u disconnected\n", client->id());
      break;

    default:
      break;
  }
}

void WifiHotspot::begin(uint8_t ledPin) {
  onboardLedPin_ = ledPin;
  g_activeHotspot = this;

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

  // Setup HTTP routes
  server_.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleRoot(request);
  });

  server_.on("/index.html", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleRoot(request);
  });

  server_.on("/style.css", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->serveFile(request, "/style.css", "text/css");
  });

  server_.on("/script.js", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->serveFile(request, "/script.js", "application/javascript");
  });

  server_.on("/led/on", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleLedOn(request);
  });

  server_.on("/led/off", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleLedOff(request);
  });

  server_.on("/led/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleLedStatus(request);
  });

  server_.on("/led/pwm", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleLedPwm(request);
  });

  server_.on("/wifi/signal", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleWifiSignal(request);
  });

  server_.onNotFound([this](AsyncWebServerRequest* request) {
    this->handleNotFound(request);
  });

  // Setup WebSocket
  ws_.onEvent(onWebSocketEvent);
  server_.addHandler(&ws_);

  server_.begin();

  Serial.println("HTTP server started on port 80");
  Serial.println("WebSocket server started at /ws");
}

void WifiHotspot::update() {
  // AsyncWebServer handles everything automatically
  // This is now a no-op but kept for compatibility
}

bool WifiHotspot::isLedOn() const { return ledController_.isOn(); }
