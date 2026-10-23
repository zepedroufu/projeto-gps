/*
  Rastreador GPS veicular com ESP32 + NEO-M8N
  --------------------------------------------
  - Le o GPS (UART2: TXD do GPS no RX2/GPIO16, RXD do GPS no TX2/GPIO17)
  - Grava o rastro em arquivos CSV na flash (LittleFS), em lotes
  - De tempos em tempos tenta o Wi-Fi (Acesso Pessoal do iPhone, 2,4 GHz)
  - Se conectar, envia cada arquivo para a planilha (Google Apps Script)
    e so apaga o arquivo depois que o servidor responde "OK"

  Biblioteca necessaria (Gerenciador de Bibliotecas): TinyGPSPlus (Mikal Hart)
  Placa: ESP32 Dev Module

  Comandos no Monitor Serial (115200 baud):
    l = listar arquivos | m = mostrar conteudo | x = apagar tudo | w = enviar agora
*/

#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <TinyGPSPlus.h>
#include <Preferences.h>

// ================= CONFIGURACOES =================
#define USAR_GPS_SIMULADO 0   // 1 = pontos simulados | 0 = GPS real

const char* WIFI_SSID  = "NOME_DA_REDE";          // nome do Acesso Pessoal do iPhone (ou rede 2,4 GHz)
const char* WIFI_SENHA = "SENHA_DA_REDE";         // senha do Wi-Fi
// URL do app da Web (Apps Script), termina em /exec
const char* URL_SCRIPT = "https://script.google.com/macros/s/SEU_ID_DE_IMPLANTACAO/exec";  // URL do app da Web (Apps Script)

const int PONTOS_POR_LOTE = 10;                     // grava na flash a cada 10 pontos
const int PONTOS_POR_ARQUIVO = 100;                 // troca de arquivo a cada 100 pontos
const unsigned long INTERVALO_PONTO_MS = 2000;      // no carro: 5000
const unsigned long INTERVALO_WIFI_MS = 60000UL;    // no carro: 300000 (5 min)
const unsigned long TIMEOUT_WIFI_MS = 15000UL;
const int MAX_ARQUIVOS_POR_ENVIO = 30;

// Filtros de qualidade (so valem com GPS real)
const int MIN_SATELITES = 5;                        // abaixo disso o ponto e descartado
const double MAX_HDOP = 3.0;                        // quanto MENOR, melhor a precisao (ate 3 e aceitavel)
const float VEL_PARADO_KMH = 3.0;                   // abaixo disso o carro e considerado parado
const double RAIO_PARADO_M = 50.0;                  // parado: ignora deslocamentos menores que isso
const unsigned long INTERVALO_PARADO_MS = 30000UL;  // parado: grava 1 ponto a cada 30 s
// =================================================

// LED azul da propria ESP32 (GPIO2) mostra o estado do sistema:
//   pisca rapido       = esperando sinal de GPS de qualidade
//   aceso fixo         = com fix, gravando o rastro
//   3 piscadas rapidas = arquivo enviado para a planilha (se estiver aceso, apaga 3 vezes)
#ifdef LED_BUILTIN
const int PINO_LED = LED_BUILTIN;
#else
const int PINO_LED = 2;
#endif

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

struct Ponto {
  char dataHora[20];
  double lat, lon;
  float vel;
  int sats;
  uint32_t viagem;
};

// Numero da viagem: aumenta 1 a cada vez que a ESP32 liga (guardado na memoria NVS,
// que nao e apagada pelo comando x nem ao carregar um codigo novo)
uint32_t idViagem = 0;

Ponto buffer[PONTOS_POR_LOTE];
int nBuffer = 0;
int arquivoAtual = 1;
int pontosNoArquivo = 0;
int contadorSimulado = 0;

// Posicao "ancorada": enquanto o carro esta parado, grava-se sempre ela
// em vez da leitura ruidosa do GPS (elimina o deslize com o carro parado)
bool temAncora = false;
double ancoraLat = 0, ancoraLon = 0;
unsigned long ultimoPontoParado = 0;

// Instante do ultimo ponto com qualidade OK (0 = ainda nao houve)
unsigned long ultimoFixBom = 0;

bool fixRecente() {
  return ultimoFixBom != 0 && millis() - ultimoFixBom < 15000UL;
}

void atualizarLed() {
  if (fixRecente()) digitalWrite(PINO_LED, HIGH);              // aceso fixo
  else digitalWrite(PINO_LED, (millis() / 200) % 2);           // pisca rapido
}

