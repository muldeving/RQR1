/*
 * Pilotage d'un rover via ESP32 WROOM32 + double pont en H L298
 * - L'ESP32 crée son propre réseau WiFi (mode Access Point)
 * - Une page web sert d'interface : 4 boutons (avant, arrière, gauche, droite)
 *   + un curseur (0 à 100 %) pour régler la vitesse
 * - La rotation se fait sur place : les deux moteurs tournent en sens opposés
 * - La vitesse est appliquée en PWM via ledcWrite
 */

#include <WiFi.h>
#include <WebServer.h>

// ---------- Configuration WiFi (point d'accès) ----------
const char* AP_SSID     = "Rover_ESP32";
const char* AP_PASSWORD = "rover1234"; // 8 caractères minimum

// ---------- Brochage L298 ----------
// Module L298 avec 4 entrees seulement (pas d'ENA/ENB) :
// on pilote la vitesse en envoyant le PWM directement sur les
// entrees IN1/IN2 (moteur gauche) et IN3/IN4 (moteur droit).
// Une seule des deux entrees recoit le PWM, l'autre reste a 0,
// selon le sens de rotation demande.
const int IN1 = 26;   // moteur gauche - entree 1
const int IN2 = 27;   // moteur gauche - entree 2
const int IN3 = 25;   // moteur droit  - entree 1
const int IN4 = 33;   // moteur droit  - entree 2

// ---------- Configuration PWM (LEDC) ----------
const int PWM_FREQ      = 1000;   // 1 kHz, bon compromis pour le L298
const int PWM_RESOLUTION = 8;     // 8 bits => 0..255
// Un canal LEDC par entree (4 canaux PWM au total)
const int PWM_CH_IN1 = 0;
const int PWM_CH_IN2 = 1;
const int PWM_CH_IN3 = 2;
const int PWM_CH_IN4 = 3;

// Vitesse courante (0..100 %)
int currentSpeed = 60;

WebServer server(80);

// ---------- Commandes moteurs ----------
// Sur ce module L298 sans ENA/ENB, le sens et la vitesse sont
// donnes par les deux entrees du moteur :
//   avant   : INx = PWM, INy = 0
//   arriere : INx = 0,   INy = PWM
//   stop    : INx = 0,   INy = 0
void setMotorLeft(int dir, int pwm) {
  // dir : +1 avant, -1 arriere, 0 stop
  if (dir > 0) {
    ledcWrite(PWM_CH_IN1, pwm);
    ledcWrite(PWM_CH_IN2, 0);
  } else if (dir < 0) {
    ledcWrite(PWM_CH_IN1, 0);
    ledcWrite(PWM_CH_IN2, pwm);
  } else {
    ledcWrite(PWM_CH_IN1, 0);
    ledcWrite(PWM_CH_IN2, 0);
  }
}

void setMotorRight(int dir, int pwm) {
  if (dir > 0) {
    ledcWrite(PWM_CH_IN3, pwm);
    ledcWrite(PWM_CH_IN4, 0);
  } else if (dir < 0) {
    ledcWrite(PWM_CH_IN3, 0);
    ledcWrite(PWM_CH_IN4, pwm);
  } else {
    ledcWrite(PWM_CH_IN3, 0);
    ledcWrite(PWM_CH_IN4, 0);
  }
}

int speedToPwm(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return map(pct, 0, 100, 0, 255);
}

void rover_forward()  { int p = speedToPwm(currentSpeed); setMotorLeft(+1, p); setMotorRight(+1, p); }
void rover_backward() { int p = speedToPwm(currentSpeed); setMotorLeft(-1, p); setMotorRight(-1, p); }
void rover_left()     { int p = speedToPwm(currentSpeed); setMotorLeft(-1, p); setMotorRight(+1, p); } // rotation sur place
void rover_right()    { int p = speedToPwm(currentSpeed); setMotorLeft(+1, p); setMotorRight(-1, p); } // rotation sur place
void rover_stop()     { setMotorLeft(0, 0); setMotorRight(0, 0); }

