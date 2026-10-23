#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>
#include <string>
#include <vector>
#include <map>
#ifndef PI
#define PI 3.14159265358979323846
#endif
#define TWO_PI (2 * PI)
#define radians(d) ((d) * PI / 180.0)
#define degrees(r) ((r) * 180.0 / PI)
#define sq(x) ((x) * (x))
extern unsigned long g_millis;
inline unsigned long millis() { return g_millis; }
inline void delay(unsigned long ms) { g_millis += ms; }
typedef uint8_t byte;
#define LED_BUILTIN 2
#define OUTPUT 1
#define HIGH 1
#define LOW 0
extern std::vector<std::pair<unsigned long,int>> g_ledlog;
extern int g_led;
inline void pinMode(int, int) {}
inline void digitalWrite(int, int v) { v = v ? 1 : 0; if (v != g_led) { g_led = v; g_ledlog.push_back({g_millis, v}); } }
#define SERIAL_8N1 0
#define FILE_APPEND "a"
#define FILE_READ "r"

class String {
  std::string s;
 public:
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& x) : s(x) {}
  size_t length() const { return s.size(); }
  const char* c_str() const { return s.c_str(); }
  bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }
  String substring(size_t a, size_t b) const { return String(s.substr(a, b > a ? b - a : 0)); }
  String operator+(const String& o) const { return String(s + o.s); }
  String operator+(const char* o) const { return String(s + o); }
  const std::string& str() const { return s; }
};

struct IPAddress {};

struct SerialStub {
  void begin(long) {}
  int available() { return 0; }
  int read() { return -1; }
  void write(uint8_t c) { putchar(c); }
  void print(const char* s) { fputs(s, stdout); }
  void println(const char* s = "") { puts(s); }
  void println(const IPAddress&) { puts("<ip>"); }
  void print(const IPAddress&) { fputs("<ip>", stdout); }
  void printf(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }
};
extern SerialStub Serial;

struct HardwareSerial {
  std::string q;
  size_t pos = 0;
  HardwareSerial(int) {}
  void setRxBufferSize(int) {}
  void begin(long, int, int, int) {}
  int available() { return (int)(q.size() - pos); }
  int read() { return pos < q.size() ? (unsigned char)q[pos++] : -1; }
  void feed(const std::string& s) { q += s; }
};

extern std::map<std::string, std::string> g_fs;

class File {
 public:
  std::string nm;
  bool ok = false, isDir = false, writing = false;
  size_t pos = 0;
  std::vector<std::string> list;
  size_t idx = 0;
  operator bool() const { return ok; }
  const char* name() const { return nm.c_str(); }
  size_t size() const { return g_fs[nm].size(); }
  int available() { return (int)(g_fs[nm].size() - pos); }
  int read() { return pos < g_fs[nm].size() ? (unsigned char)g_fs[nm][pos++] : -1; }
  void printf(const char* fmt, ...) { char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); g_fs[nm] += b; }
  void println(const char* s) { g_fs[nm] += s; g_fs[nm] += "\n"; }
  void close() { ok = false; }
  String readString() { return String(g_fs[nm]); }
  File openNextFile() {
    File f;
    if (idx < list.size()) { f.ok = true; f.nm = list[idx++]; }
    return f;
  }
};

struct LittleFSStub {
  bool begin(bool) { return true; }
  File open(const char* p, const char* mode = "r") {
    File f;
    std::string path = p;
    if (path == "/") {
      f.ok = true; f.isDir = true;
      for (auto& kv : g_fs) f.list.push_back(kv.first);
      return f;
    }
    std::string k = path[0] == '/' ? path.substr(1) : path;
    if (std::string(mode) == "r" && !g_fs.count(k)) return f;
    if (!g_fs.count(k)) g_fs[k] = "";
    f.ok = true; f.nm = k;
    return f;
  }
  File open(const String& p, const char* mode = "r") { return open(p.c_str(), mode); }
  bool exists(const String& p) { std::string s = p.c_str(); return g_fs.count(s[0] == '/' ? s.substr(1) : s) > 0; }
  bool remove(const String& p) { std::string s = p.c_str(); return g_fs.erase(s[0] == '/' ? s.substr(1) : s) > 0; }
  size_t usedBytes() { size_t n = 0; for (auto& kv : g_fs) n += kv.second.size(); return n; }
  size_t totalBytes() { return 1441792; }
};
extern LittleFSStub LittleFS;
