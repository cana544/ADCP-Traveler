#include "wifi_hotspot.h"

#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <cstdlib>

#include "config.h"
#include "wifi_signal_reporter.h"

WifiHotspot* g_activeHotspot = nullptr;

void onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                      AwsEventType type, void* arg, uint8_t* data, size_t len) {
  (void)server;
  (void)arg;
  if (g_activeHotspot) {
    g_activeHotspot->handleWebSocketEvent(client, type, data, len);
  }
}

WifiHotspot::WifiHotspot()
    : motorController_(),
      encoder_(),
      distanceController_(encoder_, motorController_),
      sectionController_(encoder_, motorController_, distanceController_),
      controlMutex_(xSemaphoreCreateRecursiveMutex()),
      server_(80),
      ws_("/ws"),
      pendingManualSpeed_(0),
      manualDirection_(0),
      manualReversalPending_(false),
      lastControlUs_(0),
      lastStateBroadcastMs_(0) {}

void WifiHotspot::setMotorEnabled(bool enabled) {
  ControlLock lock(controlMutex_);
  if (!enabled) {
    sectionController_.stop();
    if (distanceController_.isActive()) distanceController_.cancel();
    manualReversalPending_ = false;
    pendingManualSpeed_ = 0;
  }
  motorController_.setEnabled(enabled);
}

void WifiHotspot::setMotorSpeed(int speed) { applyManualSpeed(speed); }

void WifiHotspot::cancelDistanceForManualControl() {
  sectionController_.invalidate();
  if (distanceController_.isActive()) {
    distanceController_.cancel();
  }
}

void WifiHotspot::applyManualSpeed(int speed) {
  ControlLock lock(controlMutex_);
  if (!motorController_.isEnabled()) {
    manualReversalPending_ = false;
    pendingManualSpeed_ = 0;
    return;
  }
  cancelDistanceForManualControl();
  speed = constrain(speed, -255, 255);

  if (speed == 0) {
    manualReversalPending_ = false;
    pendingManualSpeed_ = 0;
    motorController_.stop();
    return;
  }

  const int requestedDirection = speed > 0 ? 1 : -1;

  if (manualDirection_ != 0 && requestedDirection != manualDirection_ &&
      !encoder_.isStopped()) {
    motorController_.stop();
    pendingManualSpeed_ = speed;
    manualReversalPending_ = true;
    return;
  }

  manualReversalPending_ = false;
  pendingManualSpeed_ = 0;
  manualDirection_ = requestedDirection;
  encoder_.setDirection(manualDirection_);
  motorController_.setSpeed(speed);
}

void WifiHotspot::updatePendingManualReversal() {
  if (!manualReversalPending_ || !encoder_.isStopped()) {
    return;
  }

  const int speed = pendingManualSpeed_;
  manualReversalPending_ = false;
  pendingManualSpeed_ = 0;

  if (speed == 0) {
    return;
  }

  manualDirection_ = speed > 0 ? 1 : -1;
  encoder_.setDirection(manualDirection_);
  motorController_.setSpeed(speed);
  broadcastMotorState();
}

bool WifiHotspot::startDistanceMove(float distanceCm, int direction) {
  ControlLock lock(controlMutex_);
  if (!isfinite(distanceCm) || distanceCm <= 0.0f ||
      (direction != 1 && direction != -1) || distanceController_.isActive() ||
      sectionController_.active()) {
    return false;
  }

  manualReversalPending_ = false;
  pendingManualSpeed_ = 0;
  motorController_.stop();

  if (!encoder_.isStopped(Config::Encoder::DISTANCE_STOP_TIMEOUT_US)) {
    return false;
  }

  manualDirection_ = direction;
  sectionController_.invalidate();
  encoder_.setDirection(direction);
  distanceController_.beginMove(distanceCm, direction);
  return distanceController_.isActive();
}

void WifiHotspot::stopDistanceMove() {
  ControlLock lock(controlMutex_);
  sectionController_.stop();
  distanceController_.cancel();
  manualReversalPending_ = false;
  pendingManualSpeed_ = 0;
}

bool WifiHotspot::zeroPosition() {
  ControlLock lock(controlMutex_);
  if (sectionController_.active() || distanceController_.isActive() ||
      motorController_.currentSpeed() != 0 || !encoder_.isStopped()) {
    return false;
  }
  encoder_.zero();
  sectionController_.invalidate();
  return true;
}