void piscarEnvioOk() {
  bool base = fixRecente();  // aceso (com fix) ou apagado (sem fix)
  for (int i = 0; i < 3; i++) {
    digitalWrite(PINO_LED, base ? LOW : HIGH);
    delay(150);
    digitalWrite(PINO_LED, base ? HIGH : LOW);
    delay(150);
  }
}

enum EstadoWifi { WIFI_OCIOSO, WIFI_CONECTANDO };
EstadoWifi estadoWifi = WIFI_OCIOSO;
unsigned long ultimaTentativaWifi = 0;
unsigned long inicioTentativaWifi = 0;

// ---------- utilidades de arquivo ----------
String caminho(const char* nome) {
  return nome[0] == '/' ? String(nome) : String("/") + nome;
}

String nomeArquivo(int idx) {
  char nome[24];
  snprintf(nome, sizeof(nome), "/rastro_%04d.csv", idx);
  return String(nome);
}

int descobrirProximoIndice() {
  int maior = 0;
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f) {
    const char* p = strstr(f.name(), "rastro_");
    if (p) {
      int n = atoi(p + 7);
      if (n > maior) maior = n;
    }
    f = root.openNextFile();
  }
  return maior + 1;
}

void gravarLote() {
  if (nBuffer == 0) return;
  String nome = nomeArquivo(arquivoAtual);
  bool novo = !LittleFS.exists(nome);

  File f = LittleFS.open(nome, FILE_APPEND);
  if (!f) {
    Serial.println("ERRO: nao consegui abrir o arquivo");
    return;
  }
  if (novo) f.println("data_hora,lat,lon,vel_kmh,satelites,viagem");
  for (int i = 0; i < nBuffer; i++) {
    f.printf("%s,%.6f,%.6f,%.1f,%d,%u\n", buffer[i].dataHora, buffer[i].lat,
             buffer[i].lon, buffer[i].vel, buffer[i].sats, (unsigned)buffer[i].viagem);
  }
  f.close();

  Serial.printf("Gravados %d pontos em %s\n", nBuffer, nome.c_str());
  pontosNoArquivo += nBuffer;
  nBuffer = 0;

  if (pontosNoArquivo >= PONTOS_POR_ARQUIVO) {
    arquivoAtual++;
    pontosNoArquivo = 0;
  }
}

void registrarPonto(const char* dh, double lat, double lon, float vel, int sats) {
  Ponto& p = buffer[nBuffer++];
  strncpy(p.dataHora, dh, sizeof(p.dataHora) - 1);
  p.dataHora[sizeof(p.dataHora) - 1] = '\0';
  p.lat = lat;
  p.lon = lon;
  p.vel = vel;
  p.sats = sats;
  p.viagem = idViagem;
  if (nBuffer >= PONTOS_POR_LOTE) gravarLote();
}

// ---------- horario: o GPS entrega UTC; a planilha guarda horario de Brasilia (UTC-3) ----------
int diasNoMes(int ano, int mes) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (mes == 2 && ((ano % 4 == 0 && ano % 100 != 0) || ano % 400 == 0)) return 29;
  return d[mes - 1];
}

// Subtrai 3 horas, ajustando dia, mes e ano (virada de dia, mes, ano e ano bissexto).
// O Brasil nao tem horario de verao desde 2019, entao o fuso e fixo.
void utcParaBrasilia(int& ano, int& mes, int& dia, int& hora) {
  hora -= 3;
  if (hora < 0) {
    hora += 24;
    dia--;
    if (dia < 1) {
      mes--;
      if (mes < 1) {
        mes = 12;
        ano--;
      }
      dia = diasNoMes(ano, mes);
    }
  }
}

