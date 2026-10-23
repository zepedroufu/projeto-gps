#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"
#define HTTPC_DISABLE_FOLLOW_REDIRECTS 0
struct HTTPClient {
  void setFollowRedirects(int) {}
  void setTimeout(int) {}
  void collectHeaders(const char**, int) {}
  bool begin(WiFiClientSecure&, const char*) { return true; }
  bool begin(WiFiClientSecure&, const String&) { return true; }
  void addHeader(const char*, const char*) {}
  int POST(const String&) { return 200; }
  int GET() { return 200; }
  String header(const char*) { return String(""); }
  String getString() { return String("OK 0"); }
  void end() {}
};