String WifiHotspot::makeStateJson() const {
  ControlLock lock(controlMutex_);
  DynamicJsonDocument response(4096);
  response["state"] = motorController_.isEnabled() ? "on" : "off";
  response["speed"] = motorController_.currentSpeed();
  response["positionCm"] = encoder_.positionCm();
  response["velocityCmS"] = encoder_.velocityCmS();
  response["distanceStatus"] = distanceController_.statusText();
  response["distanceActive"] = distanceController_.isActive();
  response["distanceCm"] = distanceController_.commandDistanceCm();
  JsonObject section = response.createNestedObject("section");
  section["stage"] = sectionController_.stageText();
  section["state"] = sectionController_.stateText();
  section["scanDirection"] = sectionController_.scanDirection() == 1 ? "cw" : "ccw";
  section["spanCm"] = sectionController_.spanCm();
  section["count"] = sectionController_.plan().count;
  section["maxCount"] = sectionController_.stage() == SectionController::Stage::SCANNING
      ? 0 : SectionPlan::maxValidCount(sectionController_.spanCm());
  section["completed"] = sectionController_.completedCount();
  section["nextSection"] = sectionController_.nextSection();
  section["lastCompletedSection"] = sectionController_.lastCompletedSection();
  section["remainingCm"] = sectionController_.remainingCm();
  section["locked"] = sectionController_.locked();
  section["stationary"] = sectionController_.stationary();
  section["error"] = sectionController_.error();
  JsonArray widths = section.createNestedArray("widthsCm");
  for (int i = 0; i < sectionController_.plan().count; ++i)
    widths.add(sectionController_.plan().widthsCm[i]);

  String responseStr;
  serializeJson(response, responseStr);
  return responseStr;
}

void WifiHotspot::sendMotorStateResponse(AsyncWebServerRequest* request) {
  request->send(200, "application/json", makeStateJson());
}

void WifiHotspot::sendDistanceStateResponse(AsyncWebServerRequest* request) {
  request->send(200, "application/json", makeStateJson());
}

void WifiHotspot::broadcastMotorState() { ws_.textAll(makeStateJson()); }

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

  String response = "{\"connected\":true,\"rssi\":" + String(rssi) +
                    ",\"quality\":" + String(quality) + ",\"label\":\"" +
                    label + "\"}";
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
    request->send(200, "text/html",
                  "<html><body><h1>Gauge Glide control page not found</h1>"
                  "<p>Upload SPIFFS data and refresh.</p></body></html>");
  }
}

void WifiHotspot::handleMotorOn(AsyncWebServerRequest* request) {
  ControlLock lock(controlMutex_);
  sectionController_.stop();
  if (distanceController_.isActive()) distanceController_.cancel();
  motorController_.setEnabled(true);
  motorController_.stop();
  broadcastMotorState();
  sendMotorStateResponse(request);
}

void WifiHotspot::handleMotorOff(AsyncWebServerRequest* request) {
  ControlLock lock(controlMutex_);
  stopDistanceMove();
  motorController_.stop();
  motorController_.setEnabled(false);
  broadcastMotorState();
  sendMotorStateResponse(request);
}

void WifiHotspot::handleMotorStatus(AsyncWebServerRequest* request) {
  sendMotorStateResponse(request);
}

void WifiHotspot::handleMotorSpeed(AsyncWebServerRequest* request) {
  ControlLock lock(controlMutex_);
  if (!motorController_.isEnabled()) {
    request->send(409, "application/json",
                  "{\"error\":\"Enable Traveller First\"}");
    return;
  }
  if (!request->hasParam("value")) {
    request->send(400, "application/json",
                  "{\"error\":\"Missing value parameter\"}");
    return;
  }

  const int speed = request->getParam("value")->value().toInt();
  if (speed < -255 || speed > 255) {
    request->send(400, "application/json",
                  "{\"error\":\"Speed must be -255 to 255\"}");
    return;
  }

  setMotorSpeed(speed);
  broadcastMotorState();
  sendMotorStateResponse(request);
}

void WifiHotspot::handleDistanceStart(AsyncWebServerRequest* request) {
  ControlLock lock(controlMutex_);
  if (!request->hasParam("distance") || !request->hasParam("direction")) {
    request->send(400, "application/json",
                  "{\"error\":\"Missing distance or direction\"}");
    return;
  }

  const float distanceCm = request->getParam("distance")->value().toFloat();
  const String directionText = request->getParam("direction")->value();
  const int direction = directionText == "cw"    ? 1
                        : directionText == "ccw" ? -1
                                                 : 0;

  if (!startDistanceMove(distanceCm, direction)) {
    request->send(409, "application/json",
                  "{\"error\":\"Invalid move or traveller still moving\"}");
    return;
  }

  broadcastMotorState();
  sendDistanceStateResponse(request);
}

void WifiHotspot::handleDistanceStop(AsyncWebServerRequest* request) {
  ControlLock lock(controlMutex_);
  stopDistanceMove();
  broadcastMotorState();
  sendDistanceStateResponse(request);
}

