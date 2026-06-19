# FSD — RQR1 : Télécommande ESP-NOW pour rover 4 roues

**Version :** 1.0  
**Date :** 2026-06-19  
**Matériel cible :** ESP32-C3 (TX / télécommande) + ESP32-WROOM32 (RX / rover)  
**Protocole radio :** ESP-NOW (802.11 LR, peer-to-peer, sans réseau WiFi)

---

## 1. Vue d'ensemble du système

```
┌──────────────────────────────┐          ESP-NOW (~10 ms)         ┌──────────────────────────────────┐
│   TÉLÉCOMMANDE — ESP32-C3    │  ─────────────────────────────►  │   ROVER — ESP32-WROOM32          │
│                              │                                    │                                  │
│  GPIO 2 → joystick G/D       │     struct espnow_packet_t {      │  IN1=26 IN2=27  ENA=14 (G)       │
│  GPIO 3 → joystick AV/AR     │       int8_t  x;   // -100..+100  │  IN3=12 IN4=33  ENB=32 (D)       │
│  GPIO 4 → potentiomètre servo│       int8_t  y;   // -100..+100  │  SERVO → GPIO 13  (50 Hz PWM)    │
│                              │       uint8_t srv; // 0..180 °    │                                  │
└──────────────────────────────┘     }                             └──────────────────────────────────┘
```

Le rover conserve le même pinout que `RoverWifiL298.ino`. Seul le canal de commande change : WiFi + HTTP → ESP-NOW.

---

## 2. Matériel

### 2.1 Télécommande — ESP32-C3

| GPIO | Rôle                      | Connexion recommandée                          |
|------|---------------------------|------------------------------------------------|
| 2    | Axe X — Gauche / Droite   | Sortie centrale du joystick (potentiomètre) → ADC1_CH2 |
| 3    | Axe Y — Avant / Arrière   | Sortie centrale du joystick (potentiomètre) → ADC1_CH3 |
| 4    | Commande servo (position) | Potentiomètre ou curseur → ADC1_CH4            |

- Alimentation joysticks / potentiomètre : 3,3 V — GND (ESP32-C3 → tolérance 3,3 V uniquement).
- Position repos joystick X : ~1,65 V → valeur ADC ~2047 (12 bits).
- Position repos joystick Y : ~1,65 V → valeur ADC ~2047 (12 bits).

### 2.2 Rover — ESP32-WROOM32 (pinout conservé)

| GPIO | Rôle               | Composant  |
|------|--------------------|------------|
| 26   | IN1 — sens M. gauche | L298N    |
| 27   | IN2 — sens M. gauche | L298N    |
| 14   | ENA — PWM M. gauche  | L298N    |
| 12   | IN3 — sens M. droit  | L298N    |
| 33   | IN4 — sens M. droit  | L298N    |
| 32   | ENB — PWM M. droit   | L298N    |
| 13   | Signal servo         | Servo (50 Hz, 1–2 ms) |

> GPIO 13 est libre dans le projet actuel ; tout autre GPIO non utilisé peut être substitué si le câblage l'exige.

---

## 3. Structure de données ESP-NOW

```c
// Partagée entre TX et RX (même fichier d'en-tête espnow_packet.h)
typedef struct __attribute__((packed)) {
    int8_t  x;    // Axe gauche/droite  : -100 (gauche) → 0 (centre) → +100 (droite)
    int8_t  y;    // Axe avant/arrière  : -100 (arrière) → 0 (stop)  → +100 (avant)
    uint8_t srv;  // Position servo (°) :    0                          →        180
} espnow_packet_t;  // 3 octets
```

- Fréquence d'envoi TX : **10 ms** (100 Hz) en `loop()` non-bloquant.
- Si le rover ne reçoit plus de paquet depuis **200 ms** → `rover_stop()` (sécurité watchdog).

---

## 4. Télécommande — logique TX (`RoverESPNOW_TX`)

### 4.1 Lecture ADC et normalisation

```
ADC 12 bits → [0..4095]
dead-zone centrale ±100 pts (évite la dérive au repos)

axe_x = map(adc_gpio2, 0, 4095, -100, +100)
axe_y = map(adc_gpio3, 0, 4095, -100, +100)
servo = map(adc_gpio4, 0, 4095, 0, 180)

si abs(axe_x) < 5 → axe_x = 0   // zone morte
si abs(axe_y) < 5 → axe_y = 0
```

### 4.2 Initialisation ESP-NOW TX

```
WiFi.mode(WIFI_STA)
esp_now_init()
esp_now_register_send_cb(onDataSent)
esp_now_add_peer(rover_mac, ESP_NOW_ROLE_CONTROLLER, 1, NULL, 0)
```