// ---------- coleta de pontos (simulado ou GPS) ----------
void coletarPonto() {
#if USAR_GPS_SIMULADO
  ultimoFixBom = millis();
  char dh[20];
  int seg = contadorSimulado;
  snprintf(dh, sizeof(dh), "2026-10-20 00:%02d:%02d", (seg / 60) % 60, seg % 60);
  registrarPonto(dh, -18.918700 + contadorSimulado * 0.0001,
                 -48.277200 + contadorSimulado * 0.0001, 30.0 + (contadorSimulado % 10), 9);
  contadorSimulado++;
#else
  bool fixOk = gps.location.isValid() && gps.location.age() < 2000 &&
               gps.date.isValid() && gps.time.isValid();
  int sats = gps.satellites.isValid() ? (int)gps.satellites.value() : 0;
  double hdop = gps.hdop.isValid() ? gps.hdop.hdop() : 99.9;

  // 1) Qualidade: sem fix, poucos satelites ou precisao ruim -> nao grava
  if (!fixOk || sats < MIN_SATELITES || hdop > MAX_HDOP) {
    Serial.printf("Aguardando fix de qualidade... fix=%s sats=%d hdop=%.1f (precisa: sats>=%d, hdop<=%.1f)\n",
                  fixOk ? "sim" : "nao", sats, hdop, MIN_SATELITES, MAX_HDOP);
    return;
  }
  ultimoFixBom = millis();  // sinal de qualidade OK (LED fica aceso)

  double lat = gps.location.lat(), lon = gps.location.lng();
  float vel = gps.speed.kmph();
  char dh[32];
  int ano = gps.date.year(), mes = gps.date.month(), dia = gps.date.day();
  int hora = gps.time.hour();
  utcParaBrasilia(ano, mes, dia, hora);  // UTC (GPS) -> horario de Brasilia
  snprintf(dh, sizeof(dh), "%04d-%02d-%02d %02d:%02d:%02d",
           ano, mes, dia, hora, gps.time.minute(), gps.time.second());

  // 2) Parado: usa a posicao ancorada e grava devagar
  if (!temAncora) {
    ancoraLat = lat;
    ancoraLon = lon;
    temAncora = true;
  }
  double dist = TinyGPSPlus::distanceBetween(lat, lon, ancoraLat, ancoraLon);
  bool parado = (vel < VEL_PARADO_KMH && dist < RAIO_PARADO_M);
  if (parado) {
    // refina a ancora devagar (media movel): a posicao parada converge para o centro do "ruido"
    ancoraLat += (lat - ancoraLat) * 0.05;
    ancoraLon += (lon - ancoraLon) * 0.05;
    if (millis() - ultimoPontoParado < INTERVALO_PARADO_MS) return;
    ultimoPontoParado = millis();
    lat = ancoraLat;
    lon = ancoraLon;
    vel = 0;
  } else {
    ancoraLat = lat;  // em movimento (ou saiu do raio): a ancora acompanha
    ancoraLon = lon;
  }

  registrarPonto(dh, lat, lon, vel, sats);
  // Parado: grava na flash na hora. Quando o carro desliga, a energia cai de repente;
  // assim o ultimo ponto (a chegada da viagem) nao fica perdido na RAM.
  if (parado) gravarLote();
#endif
}

// ---------- envio para a planilha ----------
bool enviarArquivo(const String& nome) {
  File f = LittleFS.open(nome, FILE_READ);
  if (!f) return false;
  String conteudo = f.readString();
  f.close();

  WiFiClientSecure client;
  client.setInsecure();  // sem validar certificado (simples; ver observacao no tutorial)
  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  const char* cabecalhos[] = {"Location"};
  http.collectHeaders(cabecalhos, 1);

  if (!http.begin(client, URL_SCRIPT)) return false;
  http.addHeader("Content-Type", "text/plain");
  int codigo = http.POST(conteudo);
  String corpo = "";

  if (codigo == 301 || codigo == 302 || codigo == 303) {
    // O Apps Script processa o POST e responde com um redirecionamento;
    // a confirmacao ("OK n") vem seguindo esse endereco com GET.
    String destino = http.header("Location");
    http.end();
    if (destino.length() == 0) return false;

    WiFiClientSecure client2;
    client2.setInsecure();
    HTTPClient http2;
    http2.setTimeout(15000);
    if (!http2.begin(client2, destino)) return false;
    codigo = http2.GET();
    if (codigo == 200) corpo = http2.getString();
    http2.end();
  } else {
    if (codigo == 200) corpo = http.getString();
    http.end();
  }

  bool ok = (codigo == 200 && corpo.startsWith("OK"));
  Serial.printf("Envio %s -> HTTP %d | resposta: %s\n", nome.c_str(), codigo,
                corpo.substring(0, 40).c_str());
  return ok;
}

void enviarPendentes() {
  // Fecha o arquivo atual para incluir os pontos mais recentes
  gravarLote();
  if (pontosNoArquivo > 0) {
    arquivoAtual++;
    pontosNoArquivo = 0;
  }

  // Lista os arquivos antes de enviar (nao mexe no diretorio durante a leitura)
  String nomes[MAX_ARQUIVOS_POR_ENVIO];
  int n = 0;
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f && n < MAX_ARQUIVOS_POR_ENVIO) {
    if (strstr(f.name(), "rastro_")) nomes[n++] = caminho(f.name());
    f = root.openNextFile();
  }
  root.close();

  if (n == 0) {
    Serial.println("Nada para enviar.");
    return;
  }

  bool algumEnviado = false;
  for (int i = 0; i < n; i++) {
    if (enviarArquivo(nomes[i])) {
      LittleFS.remove(nomes[i]);  // so apaga depois da confirmacao "OK"
      Serial.printf("Enviado e apagado: %s\n", nomes[i].c_str());
      algumEnviado = true;
    } else {
      Serial.printf("Falha no envio, mantido: %s\n", nomes[i].c_str());
      break;  // tenta de novo na proxima janela de Wi-Fi
    }
  }
  if (algumEnviado) piscarEnvioOk();  // 3 piscadas rapidas no LED azul
}