void WifiHotspot::handleDistanceZero(AsyncWebServerRequest* request) {
  ControlLock lock(controlMutex_);
  if (!zeroPosition()) {
    request->send(409, "application/json",
                  "{\"error\":\"Cannot zero while traveller is moving\"}");
    return;
  }
  broadcastMotorState();
  sendDistanceStateResponse(request);
}

void WifiHotspot::handleDistanceStatus(AsyncWebServerRequest* request) {
  sendDistanceStateResponse(request);
}

bool WifiHotspot::executeSectionCommand(const char* command, int direction, double count) {
  bool accepted = false;
  if (strcmp(command, "section_scan") == 0) {
    accepted = sectionController_.startScan(direction);
    if (accepted) {
      manualReversalPending_ = false;
      pendingManualSpeed_ = 0;
      manualDirection_ = direction;
    }
  } else if (strcmp(command, "section_stop") == 0) {
    sectionController_.stop();
    accepted = true;
  } else if (strcmp(command, "section_configure") == 0) {
    accepted = sectionController_.configure(count);
  } else if (strcmp(command, "section_go") == 0) {
    accepted = sectionController_.go();
    if (accepted) {
      manualReversalPending_ = false;
      pendingManualSpeed_ = 0;
      manualDirection_ = -sectionController_.scanDirection();
    }
  } else if (strcmp(command, "section_new_scan") == 0) {
    accepted = sectionController_.newScan();
  } else if (strcmp(command, "section_status") == 0) {
    accepted = true;
  }
  return accepted;
}

void WifiHotspot::handleSectionCommand(AsyncWebServerRequest* request, const char* command) {
  ControlLock lock(controlMutex_);
  int direction = 0;
  if (request->hasParam("direction")) {
    const String value = request->getParam("direction")->value();
    direction = value == "cw" ? 1 : value == "ccw" ? -1 : 0;
  }
  double count = NAN;
  if (request->hasParam("count")) {
    const String value = request->getParam("count")->value();
    char* end = nullptr;
    count = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') count = NAN;
  }
  if (!executeSectionCommand(command, direction, count)) {
    DynamicJsonDocument failure(256);
    failure["sectionError"] = sectionController_.error();
    String payload; serializeJson(failure, payload);
    request->send(409, "application/json", payload);
  } else {
    request->send(200, "application/json", makeStateJson());
  }
  broadcastMotorState();
}

void WifiHotspot::handleWifiSignal(AsyncWebServerRequest* request) {
  sendWifiSignalResponse(request);
}

void WifiHotspot::handleNotFound(AsyncWebServerRequest* request) {
  request->send(404, "text/plain", "Not found");
}

