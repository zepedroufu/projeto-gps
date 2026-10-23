#pragma once
#include "Arduino.h"
extern std::map<std::string, uint32_t> g_nvs;   // persiste entre "reinicios"
struct Preferences {
  std::string ns;
  bool begin(const char* n, bool) { ns = n; return true; }
  uint32_t getUInt(const char* k, uint32_t def = 0) { auto it = g_nvs.find(ns + "/" + k); return it == g_nvs.end() ? def : it->second; }
  size_t putUInt(const char* k, uint32_t v) { g_nvs[ns + "/" + k] = v; return 4; }
  void end() {}
};
