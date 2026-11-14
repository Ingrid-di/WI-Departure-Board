
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h"

// =====================================
// CONFIGURACOES
// =====================================

const char* RMV_PARADA_ID = "3025514";

const char* FUSO_ALEMANHA =
  "CET-1CEST,M3.5.0/2,M10.5.0/3";

const unsigned long INTERVALO_RMV = 60000;

// =====================================
// TELA
// =====================================

TFT_eSPI tela = TFT_eSPI();

const uint16_t FUNDO = TFT_NAVY;
const uint16_t AMARELO = TFT_YELLOW;
const uint16_t BRANCO = TFT_WHITE;
const uint16_t CINZA = 0xA514;

// =====================================
// PARTIDAS
// =====================================

struct Partida {
  String linha;
  String destino;
  String horario;
};

Partida partidas[5];

int quantidadePartidas = 0;

String statusRMV = "AGUARDANDO RMV";

// =====================================
// DESENHAR PAINEL
// =====================================

void desenharPainel() {
  tela.fillScreen(FUNDO);

  // Cabecalho
  tela.setTextColor(AMARELO, FUNDO);
  tela.drawString("WI DEPARTURES", 8, 8, 2);

  tela.setTextColor(CINZA, FUNDO);
  tela.drawString("SEDANPLATZ - BUS", 8, 30, 1);
  tela.drawFastHLine(8, 46, 304, CINZA);

  // Colunas
  tela.drawString("LINIE", 8, 55, 2);
  tela.drawString("RICHTUNG", 70, 55, 2);
  tela.drawRightString("ABFAHRT", 312, 55, 2);

  tela.drawFastHLine(8, 77, 304, CINZA);

  if (quantidadePartidas == 0) {
    tela.setTextColor(BRANCO, FUNDO);
    tela.drawString("Keine Abfahrten", 10, 95, 2);
  }

  // Mostrar ate 5 partidas
  for (int i = 0; i < quantidadePartidas; i++) {
    int y = 86 + i * 27;

    tela.setTextColor(AMARELO, FUNDO);
    tela.drawString(partidas[i].linha, 10, y, 2);

    tela.setTextColor(BRANCO, FUNDO);

    String destino = partidas[i].destino;

    // Encurtar destinos muito grandes
    while (destino.length() > 0 &&
           tela.textWidth(destino, 2) > 170) {
      destino.remove(destino.length() - 1);
    }

    tela.drawString(destino, 70, y, 2);

    tela.drawRightString(
      partidas[i].horario,
      312,
      y,
      2
    );
  }

  // Rodape
  tela.drawFastHLine(8, 222, 304, CINZA);

  tela.setTextColor(CINZA, FUNDO);
  tela.drawString(statusRMV, 8, 229, 1);
}

// =====================================
// RELOGIO
// =====================================

void atualizarRelogio() {
  struct tm horario;

  tela.fillRect(255, 5, 65, 25, FUNDO);

  if (getLocalTime(&horario, 100)) {
    char hora[6];

    strftime(
      hora,
      sizeof(hora),
      "%H:%M",
      &horario
    );

    tela.setTextColor(BRANCO, FUNDO);
    tela.drawRightString(hora, 312, 9, 2);

  } else {
    tela.setTextColor(AMARELO, FUNDO);
    tela.drawRightString("--:--", 312, 9, 2);
  }
}

// =====================================
// CONECTAR WI-FI
// =====================================

void conectarWiFi() {
  Serial.println("Conectando ao Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NOME, WIFI_SENHA);

  unsigned long inicio = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - inicio < 20000
  ) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi conectado!");

    configTzTime(
      FUSO_ALEMANHA,
      "pool.ntp.org",
      "time.google.com"
    );

  } else {
    Serial.println("\nWi-Fi nao conectado");
  }
}

// =====================================
// CONSULTAR RMV
// =====================================