void WifiHotspot::handleWebSocketEvent(AsyncWebSocketClient* client,
                                       AwsEventType type, uint8_t* data,
                                       size_t len) {
  ControlLock lock(controlMutex_);
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client %u connected\n", client->id());
      client->text(makeStateJson());
      break;

    case WS_EVT_DATA: {
      DynamicJsonDocument doc(384);
      const DeserializationError error = deserializeJson(doc, data, len);
      if (error) {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
        return;
      }

      const char* cmd = doc["cmd"];
      if (!cmd) {
        return;
      }

      if (strncmp(cmd, "section_", 8) == 0) {
        const char* directionText = doc["direction"].is<const char*>()
            ? doc["direction"].as<const char*>() : "";
        const int direction = strcmp(directionText, "cw") == 0 ? 1
            : strcmp(directionText, "ccw") == 0 ? -1 : 0;
        const double count = doc["count"].is<double>() ? doc["count"].as<double>() : NAN;
        if (!executeSectionCommand(cmd, direction, count)) {
          DynamicJsonDocument failure(256);
          failure["sectionError"] = sectionController_.error();
          String payload; serializeJson(failure, payload); client->text(payload);
        }
        client->text(makeStateJson());
        broadcastMotorState();
        return;
      }

      if (!motorController_.isEnabled() &&
          strcmp(cmd, "speed") == 0) {
        client->text("{\"error\":\"Enable Traveller First\"}");
        client->text(makeStateJson());
        return;
      }

      if (strcmp(cmd, "speed") == 0 && doc.containsKey("value")) {
        setMotorSpeed(doc["value"].as<int>());
      } else if (strcmp(cmd, "on") == 0) {
        sectionController_.stop();
        if (distanceController_.isActive()) distanceController_.cancel();
        motorController_.setEnabled(true);
        motorController_.stop();
      } else if (strcmp(cmd, "off") == 0) {
        stopDistanceMove();
        motorController_.stop();
        motorController_.setEnabled(false);
      } else if (strcmp(cmd, "distance_start") == 0 &&
                 doc.containsKey("distanceCm") &&
                 doc.containsKey("direction")) {
        const char* directionText = doc["direction"].is<const char*>()
            ? doc["direction"].as<const char*>() : "";
        const int direction = strcmp(directionText, "cw") == 0    ? 1
                              : strcmp(directionText, "ccw") == 0 ? -1
                                                                  : 0;
        startDistanceMove(doc["distanceCm"].as<float>(), direction);
      } else if (strcmp(cmd, "distance_stop") == 0) {
        stopDistanceMove();
      } else if (strcmp(cmd, "distance_zero") == 0) {
        zeroPosition();
      } else if (strcmp(cmd, "status") != 0 &&
                 strcmp(cmd, "distance_status") != 0) {
        return;
      }

      client->text(makeStateJson());
      broadcastMotorState();
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
  if (!controlMutex_) { Serial.println("Unable to allocate control mutex"); return; }
  g_activeHotspot = this;

  motorController_.begin(rpwmPin, lpwmPin, renPin, lenPin);
  encoder_.begin(Config::Pins::ENCODER);

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

  server_.on("/", HTTP_GET,
             [this](AsyncWebServerRequest* request) { handleRoot(request); });
  server_.on("/index.html", HTTP_GET,
             [this](AsyncWebServerRequest* request) { handleRoot(request); });
  server_.on("/style.css", HTTP_GET, [this](AsyncWebServerRequest* request) {
    serveFile(request, "/style.css", "text/css");
  });
  server_.on("/script.js", HTTP_GET, [this](AsyncWebServerRequest* request) {
    serveFile(request, "/script.js", "application/javascript");
  });
  server_.on("/section-control.js", HTTP_GET, [this](AsyncWebServerRequest* request) {
    serveFile(request, "/section-control.js", "application/javascript");
  });
  server_.on("/uoa-logo-white.png", HTTP_GET,
             [this](AsyncWebServerRequest* request) {
               serveFile(request, "/uoa-logo-white.png", "image/png");
             });

  server_.on("/motor/on", HTTP_GET, [this](AsyncWebServerRequest* request) {
    handleMotorOn(request);
  });
  server_.on("/motor/off", HTTP_GET, [this](AsyncWebServerRequest* request) {
    handleMotorOff(request);
  });
  server_.on("/motor/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    handleMotorStatus(request);
  });
  server_.on("/motor/speed", HTTP_GET, [this](AsyncWebServerRequest* request) {
    handleMotorSpeed(request);
  });

  server_.on(
      "/distance/start", HTTP_GET,
      [this](AsyncWebServerRequest* request) { handleDistanceStart(request); });
  server_.on(
      "/distance/stop", HTTP_GET,
      [this](AsyncWebServerRequest* request) { handleDistanceStop(request); });
  server_.on(
      "/distance/zero", HTTP_GET,
      [this](AsyncWebServerRequest* request) { handleDistanceZero(request); });
  server_.on("/distance/status", HTTP_GET,
             [this](AsyncWebServerRequest* request) {
               handleDistanceStatus(request);
             });

  server_.on("/wifi/signal", HTTP_GET, [this](AsyncWebServerRequest* request) {
    handleWifiSignal(request);
  });
  const char* sectionActions[] = {"scan", "stop", "configure", "go", "new_scan", "status"};
  for (const char* action : sectionActions) {
    const String command = String("section_") + action;
    server_.on((String("/section/") + action).c_str(), HTTP_GET,
        [this, command](AsyncWebServerRequest* request) {
          handleSectionCommand(request, command.c_str());
        });
  }
  server_.onNotFound(
      [this](AsyncWebServerRequest* request) { handleNotFound(request); });

  ws_.onEvent(onWebSocketEvent);
  server_.addHandler(&ws_);
  server_.begin();

  lastControlUs_ = micros();
  Serial.println("HTTP server started on port 80");
  Serial.println("WebSocket server started at /ws");
}

void WifiHotspot::update() {
  ControlLock lock(controlMutex_);
  const uint32_t nowUs = micros();
  if ((uint32_t)(nowUs - lastControlUs_) >=
      Config::Control::CONTROL_PERIOD_US) {
    lastControlUs_ += Config::Control::CONTROL_PERIOD_US;
    encoder_.update();
    distanceController_.update();
    sectionController_.update();
    updatePendingManualReversal();
  }

  const uint32_t nowMs = millis();
  if ((uint32_t)(nowMs - lastStateBroadcastMs_) >= 200UL) {
    lastStateBroadcastMs_ = nowMs;
    if (ws_.count() > 0) {
      broadcastMotorState();
    }
  }

  ws_.cleanupClients();
}

bool WifiHotspot::isMotorEnabled() const {
  ControlLock lock(controlMutex_);
  return motorController_.isEnabled();
}
