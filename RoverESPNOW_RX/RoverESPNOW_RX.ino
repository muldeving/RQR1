/*
 * Rover ESP32-WROOM32 — ESP-NOW RX + pilotage L298N (PWM) + moteur DC on/off
 *   IN1=26 IN2=27 ENA=14  (moteur gauche, PWM 1kHz 8-bit)
 *   IN3=12 IN4=33 ENB=32  (moteur droit,  PWM 1kHz 8-bit)
 *   MOTOR_PIN=13           (moteur DC auxiliaire, on/off)
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "espnow_packet.h"

// L298N
const int IN1 = 26, IN2 = 27, ENA = 14;
const int IN3 = 12, IN4 = 33, ENB = 32;

const int PWM_FREQ = 1000;
const int PWM_RES  = 8;

// Moteur DC auxiliaire (on/off)
const int MOTOR_PIN = 13;

volatile unsigned long last_packet_ms = 0;

void setMotorLeft(int dir, int pwm) {
    if      (dir > 0) { digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);  }
    else if (dir < 0) { digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH); }
    else              { digitalWrite(IN1, LOW);   digitalWrite(IN2, LOW);  }
    ledcWrite(ENA, pwm);
}

void setMotorRight(int dir, int pwm) {
    if      (dir > 0) { digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);  }
    else if (dir < 0) { digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH); }
    else              { digitalWrite(IN3, LOW);   digitalWrite(IN4, LOW);  }
    ledcWrite(ENB, pwm);
}

void rover_stop() { setMotorLeft(0, 0); setMotorRight(0, 0); }

void applyDrive(int8_t x, int8_t y) {
    if (x == 0 && y == 0) { rover_stop(); return; }

    if (y == 0) {
        int p = map(abs(x), 0, 100, 0, 255);
        if (x > 0) { setMotorLeft(+1, p); setMotorRight(-1, p); }
        else        { setMotorLeft(-1, p); setMotorRight(+1, p); }
        return;
    }

    int dir  = (y > 0) ? 1 : -1;
    int base = map(abs(y), 0, 100, 0, 255);
    int turn = map(abs(x), 0, 100, 0, 255);

    int pwmL = base, pwmR = base;
    if      (x > 0) { pwmL = base - turn; pwmR = min(base + turn, 255); }
    else if (x < 0) { pwmL = min(base + turn, 255); pwmR = base - turn; }

    // Roue intérieure peut contra-tourner si turn > base
    if (pwmL < 0) setMotorLeft(-dir, -pwmL);
    else          setMotorLeft( dir,  pwmL);
    if (pwmR < 0) setMotorRight(-dir, -pwmR);
    else          setMotorRight( dir,  pwmR);
}

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != sizeof(espnow_packet_t)) return;
    const espnow_packet_t *pkt = (const espnow_packet_t *)data;
    last_packet_ms = millis();
    applyDrive(pkt->x, pkt->y);
    digitalWrite(MOTOR_PIN, pkt->srv ? LOW : HIGH);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // L298N
    pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
    ledcAttach(ENA, PWM_FREQ, PWM_RES);
    ledcAttach(ENB, PWM_FREQ, PWM_RES);
    rover_stop();

    // Moteur DC auxiliaire
    pinMode(MOTOR_PIN, OUTPUT);
    digitalWrite(MOTOR_PIN, HIGH);  // inhibé au démarrage

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    delay(200);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init FAILED");
        while (1) delay(1000);
    }

    esp_now_peer_info_t bcast = {};
    memset(bcast.peer_addr, 0xFF, 6);
    bcast.channel = 0; bcast.encrypt = false;
    esp_now_add_peer(&bcast);

    esp_now_register_recv_cb(onDataRecv);

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    Serial.printf("RX prêt. MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void loop() {
    unsigned long now = millis();
    unsigned long ago = now - last_packet_ms;

    if (ago > 300) {
        rover_stop();
        digitalWrite(MOTOR_PIN, HIGH);
    }

    static unsigned long last_log = 0;
    if (now - last_log >= 2000) {
        last_log = now;
        if (ago < 1000)
            Serial.printf("[OK] dernier paquet il y a %lums\n", ago);
        else
            Serial.printf("[WD] pas de paquet depuis %lus\n", ago / 1000);
    }

    delay(20);
}
