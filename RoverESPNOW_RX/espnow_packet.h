#pragma once
#include <stdint.h>

typedef struct __attribute__((packed)) {
    int8_t  x;    // -100 (gauche) .. 0 .. +100 (droite)
    int8_t  y;    // -100 (arrière) .. 0 .. +100 (avant)
    uint8_t srv;  // 0..180 degrés
} espnow_packet_t;
