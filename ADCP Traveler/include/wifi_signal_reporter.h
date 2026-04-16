#ifndef WIFI_SIGNAL_REPORTER_H
#define WIFI_SIGNAL_REPORTER_H

#include <WebServer.h>

namespace WifiSignalReporter {
void sendResponse(WebServer& server);
}

#endif