// ---------- Page web ----------
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Pilotage Rover ESP32</title>
<style>
  body { font-family: Arial, sans-serif; text-align: center; background:#222; color:#eee; margin:0; padding:20px; }
  h1 { margin-top: 0; }
  .grid { display: grid; grid-template-columns: repeat(3, 100px); gap: 10px; justify-content: center; margin: 20px auto; }
  button {
    width: 100px; height: 100px; font-size: 18px; border-radius: 12px;
    border: none; background:#0066cc; color:white; cursor:pointer;
    touch-action: manipulation; user-select: none;
  }
  button:active { background:#004080; }
  .stop { background:#cc0000; }
  .stop:active { background:#800000; }
  .empty { visibility:hidden; }
  .speedBox { margin-top: 25px; }
  input[type=range] { width: 80%; }
  .val { font-size: 22px; font-weight: bold; }
</style>
</head>
<body>
  <h1>Pilotage Rover</h1>

  <div class="grid">
    <button class="empty"></button>
    <button id="btnF" onmousedown="cmd('F')" onmouseup="cmd('S')" ontouchstart="cmd('F')" ontouchend="cmd('S')">Avant</button>
    <button class="empty"></button>

    <button id="btnL" onmousedown="cmd('L')" onmouseup="cmd('S')" ontouchstart="cmd('L')" ontouchend="cmd('S')">Gauche</button>
    <button id="btnS" class="stop" onclick="cmd('S')">STOP</button>
    <button id="btnR" onmousedown="cmd('R')" onmouseup="cmd('S')" ontouchstart="cmd('R')" ontouchend="cmd('S')">Droite</button>

    <button class="empty"></button>
    <button id="btnB" onmousedown="cmd('B')" onmouseup="cmd('S')" ontouchstart="cmd('B')" ontouchend="cmd('S')">Arriere</button>
    <button class="empty"></button>
  </div>

  <div class="speedBox">
    <div>Vitesse : <span class="val" id="speedVal">60</span> %</div>
    <input type="range" min="0" max="100" value="60" id="speedSlider"
           oninput="onSpeedInput(this.value)"
           onchange="onSpeedChange(this.value)">
  </div>

<script>
  function cmd(c){
    fetch('/cmd?c=' + c).catch(e=>{});
  }
  function onSpeedInput(v){
    document.getElementById('speedVal').innerText = v;
  }
  function onSpeedChange(v){
    fetch('/speed?v=' + v).catch(e=>{});
  }
  // Securite : si on quitte/perd le contact, on coupe
  window.addEventListener('blur',  ()=>cmd('S'));
  window.addEventListener('mouseup', ()=>cmd('S'));
  window.addEventListener('touchend', ()=>cmd('S'));
</script>
</body>
</html>
)HTML";

// ---------- Handlers HTTP ----------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleCmd() {
  if (!server.hasArg("c")) { server.send(400, "text/plain", "missing c"); return; }
  String c = server.arg("c");
  if      (c == "F") rover_forward();
  else if (c == "B") rover_backward();
  else if (c == "L") rover_left();
  else if (c == "R") rover_right();
  else               rover_stop();
  server.send(200, "text/plain", "OK");
}

void handleSpeed() {
  if (!server.hasArg("v")) { server.send(400, "text/plain", "missing v"); return; }
  currentSpeed = server.arg("v").toInt();
  if (currentSpeed < 0)   currentSpeed = 0;
  if (currentSpeed > 100) currentSpeed = 100;
  server.send(200, "text/plain", String(currentSpeed));
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);

  // PWM sur les 4 entrees du L298
  ledcSetup(PWM_CH_IN1, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(IN1, PWM_CH_IN1);
  ledcSetup(PWM_CH_IN2, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(IN2, PWM_CH_IN2);
  ledcSetup(PWM_CH_IN3, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(IN3, PWM_CH_IN3);
  ledcSetup(PWM_CH_IN4, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(IN4, PWM_CH_IN4);

  rover_stop();

  // Point d'acces WiFi
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP demarre. SSID: "); Serial.println(AP_SSID);
  Serial.print("IP: "); Serial.println(ip);

  // Routes
  server.on("/",      handleRoot);
  server.on("/cmd",   handleCmd);
  server.on("/speed", handleSpeed);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Serveur HTTP demarre.");
}

void loop() {
  server.handleClient();
}
