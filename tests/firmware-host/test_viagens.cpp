#include "stubs/Arduino.h"
#include "stubs/LittleFS.h"
#include "stubs/WiFi.h"
#include "stubs/WiFiClientSecure.h"
#include "stubs/HTTPClient.h"
#include "stubs/TinyGPS++.h"
#include "stubs/Preferences.h"

unsigned long g_millis = 0;
SerialStub Serial;
std::map<std::string, std::string> g_fs;
std::map<std::string, uint32_t> g_nvs;
LittleFSStub LittleFS;
WiFiStub WiFi;
std::vector<std::pair<unsigned long,int>> g_ledlog;
int g_led = -1;
bool g_wifi_ok = false;

#include "../../firmware/rastreador_gps/rastreador_gps.ino"

static std::string cks(const std::string& body) { unsigned c = 0; for (char ch : body) c ^= (unsigned char)ch; char b[8]; snprintf(b, sizeof b, "*%02X\r\n", c); return "$" + body + b; }
static std::string ddmm(double deg, bool lat) { double a = fabs(deg); int d = (int)a; double m = (a - d) * 60.0; char b[32]; snprintf(b, sizeof b, lat ? "%02d%08.5f" : "%03d%08.5f", d, m); return b; }
static void feed(int seg, double lat, double lon, double kmh) {
  int hh = 12 + seg / 3600, mm = (seg / 60) % 60, ss = seg % 60; char t[16]; snprintf(t, sizeof t, "%02d%02d%02d.00", hh, mm, ss);
  char g[200], r[200];
  snprintf(g, sizeof g, "GPGGA,%s,%s,S,%s,W,1,08,1.0,800.0,M,0.0,M,,", t, ddmm(lat, true).c_str(), ddmm(lon, false).c_str());
  snprintf(r, sizeof r, "GPRMC,%s,A,%s,S,%s,W,%.2f,90.0,201026,,,A", t, ddmm(lat, true).c_str(), ddmm(lon, false).c_str(), kmh / 1.852);
  gpsSerial.feed(cks(g)); gpsSerial.feed(cks(r));
}
static void dump(const char* titulo) {
  printf("== %s\n", titulo);
  for (auto& kv : g_fs) printf("--- %s\n%s", kv.first.c_str(), kv.second.c_str());
}

int main() {
  int seg = 0;
  auto passo = [&](double lat, double lon, double kmh) { feed(seg++, lat, lon, kmh); g_millis += 1000; loop(); };
  const double L0 = -18.9187, O0 = -48.2772;

  setup();                                    // VIAGEM 1
  for (int i = 0; i < 20; i++) passo(L0 + i * 0.0001, O0 + i * 0.00005, 40);   // 20 s andando (4 pontos... 2 s cada = 10 pontos)
  double LA = L0 + 19 * 0.0001, OA = O0 + 19 * 0.00005;
  for (int i = 0; i < 100; i++) passo(LA, OA, 0);                               // 100 s parado
  dump("APOS VIAGEM 1 (sem gravarLote manual, como se a energia caisse agora)");
  printf("buffer na RAM (perdido ao desligar): %d pontos\n", nBuffer);

  // "desliga e liga o carro": a RAM some, a flash e a NVS ficam
  nBuffer = 0; pontosNoArquivo = 0; temAncora = false; ultimoFixBom = 0; ultimoPontoParado = 0;
  setup();                                    // VIAGEM 2
  for (int i = 0; i < 20; i++) passo(L0 + 0.01 + i * 0.0001, O0 + 0.01, 30);
  gravarLote();
  dump("APOS VIAGEM 2");

  // x (apagar tudo) nao pode zerar a numeracao
  apagarTudo();
  nBuffer = 0; setup();                       // VIAGEM 3
  printf("depois de 'x' e novo boot, a viagem e: %u (esperado 3)\n", (unsigned)idViagem);
  return 0;
}