void consultarRMV() {
  if (WiFi.status() != WL_CONNECTED) {
    statusRMV = "WI-FI OFFLINE";
    desenharPainel();
    atualizarRelogio();
    return;
  }

  Serial.println("Consultando RMV - Sedanplatz...");

  WiFiClientSecure cliente;

  // Temporario para teste:
  // certificado HTTPS nao verificado
  cliente.setInsecure();

  HTTPClient http;
  http.setTimeout(15000);

  String url =
    "https://www.rmv.de/hapi/departureBoard";

  url += "?accessId=";
  url += RMV_ACCESS_ID;

  url += "&id=";
  url += RMV_PARADA_ID;

  url += "&format=json";
  url += "&maxJourneys=5";

  if (!http.begin(cliente, url)) {
    Serial.println("Erro ao iniciar conexao RMV");
    statusRMV = "ERRO DE CONEXAO";
    desenharPainel();
    atualizarRelogio();
    return;
  }

  int codigo = http.GET();

  Serial.print("Codigo HTTP: ");
  Serial.println(codigo);

  if (codigo == HTTP_CODE_OK) {

    // Filtrar somente os dados necessarios
    JsonDocument filtro;

    filtro["Departure"][0]["direction"] = true;
    filtro["Departure"][0]["time"] = true;
    filtro["Departure"][0]["rtTime"] = true;
    filtro["Departure"][0]["ProductAtStop"]["line"] = true;

    JsonDocument dados;

    DeserializationError erro = deserializeJson(
    dados,
    http.getStream(),s
    DeserializationOption::Filter(filtro),
    DeserializationOption::NestingLimit(32)
    );

    if (erro) {
      Serial.print("Erro JSON: ");
      Serial.println(erro.c_str());

      statusRMV = "ERRO AO LER RMV";

    } else {
      JsonArray lista =
        dados["Departure"].as<JsonArray>();

      quantidadePartidas = 0;

      for (JsonObject item : lista) {
        if (quantidadePartidas >= 5) {
          break;
        }

        const char* linha =
          item["ProductAtStop"]["line"] | "?";

        const char* destino =
          item["direction"] | "?";

        const char* horarioReal =
          item["rtTime"] | "";

        const char* horarioPrevisto =
          item["time"] | "--:--";

        String horario;

        if (strlen(horarioReal) > 0) {
          horario = String(horarioReal);
        } else {
          horario = String(horarioPrevisto);
        }

        partidas[quantidadePartidas].linha =
          String(linha);

        partidas[quantidadePartidas].destino =
          String(destino);

        partidas[quantidadePartidas].horario =
          horario;

        Serial.print("Linha: ");
        Serial.print(linha);

        Serial.print(" | Destino: ");
        Serial.print(destino);

        Serial.print(" | Horario: ");
        Serial.println(horario);

        quantidadePartidas++;
      }

      if (quantidadePartidas > 0) {
        statusRMV = "RMV LIVE - 60 SEK";
      } else {
        statusRMV = "KEINE ABFAHRTEN";
      }

      Serial.print("Partidas recebidas: ");
      Serial.println(quantidadePartidas);
    }

  } else {
    Serial.print("Erro RMV: ");
    Serial.println(codigo);

    statusRMV = "RMV NICHT ERREICHBAR";
  }

  http.end();

  desenharPainel();
  atualizarRelogio();
}

// =====================================
// SETUP
// =====================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Ligar iluminacao da tela
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  tela.init();
  tela.setRotation(1);

  desenharPainel();
  atualizarRelogio();

  conectarWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    struct tm horario;

    if (getLocalTime(&horario, 10000)) {
      Serial.println("Horario sincronizado!");
    } else {
      Serial.println("Horario nao sincronizado");
    }

    consultarRMV();

  } else {
    statusRMV = "WI-FI OFFLINE";
    desenharPainel();
  }

  atualizarRelogio();
}

// =====================================
// LOOP
// =====================================

void loop() {
  static unsigned long ultimaAtualizacaoRelogio = 0;
  static unsigned long ultimaConsultaRMV = millis();

  // Atualizar relogio a cada segundo
  if (
    millis() - ultimaAtualizacaoRelogio >= 1000
  ) {
    ultimaAtualizacaoRelogio = millis();
    atualizarRelogio();
  }

  // Consultar RMV a cada 60 segundos
  if (
    millis() - ultimaConsultaRMV >= INTERVALO_RMV
  ) {
    ultimaConsultaRMV = millis();

    if (WiFi.status() != WL_CONNECTED) {
      conectarWiFi();
    }

    consultarRMV();
  }
}