- L'adresse MAC du rover est codée en dur dans le firmware TX (ou stockée en NVS).
- Canal WiFi fixé à **1** sur les deux cartes.

---

## 5. Rover — logique RX (`RoverESPNOW_RX`)

### 5.1 Initialisation ESP-NOW RX

```
WiFi.mode(WIFI_STA)
esp_now_init()
esp_now_register_recv_cb(onDataRecv)
```

### 5.2 Commande différentielle

À chaque réception de paquet :

```
pwm_base  = map(|y|, 0, 100, 0, 255)   // vitesse longitudinale
pwm_turn  = map(|x|, 0, 100, 0, 255)   // composante de virage

si y > 0 (avant) :
    motor_L_dir = +1,  motor_R_dir = +1
    motor_L_pwm = clamp(pwm_base + pwm_turn_L, 0, 255)
    motor_R_pwm = clamp(pwm_base + pwm_turn_R, 0, 255)

    si x > 0 (droite) : pwm_turn_L = +pwm_turn, pwm_turn_R = -pwm_turn
    si x < 0 (gauche) : pwm_turn_L = -pwm_turn, pwm_turn_R = +pwm_turn

si y < 0 (arrière) : même logique, dirs inversés

si y == 0 et x != 0 : rotation sur place (M.gauche ↔ M.droit en opposition)

si x == 0 et y == 0 : rover_stop()
```

Cette logique est une extension de `rover_left()` / `rover_right()` existants — les fonctions `setMotorLeft()` et `setMotorRight()` sont **réutilisées sans modification**.

### 5.3 Commande servo

```
ledcAttach(SERVO_PIN, 50, 16)          // 50 Hz, résolution 16 bits
duty = map(srv, 0, 180, 1638, 3277)   // 1 ms → 2 ms sur période 20 ms (65535 steps)
ledcWrite(SERVO_PIN, duty)
```

### 5.4 Watchdog de sécurité

```c
// Dans loop(), vérifié toutes les 50 ms
if (millis() - last_packet_ms > 200) {
    rover_stop();
    ledcWrite(SERVO_PIN, SERVO_CENTER_DUTY);
}
```

---

## 6. Structure des fichiers

```
RQR1/
├── RoverWifiL298/
│   └── RoverWifiL298.ino          (existant — non modifié)
│
├── RoverESPNOW_TX/                (télécommande ESP32-C3)
│   ├── RoverESPNOW_TX.ino
│   └── espnow_packet.h
│
├── RoverESPNOW_RX/                (rover ESP32-WROOM32)
│   ├── RoverESPNOW_RX.ino
│   └── espnow_packet.h            (copie identique)
│
└── FSD_RQR1.md                    (ce document)
```

---

## 7. Configuration PWM — récapitulatif

| Canal          | GPIO | Fréquence | Résolution | Plage duty         |
|----------------|------|-----------|------------|--------------------|
| Moteur gauche  | 14   | 1 000 Hz  | 8 bits     | 0 – 255            |
| Moteur droit   | 32   | 1 000 Hz  | 8 bits     | 0 – 255            |
| Servo          | 13   | 50 Hz     | 16 bits    | 1 638 – 3 277      |

API utilisée : `ledcAttach(pin, freq, res)` + `ledcWrite(pin, duty)` — compatible ESP32 Arduino core v3.x (identique à `RoverWifiL298.ino`).

---

## 8. Contraintes et limites

| Paramètre              | Valeur              | Remarque                                         |
|------------------------|---------------------|--------------------------------------------------|
| Portée ESP-NOW typ.    | ~100 m (champ libre)| Réduite en intérieur / obstacles                 |
| Latence TX→RX          | < 5 ms              | Sans gestion WiFi concurrente                    |
| Watchdog arrêt         | 200 ms              | Tolérance à une perte de 20 paquets successifs   |
| Tension ADC ESP32-C3   | 0 – 3,3 V           | Ne pas dépasser 3,3 V sur GPIO 2/3/4             |
| Courant servo          | À alimenter séparément | Ne pas alimenter par le 3,3 V ESP32           |

---

## 9. Points d'attention implémentation

1. **Adresse MAC du rover** : récupérer avec `WiFi.macAddress()` sur le rover et la fixer en dur dans `RoverESPNOW_TX.ino`.
2. **Canal WiFi** : imposer `WiFi.channel(1)` des deux côtés avant `esp_now_init()`.
3. **Auto-test moteurs** : conservé au démarrage du rover (issu de `RoverWifiL298.ino`).
4. **Zone morte joystick** : régler empiriquement selon le potentiomètre utilisé (±5 % recommandé).
5. **Servo à centrer** au démarrage et sur watchdog — évite un débattement intempestif.
