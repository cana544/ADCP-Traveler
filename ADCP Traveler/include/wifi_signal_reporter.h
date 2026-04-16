#ifndef WIFI_SIGNAL_REPORTER_H
#define WIFI_SIGNAL_REPORTER_H

#include <ESPAsyncWebServer.h>

namespace WifiSignalReporter {
void sendResponse(AsyncWebServerRequest* request);
}

#endif