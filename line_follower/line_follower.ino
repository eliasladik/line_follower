/*
 * ===================================================================
 *  LINE FOLLOWER - ESP32 + L298N + 5x TCRT5000 (LaskaKit LA131112)
 * ===================================================================
 *
 *  Robot jede po černé čáře pomocí PID regulace a zároveň si
 *  za jízdy hostuje jednoduchou webovou stránku, na které vidíš
 *  živě stav senzorů a můžeš měnit PID konstanty a rychlost
 *  bez nutnosti nahrávat kód znovu.
 *
 *  ESP32 se po startu chová jako WiFi Access Point:
 *      SSID:     LineFollower
 *      Heslo:    lajnovac123
 *      Stránka:  http://192.168.4.1
 *
 *  Vyžaduje: ESP32 Arduino core 3.x (kvůli ledcAttach/ledcWrite API)
 *  Board Manager: "esp32" by Espressif Systems, verze 3.0.0 a vyšší
 *
 * ===================================================================
 *  ZAPOJENÍ - viz README.md pro tabulku a schéma
 * ===================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>

// ---------------------------------------------------------------
// KONFIGURACE PINŮ (uprav podle svého zapojení, viz README.md)
// ---------------------------------------------------------------

// 5x IR senzor TCRT5000, zleva doprava
const int SENSOR_PINS[5] = {34, 35, 32, 33, 25};

// Pokud tvoje deska hlásí HIGH nad bílým a LOW nad černou čárou,
// nastav na true (prohodí logiku). Zjistíš to podle sériové
// konzole nebo webu - najeď senzorem nad čáru a sleduj hodnoty.
bool INVERT_SENSORS = false;

// L298N - motor A (levý)
const int ENA = 27;   // PWM
const int IN1 = 26;
const int IN2 = 14;

// L298N - motor B (pravý)
const int ENB = 13;   // PWM
const int IN3 = 16;
const int IN4 = 17;

// PWM nastavení
const int PWM_FREQ = 5000;
const int PWM_RES  = 8;   // 8 bitů = 0-255

// ---------------------------------------------------------------
// PID a rychlost - výchozí hodnoty, dají se měnit na webu
// ---------------------------------------------------------------
float Kp = 18.0;
float Ki = 0.0;
float Kd = 8.0;
int   baseSpeed = 150;    // 0-255

// ---------------------------------------------------------------
// Vnitřní proměnné - nesahat
// ---------------------------------------------------------------
bool running = false;
int  sensorVal[5] = {0, 0, 0, 0, 0};
float lastError = 0;
float integral = 0;
unsigned long lastPidTime = 0;

WebServer server(80);
Preferences prefs;

// ---------------------------------------------------------------
// Ukládání PID konstant a rychlosti do trvalé paměti (NVS)
// ---------------------------------------------------------------
const char* PREFS_NAMESPACE = "linefw";

void loadPidFromFlash() {
  prefs.begin(PREFS_NAMESPACE, true);  // read-only otevření
  Kp = prefs.getFloat("kp", Kp);
  Ki = prefs.getFloat("ki", Ki);
  Kd = prefs.getFloat("kd", Kd);
  baseSpeed = prefs.getInt("speed", baseSpeed);
  prefs.end();
}

void savePidToFlash() {
  prefs.begin(PREFS_NAMESPACE, false);  // read-write otevření
  prefs.putFloat("kp", Kp);
  prefs.putFloat("ki", Ki);
  prefs.putFloat("kd", Kd);
  prefs.putInt("speed", baseSpeed);
  prefs.end();
}

// ---------------------------------------------------------------
// Nastavení rychlosti a směru motorů
// ---------------------------------------------------------------
void setMotors(int leftSpeed, int rightSpeed) {
  // Omez rozsah 0-255
  leftSpeed  = constrain(leftSpeed, 0, 255);
  rightSpeed = constrain(rightSpeed, 0, 255);

  // Levý motor - vpřed (pokud jede opačně, prohoď IN1/IN2 na desce)
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  ledcWrite(ENA, leftSpeed);

  // Pravý motor - vpřed (pokud jede opačně, prohoď IN3/IN4 na desce)
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  ledcWrite(ENB, rightSpeed);
}

void stopMotors() {
  ledcWrite(ENA, 0);
  ledcWrite(ENB, 0);
}

// ---------------------------------------------------------------
// Čtení senzorů a výpočet chyby (error)
// váhy zleva doprava: -2 -1 0 1 2
// ---------------------------------------------------------------
float readLineError() {
  int weights[5] = {-2, -1, 0, 1, 2};
  int activeCount = 0;
  int weightedSum = 0;

  for (int i = 0; i < 5; i++) {
    int raw = digitalRead(SENSOR_PINS[i]);
    int val = INVERT_SENSORS ? !raw : raw;   // 1 = vidí čáru
    sensorVal[i] = val;
    if (val) {
      activeCount++;
      weightedSum += weights[i];
    }
  }

  if (activeCount == 0) {
    // čára ztracena - pokračuj naposledy známým směrem (extrapolace)
    return (lastError > 0) ? 3.0 : (lastError < 0 ? -3.0 : 0.0);
  }

  float error = (float)weightedSum / (float)activeCount;
  return error;
}

// ---------------------------------------------------------------
// PID smyčka - volat pravidelně
// ---------------------------------------------------------------
void pidLoop() {
  unsigned long now = millis();
  float dt = (now - lastPidTime) / 1000.0;
  if (dt <= 0) dt = 0.001;
  lastPidTime = now;

  float error = readLineError();
  integral += error * dt;
  integral = constrain(integral, -50, 50);  // anti-windup
  float derivative = (error - lastError) / dt;
  lastError = error;

  float correction = Kp * error + Ki * integral + Kd * derivative;

  int leftSpeed  = baseSpeed - (int)correction;
  int rightSpeed = baseSpeed + (int)correction;

  setMotors(leftSpeed, rightSpeed);
}

// ---------------------------------------------------------------
// HTTP handlery
// ---------------------------------------------------------------
void handleStatus() {
  String json = "{";
  json += "\"s\":[";
  for (int i = 0; i < 5; i++) {
    json += sensorVal[i];
    if (i < 4) json += ",";
  }
  json += "],";
  json += "\"error\":" + String(lastError, 2) + ",";
  json += "\"kp\":" + String(Kp, 1) + ",";
  json += "\"ki\":" + String(Ki, 1) + ",";
  json += "\"kd\":" + String(Kd, 1) + ",";
  json += "\"speed\":" + String(baseSpeed) + ",";
  json += "\"running\":" + String(running ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleSet() {
  if (server.hasArg("kp")) Kp = server.arg("kp").toFloat();
  if (server.hasArg("ki")) Ki = server.arg("ki").toFloat();
  if (server.hasArg("kd")) Kd = server.arg("kd").toFloat();
  if (server.hasArg("speed")) baseSpeed = server.arg("speed").toInt();
  server.send(200, "text/plain", "OK");
}

void handleSave() {
  savePidToFlash();
  server.send(200, "text/plain", "SAVED");
}

void handleToggle() {
  running = !running;
  if (!running) {
    stopMotors();
    integral = 0;
  }
  String json = "{\"running\":" + String(running ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

// ---------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin(true)) {  // true = automaticky naformatuj, pokud filesystem chybi
    Serial.println("Chyba: LittleFS se nepodarilo pripojit!");
  }

  loadPidFromFlash();  // načti uložené PID konstanty a rychlost (pokud existují)

  for (int i = 0; i < 5; i++) {
    pinMode(SENSOR_PINS[i], INPUT);
  }

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  ledcAttach(ENA, PWM_FREQ, PWM_RES);
  ledcAttach(ENB, PWM_FREQ, PWM_RES);

  stopMotors();

  // Access Point - připoj se z mobilu/notebooku na tuto síť
  WiFi.softAP("LineFollower", "lajnovac123");
  Serial.print("AP spustena, IP adresa: ");
  Serial.println(WiFi.softAPIP());  // typicky 192.168.4.1

  // Webová stránka se načítá ze soboru data/index.html (LittleFS),
  // ne z kódu - staci upravit ten soubor a nahrát jen filesystem,
  // bez překompilování celého firmwaru.
  server.serveStatic("/", LittleFS, "/index.html");
  server.on("/status", handleStatus);
  server.on("/set", handleSet);
  server.on("/save", handleSave);
  server.on("/toggle", handleToggle);
  server.begin();

  lastPidTime = millis();
}

// ---------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------
void loop() {
  server.handleClient();

  if (running) {
    pidLoop();
  } else {
    stopMotors();
    readLineError();  // pořád čti senzory, ať je vidět stav na webu i vypnuto
  }

  delay(10);
}
