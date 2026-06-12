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
    : motorController_(), server_(80), ws_("/ws"), jogStopAtMs_(0) {}

void WifiHotspot::setMotorEnabled(bool enabled) {
  motorController_.setEnabled(enabled);
}

void WifiHotspot::setMotorSpeed(int speed) { motorController_.setSpeed(speed); }

void WifiHotspot::sendMotorStateResponse(AsyncWebServerRequest* request) {
  const int speed = motorController_.currentSpeed();
  String direction = "stop";
  if (speed > 0) {
    direction = "cw";
  } else if (speed < 0) {
    direction = "ccw";
  }

  const String state = motorController_.isEnabled() ? "on" : "off";
  String response = "{\"state\":\"" + state + "\",\"direction\":\"" +
                    direction + "\",\"speed\":" + String(speed) + "}";
  request->send(200, "application/json", response);
}

void WifiHotspot::broadcastMotorState() {
  const int speed = motorController_.currentSpeed();
  DynamicJsonDocument response(256);
  response["state"] = motorController_.isEnabled() ? "on" : "off";
  response["speed"] = speed;

  String responseStr;
  serializeJson(response, responseStr);
  ws_.textAll(responseStr);
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
  AsyncWebServerResponse* response =
      request->beginResponse(SPIFFS, path, contentType);
  response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "0");
  request->send(response);
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

void WifiHotspot::handleMotorOn(AsyncWebServerRequest* request) {
  setMotorEnabled(true);
  setMotorSpeed(0);
  broadcastMotorState();
  sendMotorStateResponse(request);
}

void WifiHotspot::handleMotorOff(AsyncWebServerRequest* request) {
  setMotorSpeed(0);
  setMotorEnabled(false);
  broadcastMotorState();
  sendMotorStateResponse(request);
}

void WifiHotspot::handleMotorStatus(AsyncWebServerRequest* request) {
  sendMotorStateResponse(request);
}

void WifiHotspot::handleWifiSignal(AsyncWebServerRequest* request) {
  sendWifiSignalResponse(request);
}

void WifiHotspot::handleMotorSpeed(AsyncWebServerRequest* request) {
  if (!request->hasParam("value")) {
    request->send(400, "application/json",
                  "{\"error\":\"Missing value parameter\"}");
    return;
  }

  int speed = request->getParam("value")->value().toInt();
  if (speed < -255 || speed > 255) {
    request->send(400, "application/json",
                  "{\"error\":\"Speed must be -255 to 255\"}");
    return;
  }

  setMotorSpeed(speed);
  jogStopAtMs_ = 0;
  broadcastMotorState();
  sendMotorStateResponse(request);
}

void WifiHotspot::handleMotorJog(AsyncWebServerRequest* request) {
  if (!request->hasParam("direction")) {
    request->send(400, "application/json",
                  "{\"error\":\"Missing direction parameter\"}");
    return;
  }

  String direction = request->getParam("direction")->value();
  direction.toLowerCase();

  int speed = 255;
  if (request->hasParam("speed")) {
    speed = request->getParam("speed")->value().toInt();
  }
  speed = constrain(speed, 0, 255);

  unsigned long durationMs = 1200;
  if (request->hasParam("ms")) {
    durationMs = request->getParam("ms")->value().toInt();
  }
  durationMs = constrain(durationMs, 100UL, 5000UL);

  if (direction == "cw") {
    setMotorSpeed(speed);
  } else if (direction == "ccw") {
    setMotorSpeed(-speed);
  } else {
    request->send(400, "application/json",
                  "{\"error\":\"Direction must be cw or ccw\"}");
    return;
  }

  jogStopAtMs_ = millis() + durationMs;
  broadcastMotorState();
  sendMotorStateResponse(request);
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

      if (strcmp(cmd, "speed") == 0 && doc.containsKey("value")) {
        int speed = doc["value"];
        setMotorSpeed(speed);
        jogStopAtMs_ = 0;
        broadcastMotorState();

        DynamicJsonDocument response(256);
        response["state"] = motorController_.isEnabled() ? "on" : "off";
        response["speed"] = motorController_.currentSpeed();

        String responseStr;
        serializeJson(response, responseStr);
        client->text(responseStr);

      } else if (strcmp(cmd, "on") == 0) {
        setMotorEnabled(true);
        setMotorSpeed(0);
        broadcastMotorState();

        DynamicJsonDocument response(256);
        response["state"] = "on";
        response["speed"] = motorController_.currentSpeed();

        String responseStr;
        serializeJson(response, responseStr);
        client->text(responseStr);

      } else if (strcmp(cmd, "off") == 0) {
        setMotorSpeed(0);
        setMotorEnabled(false);
        broadcastMotorState();

        DynamicJsonDocument response(256);
        response["state"] = "off";
        response["speed"] = motorController_.currentSpeed();

        String responseStr;
        serializeJson(response, responseStr);
        client->text(responseStr);

      } else if (strcmp(cmd, "status") == 0) {
        DynamicJsonDocument response(256);
        response["state"] = motorController_.isEnabled() ? "on" : "off";
        response["speed"] = motorController_.currentSpeed();

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

void WifiHotspot::begin(uint8_t rpwmPin, uint8_t lpwmPin, uint8_t renPin,
                        uint8_t lenPin) {
  g_activeHotspot = this;

  motorController_.begin(rpwmPin, lpwmPin, renPin, lenPin);

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

  server_.on("/motor/on", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleMotorOn(request);
  });

  server_.on("/motor/off", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleMotorOff(request);
  });

  server_.on("/motor/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleMotorStatus(request);
  });

  server_.on("/motor/speed", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleMotorSpeed(request);
  });

  server_.on("/motor/jog", HTTP_GET, [this](AsyncWebServerRequest* request) {
    this->handleMotorJog(request);
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
  if (jogStopAtMs_ != 0 && static_cast<long>(millis() - jogStopAtMs_) >= 0) {
    jogStopAtMs_ = 0;
    setMotorSpeed(0);
    broadcastMotorState();
  }
}

bool WifiHotspot::isMotorEnabled() const {
  return motorController_.isEnabled();
}
