/*
 * Télécommande ESP32-C3 — ESP-NOW TX
 * GPIO 2 : Joystick X (gauche/droite)
 * GPIO 3 : Joystick Y (avant/arrière)
 * GPIO 4 : Potentiomètre servo
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "espnow_packet.h"

// MAC WiFi STA du rover (ESP32-WROOM32), confirmée via esptool
uint8_t ROVER_MAC[] = {0xE4, 0x65, 0xB8, 0xD8, 0xE8, 0xB4};

#define PIN_X     2
#define PIN_Y     3
#define PIN_SERVO 4

// Zone morte brute autour du centre ADC (2048).
#define DEAD_ZONE 600

// Déflexion physique (en unités ADC depuis le centre) correspondant à 100%.
// Réglé à ~50 % de la course totale (2047) → 100% atteint à mi-course.
// Au-delà, la valeur est saturée à 100.
#define MAX_DEFLECT 1100

int8_t normalize(int raw) {
    int v = raw - 2048;
    if (abs(v) < DEAD_ZONE) return 0;
    if (v > 0) return (int8_t)constrain(map(v, DEAD_ZONE, MAX_DEFLECT, 1, 100), 1, 100);
    else       return (int8_t)constrain(map(v, -MAX_DEFLECT, -DEAD_ZONE, -100, -1), -100, -1);
}

void setup() {
    Serial.begin(115200);
    delay(1000);  // laisser le rover démarrer en premier

    analogSetAttenuation(ADC_11db);

    WiFi.mode(WIFI_STA);
    delay(200);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init FAILED");
        while (1) delay(1000);
    }
    // Recv CB vide — nécessaire pour activer le module de réception ESP-NOW
    esp_now_register_recv_cb([](const esp_now_recv_info_t*, const uint8_t*, int){});

    // Peer broadcast (requis pour que l'envoi unicast fonctionne dans core v3.x)
    esp_now_peer_info_t bcast = {};
    memset(bcast.peer_addr, 0xFF, 6);
    bcast.channel = 0; bcast.encrypt = false;
    esp_now_add_peer(&bcast);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, ROVER_MAC, 6);
    peer.channel = 0;   // canal courant (laisser ESP-NOW gérer)
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("Peer add FAILED");
        while (1) delay(1000);
    }

    Serial.print("TX prêt. MAC locale: ");
    Serial.println(WiFi.macAddress());
    Serial.printf("Rover cible: %02X:%02X:%02X:%02X:%02X:%02X ch:1\n",
                  ROVER_MAC[0], ROVER_MAC[1], ROVER_MAC[2],
                  ROVER_MAC[3], ROVER_MAC[4], ROVER_MAC[5]);
}

void loop() {
    int raw_x = analogRead(PIN_X);
    int raw_y = analogRead(PIN_Y);
    int raw_s = analogRead(PIN_SERVO);

    espnow_packet_t pkt;
    pkt.x   = -normalize(raw_x);  // inversion sens G/D
    pkt.y   = -normalize(raw_y);  // inversion sens AV/AR
    pkt.srv = (uint8_t)map(raw_s, 0, 4095, 0, 180);

    esp_err_t res = esp_now_send(ROVER_MAC, (uint8_t *)&pkt, sizeof(pkt));

    Serial.printf("X=%4d(%4d) Y=%4d(%4d) S=%4d(%3d) [%s]\n",
        raw_x, pkt.x, raw_y, pkt.y, raw_s, pkt.srv,
        res == ESP_OK ? "OK" : esp_err_to_name(res));

    delay(50);  // 20Hz — évite le watchdog et la saturation WiFi
}