// ---------- Wi-Fi sem travar o resto do programa ----------
void gerenciarWifi() {
  unsigned long agora = millis();

  if (estadoWifi == WIFI_OCIOSO) {
    if (agora - ultimaTentativaWifi >= INTERVALO_WIFI_MS) {
      ultimaTentativaWifi = agora;
      inicioTentativaWifi = agora;
      Serial.println("Tentando conectar no Wi-Fi...");
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_SENHA);
      estadoWifi = WIFI_CONECTANDO;
    }
  } else {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Wi-Fi conectado. IP: ");
      Serial.println(WiFi.localIP());
      enviarPendentes();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      estadoWifi = WIFI_OCIOSO;
    } else if (agora - inicioTentativaWifi > TIMEOUT_WIFI_MS) {
      Serial.println("Wi-Fi nao encontrado, seguindo gravando.");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      estadoWifi = WIFI_OCIOSO;
    }
  }
}

// ---------- comandos pelo Monitor Serial ----------
void listarArquivos() {
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f) {
    Serial.printf("%s  (%u bytes)\n", f.name(), (unsigned)f.size());
    f = root.openNextFile();
  }
  Serial.printf("Usado: %u / %u bytes\n", (unsigned)LittleFS.usedBytes(),
                (unsigned)LittleFS.totalBytes());
}

void mostrarConteudo() {
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f) {
    Serial.printf("--- %s ---\n", f.name());
    while (f.available()) Serial.write(f.read());
    f = root.openNextFile();
  }
}

void apagarTudo() {
  for (int i = 0; i < 1000; i++) {
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    if (!f) break;
    String p = caminho(f.name());
    f.close();
    root.close();
    LittleFS.remove(p);
  }
  nBuffer = 0;
  pontosNoArquivo = 0;
  arquivoAtual = 1;
  Serial.println("Todos os arquivos apagados.");
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_LED, OUTPUT);
  digitalWrite(PINO_LED, LOW);
  delay(500);

  gpsSerial.setRxBufferSize(2048);                // folga enquanto o envio ocupa o processador
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);       // RX2 = 16, TX2 = 17

  if (!LittleFS.begin(true)) {
    Serial.println("ERRO ao iniciar o LittleFS");
    while (true) delay(1000);
  }
  arquivoAtual = descobrirProximoIndice();
  ultimaTentativaWifi = millis();

  // Nova viagem a cada vez que a ESP32 liga
  Preferences prefs;
  prefs.begin("rastreador", false);
  idViagem = prefs.getUInt("viagem", 0) + 1;
  prefs.putUInt("viagem", idViagem);
  prefs.end();

  Serial.printf("Pronto. Modo: %s. Viagem n. %u. Proximo arquivo: %s\n",
                USAR_GPS_SIMULADO ? "SIMULADO" : "GPS REAL",
                (unsigned)idViagem, nomeArquivo(arquivoAtual).c_str());
  Serial.println("Comandos: l = listar | m = conteudo | x = apagar tudo | w = enviar agora");
}

void loop() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  static unsigned long ultimoPonto = 0;
  if (millis() - ultimoPonto >= INTERVALO_PONTO_MS) {
    ultimoPonto = millis();
    coletarPonto();
  }

  gerenciarWifi();
  atualizarLed();

#if !USAR_GPS_SIMULADO
  // Aviso se o GPS parar de mandar dados (fiacao solta, alimentacao, baud rate)
  static unsigned long ultimoCheckGps = 0;
  static uint32_t ultimosChars = 0;
  if (millis() - ultimoCheckGps >= 10000) {
    ultimoCheckGps = millis();
    if (gps.charsProcessed() == ultimosChars) {
      Serial.println("ATENCAO: o GPS nao esta enviando dados (confira fiacao TXD->RX2 e alimentacao).");
    }
    ultimosChars = gps.charsProcessed();
  }
#endif

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') continue;
    c = tolower(c);
    if (c == 'l') listarArquivos();
    else if (c == 'm') mostrarConteudo();
    else if (c == 'x') apagarTudo();
    else if (c == 'w') ultimaTentativaWifi = millis() - INTERVALO_WIFI_MS;  // forca tentativa agora
  }
}
