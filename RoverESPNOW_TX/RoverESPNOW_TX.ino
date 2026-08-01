/*
 * Télécommande ESP32-C3 — ESP-NOW TX
 * GPIO 2 : Joystick X  |  GPIO 3 : Joystick Y
 * GPIO 4 : Interrupteur moteur auxiliaire (GND = ON)
 *
 * Commande Serial : "cal" → calibrage des axes (sauvegardé en flash)
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "driver/gpio.h"
#include <Preferences.h>
#include "espnow_packet.h"

uint8_t ROVER_MAC[] = {0xE4, 0x65, 0xB8, 0xD8, 0xE8, 0xB4};

#define PIN_X      2
#define PIN_Y      3
#define PIN_SWITCH 4

// Zone morte en unités ADC autour du centre calibré
#define DEAD_ZONE 600

struct CalData { int cx, cy, xmin, xmax, ymin, ymax; };
CalData cal = {2048, 2048, 0, 4095, 0, 4095};

Preferences prefs;

void load_cal() {
    prefs.begin("joy", true);
    cal.cx   = prefs.getInt("cx",   2048);
    cal.cy   = prefs.getInt("cy",   2048);
    cal.xmin = prefs.getInt("xmin", 0);
    cal.xmax = prefs.getInt("xmax", 4095);
    cal.ymin = prefs.getInt("ymin", 0);
    cal.ymax = prefs.getInt("ymax", 4095);
    prefs.end();
}

void save_cal() {
    prefs.begin("joy", false);
    prefs.putInt("cx",   cal.cx);   prefs.putInt("cy",   cal.cy);
    prefs.putInt("xmin", cal.xmin); prefs.putInt("xmax", cal.xmax);
    prefs.putInt("ymin", cal.ymin); prefs.putInt("ymax", cal.ymax);
    prefs.end();
}

// Mapping linéaire de la zone morte jusqu'à la limite calibrée
int8_t norm(int raw, int cen, int mn, int mx) {
    int v = raw - cen;
    if (abs(v) < DEAD_ZONE) return 0;
    if (v > 0) return (int8_t)constrain(map(v, DEAD_ZONE, mx - cen, 1, 100), 1, 100);
    else       return (int8_t)constrain(map(v, -(cen - mn), -DEAD_ZONE, -100, -1), -100, -1);
}

// --- Machine à états de calibrage ---
enum CalState { CAL_IDLE, CAL_CENTER, CAL_RANGE };
CalState cal_state = CAL_IDLE;
unsigned long cal_ms = 0;
CalData tmp;
long sum_cx, sum_cy;
int  n_samples;

void start_calibration() {
    cal_state = CAL_CENTER;
    cal_ms    = millis();
    sum_cx = sum_cy = 0;
    n_samples = 0;
    Serial.println("=== Calibrage ===");
    Serial.println("Phase 1/2 (3s) : ne bougez PAS les joysticks...");
}

void update_calibration() {
    int rx = analogRead(PIN_X);
    int ry = analogRead(PIN_Y);
    unsigned long elapsed = millis() - cal_ms;

    if (cal_state == CAL_CENTER) {
        sum_cx += rx; sum_cy += ry; n_samples++;
        if (elapsed >= 3000) {
            tmp.cx = sum_cx / n_samples;
            tmp.cy = sum_cy / n_samples;
            tmp.xmin = tmp.xmax = tmp.cx;
            tmp.ymin = tmp.ymax = tmp.cy;
            Serial.printf("Centre : X=%d Y=%d\n", tmp.cx, tmp.cy);
            Serial.println("Phase 2/2 (5s) : bougez les joysticks jusqu'aux limites !");
            cal_state = CAL_RANGE;
            cal_ms = millis();
        }
    } else if (cal_state == CAL_RANGE) {
        tmp.xmin = min(tmp.xmin, rx); tmp.xmax = max(tmp.xmax, rx);
        tmp.ymin = min(tmp.ymin, ry); tmp.ymax = max(tmp.ymax, ry);
        if (elapsed >= 5000) {
            cal = tmp;
            save_cal();
            Serial.printf("X [%d .. %d .. %d]\n", cal.xmin, cal.cx, cal.xmax);
            Serial.printf("Y [%d .. %d .. %d]\n", cal.ymin, cal.cy, cal.ymax);
            Serial.println("Calibrage sauvegardé.");
            cal_state = CAL_IDLE;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    gpio_reset_pin((gpio_num_t)PIN_SWITCH);
    pinMode(PIN_SWITCH, INPUT_PULLUP);

    load_cal();
    Serial.printf("Cal: X[%d..%d..%d] Y[%d..%d..%d]\n",
        cal.xmin, cal.cx, cal.xmax, cal.ymin, cal.cy, cal.ymax);
    Serial.println("Tapez 'cal' pour recalibrer.");

    WiFi.mode(WIFI_STA);
    delay(200);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init FAILED"); while (1) delay(1000);
    }
    esp_now_register_recv_cb([](const esp_now_recv_info_t*, const uint8_t*, int){});

    esp_now_peer_info_t bcast = {};
    memset(bcast.peer_addr, 0xFF, 6); bcast.channel = 0; bcast.encrypt = false;
    esp_now_add_peer(&bcast);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, ROVER_MAC, 6); peer.channel = 0; peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("Peer add FAILED"); while (1) delay(1000);
    }

    Serial.print("TX prêt. MAC: "); Serial.println(WiFi.macAddress());
}

void loop() {
    // Lecture commande Serial
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "cal" && cal_state == CAL_IDLE) start_calibration();
    }

    // Pendant le calibrage : envoie paquet nul, continue la capture
    if (cal_state != CAL_IDLE) {
        update_calibration();
        espnow_packet_t pkt = {0, 0, 0};
        esp_now_send(ROVER_MAC, (uint8_t *)&pkt, sizeof(pkt));
        delay(50);
        return;
    }

    // Fonctionnement normal
    int raw_x = analogRead(PIN_X);
    int raw_y = analogRead(PIN_Y);
    bool sw   = (digitalRead(PIN_SWITCH) == LOW);

    espnow_packet_t pkt;
    pkt.x   = -norm(raw_x, cal.cx, cal.xmin, cal.xmax);
    pkt.y   = -norm(raw_y, cal.cy, cal.ymin, cal.ymax);
    pkt.srv = sw ? 1 : 0;

    esp_err_t res = esp_now_send(ROVER_MAC, (uint8_t *)&pkt, sizeof(pkt));

    Serial.printf("X=%4d(%4d) Y=%4d(%4d) SW=%s [%s]\n",
        raw_x, pkt.x, raw_y, pkt.y,
        sw ? "ON " : "OFF",
        res == ESP_OK ? "OK" : esp_err_to_name(res));

    delay(50);
}
