#pragma once
#include "Arduino.h"
#define WIFI_STA 1
#define WIFI_OFF 0
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
struct WiFiStub {
  void mode(int) {}
  void begin(const char*, const char*) {}
  int status() { extern bool g_wifi_ok; return g_wifi_ok ? WL_CONNECTED : WL_DISCONNECTED; }
  IPAddress localIP() { return IPAddress(); }
  void disconnect(bool = false) {}
};
extern WiFiStub WiFi;
