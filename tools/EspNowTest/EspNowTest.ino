/*
 * Diagnostic ESP-NOW
 * Compile avec TX_MODE défini pour la C3  → envoie unicast vers le rover
 * Sans TX_MODE (rover)                   → écoute et renvoie broadcast
 *
 * C3 FQBN  : esp32:esp32:esp32c3:CDCOnBoot=cdc  + -DTX_MODE
 * WROOM32  : esp32:esp32:esp32  (sans -DTX_MODE)
 */

#include <esp_now.h>
#include <WiFi.h>
#include "esp_wifi.h"

// ---- à ajuster selon la cible ----
// Sur ESP32-C3 uniquement, compiler avec -DTX_MODE=1 via FQBN extra flags
// (ou changer manuellement cette ligne)
#define TX_MODE 1   // 1 = C3 (TX), 0 = rover (RX)
// ----------------------------------

uint8_t BCAST[]     = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t ROVER_MAC[] = {0xE4,0x65,0xB8,0xD8,0xE8,0xB4};

uint32_t counter = 0;

void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    Serial.printf("[RECV] from %02X:%02X:%02X:%02X:%02X:%02X len=%d data=%.*s\n",
        info->src_addr[0], info->src_addr[1], info->src_addr[2],
        info->src_addr[3], info->src_addr[4], info->src_addr[5],
        len, len, (char*)data);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    WiFi.mode(WIFI_STA);
    delay(200);

    uint8_t mac[6]; esp_wifi_get_mac(WIFI_IF_STA, mac);
    Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X mode=%s\n",
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],
        TX_MODE ? "TX" : "RX");

    esp_now_init();
    esp_now_register_recv_cb(onRecv);

    // Ajouter les peers
    auto addPeer = [](uint8_t *m) {
        esp_now_peer_info_t p = {};
        memcpy(p.peer_addr, m, 6);
        p.channel = 0; p.encrypt = false;
        esp_now_add_peer(&p);
    };
    addPeer(BCAST);
    if (TX_MODE) addPeer(ROVER_MAC);

    uint8_t pri; wifi_second_chan_t sec;
    esp_wifi_get_channel(&pri, &sec);
    Serial.printf("Ready — ch%d — %s\n", pri, TX_MODE ? "sending UNICAST to rover" : "listening broadcast");
}

void loop() {
    char msg[32];
    snprintf(msg, sizeof(msg), "PKT_%u", counter++);

    esp_err_t r;
    if (TX_MODE) {
        // C3 : envoyer unicast au rover
        r = esp_now_send(ROVER_MAC, (uint8_t*)msg, strlen(msg)+1);
    } else {
        // Rover : envoyer broadcast
        r = esp_now_send(BCAST, (uint8_t*)msg, strlen(msg)+1);
    }
    Serial.printf("[SEND] %s [%s]\n", msg, r == ESP_OK ? "OK" : "FAIL");
    delay(500);
}
