# Testes

Testes que rodam no computador, sem a ESP32.

## Firmware (simulação no PC)

O mesmo arquivo `.ino` é compilado com g++ usando substitutos das bibliotecas da ESP32 (`stubs/`)
e a biblioteca TinyGPSPlus original (copiada em `stubs/TinyGPS++.*`, licença LGPL 2.1, Mikal Hart).
O teste alimenta o código com frases NMEA simuladas e verifica viagens 1, 2 e 3, a gravação imediata ao
parar, os filtros e que o comando `x` não zera a numeração das viagens.

```bash
cd tests/firmware-host
g++ -std=c++17 -DARDUINO=1 -Istubs test_viagens.cpp stubs/TinyGPS++.cpp -o test_viagens
./test_viagens
```

## Apps Script (Node.js)

Executa `Codigo.gs` em um ambiente simulado (planilha em memória).

```bash
cd tests/apps-script
node teste_doPost_getPontos.js ../../apps-script/Codigo.gs   # doPost com 5 e 6 colunas e getPontos
node teste_excluir_viagem.js   ../../apps-script/Codigo.gs   # excluirViagem
```
