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
// WEBOVÁ STRÁNKA (uložena ve flash paměti, ne v RAM)
// ---------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="cs">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Line Follower</title>
<style>
  body { font-family: -apple-system, Arial, sans-serif; background:#111; color:#eee; margin:0; padding:16px; }
  h1 { font-size:20px; text-align:center; margin-bottom:4px; }
  .card { background:#1c1c1c; border-radius:12px; padding:16px; margin-bottom:14px; }
  .sensors { display:flex; justify-content:space-between; gap:6px; }
  .sensor { flex:1; height:60px; border-radius:8px; background:#333; display:flex; align-items:flex-end; justify-content:center; transition: background 0.15s; }
  .sensor.on { background:#2ecc71; }
  label { display:block; margin-top:10px; font-size:14px; color:#aaa; }
  input[type=range] { width:100%; }
  .val { float:right; color:#2ecc71; font-weight:bold; }
  button { width:100%; padding:14px; font-size:16px; border:none; border-radius:10px; margin-top:8px; font-weight:bold; }
  #toggleBtn.stopped { background:#2ecc71; color:#000; }
  #toggleBtn.running { background:#e74c3c; color:#fff; }
  .status { text-align:center; font-size:13px; color:#888; margin-top:6px; }
</style>
</head>
<body>
  <h1>🤖 Line Follower</h1>

  <div class="card">
    <div class="sensors" id="sensors">
      <div class="sensor"></div><div class="sensor"></div><div class="sensor"></div>
      <div class="sensor"></div><div class="sensor"></div>
    </div>
    <div class="status" id="errText">error: 0</div>
  </div>

  <div class="card">
    <label>Kp <span class="val" id="kpVal">0</span></label>
    <input type="range" id="kp" min="0" max="50" step="0.5" oninput="onChange()">

    <label>Ki <span class="val" id="kiVal">0</span></label>
    <input type="range" id="ki" min="0" max="5" step="0.1" oninput="onChange()">

    <label>Kd <span class="val" id="kdVal">0</span></label>
    <input type="range" id="kd" min="0" max="30" step="0.5" oninput="onChange()">

    <label>Rychlost <span class="val" id="spVal">0</span></label>
    <input type="range" id="sp" min="0" max="255" step="5" oninput="onChange()">
  </div>

  <button id="toggleBtn" onclick="toggleRun()">START</button>

<script>
let debounceTimer = null;

function onChange() {
  document.getElementById('kpVal').innerText = document.getElementById('kp').value;
  document.getElementById('kiVal').innerText = document.getElementById('ki').value;
  document.getElementById('kdVal').innerText = document.getElementById('kd').value;
  document.getElementById('spVal').innerText = document.getElementById('sp').value;
  clearTimeout(debounceTimer);
  debounceTimer = setTimeout(sendSettings, 150);
}

function sendSettings() {
  const kp = document.getElementById('kp').value;
  const ki = document.getElementById('ki').value;
  const kd = document.getElementById('kd').value;
  const sp = document.getElementById('sp').value;
  fetch(`/set?kp=${kp}&ki=${ki}&kd=${kd}&speed=${sp}`);
}

function toggleRun() {
  fetch('/toggle').then(r => r.json()).then(d => updateRunUI(d.running));
}

function updateRunUI(running) {
  const btn = document.getElementById('toggleBtn');
  btn.innerText = running ? 'STOP' : 'START';
  btn.className = running ? 'running' : 'stopped';
}

function poll() {
  fetch('/status').then(r => r.json()).then(d => {
    for (let i = 0; i < 5; i++) {
      document.getElementById('sensors').children[i].className =
        'sensor' + (d.s[i] ? ' on' : '');
    }
    document.getElementById('errText').innerText = 'error: ' + d.error.toFixed(2);
    document.getElementById('kp').value = d.kp;
    document.getElementById('ki').value = d.ki;
    document.getElementById('kd').value = d.kd;
    document.getElementById('sp').value = d.speed;
    document.getElementById('kpVal').innerText = d.kp;
    document.getElementById('kiVal').innerText = d.ki;
    document.getElementById('kdVal').innerText = d.kd;
    document.getElementById('spVal').innerText = d.speed;
    updateRunUI(d.running);
  });
}
setInterval(poll, 300);
poll();
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------
// HTTP handlery
// ---------------------------------------------------------------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

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

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/set", handleSet);
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
