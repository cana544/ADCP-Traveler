  // Baseline-only observation/STOP. No changes to the original control law.
  static DiagnosticCommands baselineParser;
  static bool baselineLogging = false, baselineWasActive = false;
  static uint32_t baselineMove = 0, baselineLastLog = 0, baselineDrops = 0, baselineSequence = 0;
  const bool baselineActive = distanceController_.isActive();
  if (baselineActive && !baselineWasActive) ++baselineMove;
  baselineWasActive = baselineActive;
  for (int budget = 0; budget < 32 && Serial.available() > 0; ++budget) {
    const auto command = baselineParser.feed(static_cast<char>(Serial.read()));
    if (command == DiagnosticCommands::STOP) stopDistanceMove();
    else if (command == DiagnosticCommands::LOG_ON) baselineLogging = true;
    else if (command == DiagnosticCommands::LOG_OFF) baselineLogging = false;
  }
  const uint32_t baselineNow = micros();
  if (baselineLogging && baselineNow - baselineLastLog >= 100000UL) {
    baselineLastLog = baselineNow;
    static StaticJsonDocument<1536> baselineJson;
    static char baselineLine[1536];
    baselineJson.clear();
    baselineJson["version"] = 1; baselineJson["baseline"] = true;
    baselineJson["boot"] = bootId_; baselineJson["us"] = baselineNow;
    baselineJson["seq"] = ++baselineSequence; baselineJson["move"] = baselineMove;
    baselineJson["phase"] = distanceController_.statusText();
    baselineJson["position_cm"] = encoder_.positionCm();
    baselineJson["target_cm"] = distanceController_.diagnosticTargetCm();
    baselineJson["start_cm"] = distanceController_.moveStartPositionCm();
    baselineJson["velocity_cm_s"] = encoder_.velocityCmS();
    baselineJson["applied_v"] = motorController_.currentSpeed() * 12.0f / 255;
    baselineJson["pwm"] = motorController_.currentSpeed();
    baselineJson["drops"] = baselineDrops;
    const size_t length = measureJson(baselineJson);
    if (baselineJson.overflowed() || length + 5 > sizeof(baselineLine) || Serial.availableForWrite() < int(length + 4)) ++baselineDrops;
    else {
      memcpy(baselineLine, "GG,", 3);
      const size_t n = serializeJson(baselineJson, baselineLine + 3, sizeof(baselineLine) - 4);
      baselineLine[n+3] = '\n';
      Serial.write(reinterpret_cast<const uint8_t*>(baselineLine), n+4);
    }
  }
