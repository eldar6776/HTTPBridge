# ANALIZA KOLIZIJE HEX KODOVA - ESP32 vs IC Kontroler

**Datum:** 15. Januar 2026  
**Izvor:** `common.h` (IC Kontroler) vs `main.cpp` (ESP32 HTTP Bridge)

---

## KOMANDE PREMA common.h (IC Kontroler - DEFINITIVNO)

Ove hex kodove **NE SMIJEMO MIJENJATI** jer ih IC kontroler očekuje:

| Hex | Komanda IC | Opis |
|-----|-----------|------|
| 0xAC | GET_ROOM_TEMP | Čitanje temperature sobe |
| 0xB1 | PINS | Kontrola pinova (grupno) |
| 0xB2 | READ_PINS | Čitanje stanja pinova |
| 0xB4 | SET_THST_ON | Paljenje termostata |
| 0xB5 | GET_FAN_DIFFERENCE | Čitanje razlike ventilatora |
| 0xB6 | GET_FAN_BAND | Čitanje opsega ventilatora |
| 0xC0 | RESTART_CTRL | Restart kontrolera |
| 0xCB | THERMOSTAT_CHANGE_ALL | Promjena svih parametara termostata |
| 0xCE | HOTEL_READ_LOG | Čitanje logova |
| 0xCF | HOTEL_DELETE_LOG | Brisanje logova |
| 0xD0 | GUEST_TEMPERATURE_IN | Guest temp in |
| 0xD1 | GUEST_TEMPERATURE_OUT | Guest temp out |
| 0xD5 | SET_RTC_DATE_TIME | Postavljanje RTC vremena |
| 0xD6 | SET_ROOM_TEMP | Postavljanje temperature sobe |
| 0xD7 | GET_GUEST_IN_TEMP | Čitanje guest in temp |
| 0xD8 | GET_GUEST_OUT_TEMP | Čitanje guest out temp |
| 0xDB | HOTEL_SET_PIN_V2 | Napredno postavljanje pina |
| 0xDC | HOTEL_GET_PIN / SET_THST_HEATING | **KONFLIKT!** |
| 0xDD | SET_THST_COOLING | Hlađenje |
| 0xDE | SET_THST_OFF | Isključivanje termostata |
| 0xDF | SET_PASSWORD | Postavljanje lozinke |
| 0xE1 | GET_PASSWORD | Čitanje lozinke |
| 0xFE | SET_PIN (254) | Direktno setovanje pina |

---

## TRENUTNE ESP32 KOMANDE (main.cpp)

Komande koje ESP32 koristi:

| Hex | ESP32 Komanda | Namjena | Kolizija? |
|-----|---------------|---------|-----------|
| 0xA0 | CMD_ESP_GET_PINS | ESP32 lokalno | ✅ OK |
| 0xA1 | CMD_ESP_SET_PIN | ESP32 lokalno | ✅ OK |
| 0xA2 | CMD_ESP_RESET_PIN | ESP32 lokalno | ✅ OK |
| 0xA3 | CMD_ESP_PULSE_PIN | ESP32 lokalno | ✅ OK |
| 0xAA | CMD_GET_STATUS | ESP32 lokalno | ✅ OK |
| 0xAC | CMD_GET_ROOM_TEMP | **→ IC** | ✅ POKLAPANJE |
| 0xB1 | CMD_SET_PIN | **→ IC** | ✅ POKLAPANJE |
| 0xB2 | CMD_GET_PINS | **→ IC** | ✅ POKLAPANJE |
| 0xB4 | CMD_SET_THST_ON | **→ IC** | ✅ POKLAPANJE |
| 0xB5 | CMD_GET_FAN_DIFFERENCE | **→ IC** | ✅ POKLAPANJE |
| 0xB6 | CMD_GET_FAN_BAND | **→ IC** | ✅ POKLAPANJE |
| 0xC0 | CMD_RESTART_CTRL | **→ IC** | ✅ POKLAPANJE |
| 0xD0 | CMD_SET_GUEST_IN_TEMP | **→ IC** | ✅ POKLAPANJE |
| 0xD1 | CMD_SET_GUEST_OUT_TEMP | **→ IC** | ✅ POKLAPANJE |
| 0xD6 | CMD_SET_ROOM_TEMP | **→ IC** | ✅ POKLAPANJE |
| 0xD7 | CMD_GET_GUEST_IN_TEMP | **→ IC** | ✅ POKLAPANJE |
| 0xD8 | CMD_GET_GUEST_OUT_TEMP | **→ IC** | ✅ POKLAPANJE |
| 0xDB | CMD_OPEN_DOOR | **→ IC** | ✅ POKLAPANJE |
| 0xDC | CMD_SET_THST_HEATING | **→ IC** | ✅ POKLAPANJE |
| 0xDD | CMD_SET_THST_COOLING | **→ IC** | ✅ POKLAPANJE |
| 0xDE | CMD_SET_THST_OFF | **→ IC** | ✅ POKLAPANJE |
| 0xDF | CMD_SET_PASSWORD | **→ IC** | ✅ POKLAPANJE |
| **0xE0** | **CMD_GET_SSID_PSWRD** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xE1** | **CMD_SET_SSID_PSWRD** | **ESP32 lokalno** | ❌ **KOLIZIJA sa GET_PASSWORD!** |
| **0xE2** | **CMD_GET_MDNS_NAME** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xE3** | **CMD_SET_MDNS_NAME** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xE4** | **CMD_GET_TCPIP_PORT** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xE5** | **CMD_SET_TCPIP_PORT** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xE6** | **CMD_GET_IP_ADDRESS** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xE7** | **CMD_RESTART** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xE8** | **CMD_GET_TIMER** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xE9** | **CMD_SET_TIMER** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xEA** | **CMD_GET_TIME** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xEB** | **CMD_SET_TIME** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xEC** | **CMD_OUTDOOR_LIGHT_ON** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xED** | **CMD_OUTDOOR_LIGHT_OFF** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xEE** | **CMD_GET_PINGWDG** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xEF** | **CMD_PINGWDG_ON** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xF0** | **CMD_PINGWDG_OFF** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xF1** | **CMD_TH_SETPOINT** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xF2** | **CMD_TH_DIFF** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xF3** | **CMD_TH_STATUS** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xF4** | **CMD_TH_HEATING** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xF5** | **CMD_TH_COOLING** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xF6** | **CMD_TH_OFF** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xF7** | **CMD_TH_ON** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |
| **0xF8** | **CMD_TH_EMA** | **ESP32 lokalno** | ⚠️ **SLOBODNO** |

---

## IDENTIFIKOVANE KOLIZIJE

### 🔴 KRITIČNA KOLIZIJA

**0xE1** - Koristi se za DVE različite stvari:
1. **IC Kontroler:** `GET_PASSWORD` (prema common.h)
2. **ESP32:** `CMD_SET_SSID_PSWRD` (lokalna WiFi konfiguracija)

**RJEŠENJE:** ESP32 lokalne komande (WiFi, Timer, Outdoor Light, itd.) **MORAMO PREMJESTITI** na sigurne hex kodove koji se ne koriste za RS485 komunikaciju.

---

## PRIJEDLOG RJEŠENJA

### Strategija:
1. **IC kontroler komande (0xAC-0xE1) OSTAJU NETAKNUTE**
2. **ESP32 lokalne komande premještamo na opseg 0x50-0x7F** (garantovano slobodan)

### Nova alokacija za ESP32 lokalne komande:

| Stari Hex | Nova Hex | Komanda | Tip |
|-----------|----------|---------|-----|
| 0xE0 | **0x50** | CMD_GET_SSID_PSWRD | ESP32 WiFi |
| 0xE1 | **0x51** | CMD_SET_SSID_PSWRD | ESP32 WiFi |
| 0xE2 | **0x52** | CMD_GET_MDNS_NAME | ESP32 Network |
| 0xE3 | **0x53** | CMD_SET_MDNS_NAME | ESP32 Network |
| 0xE4 | **0x54** | CMD_GET_TCPIP_PORT | ESP32 Network |
| 0xE5 | **0x55** | CMD_SET_TCPIP_PORT | ESP32 Network |
| 0xE6 | **0x56** | CMD_GET_IP_ADDRESS | ESP32 Network |
| 0xE7 | **0x57** | CMD_RESTART | ESP32 System |
| 0xE8 | **0x58** | CMD_GET_TIMER | ESP32 Timer |
| 0xE9 | **0x59** | CMD_SET_TIMER | ESP32 Timer |
| 0xEA | **0x5A** | CMD_GET_TIME | ESP32 RTC |
| 0xEB | **0x5B** | CMD_SET_TIME | ESP32 RTC |
| 0xEC | **0x5C** | CMD_OUTDOOR_LIGHT_ON | ESP32 Light |
| 0xED | **0x5D** | CMD_OUTDOOR_LIGHT_OFF | ESP32 Light |
| 0xEE | **0x5E** | CMD_GET_PINGWDG | ESP32 Watchdog |
| 0xEF | **0x5F** | CMD_PINGWDG_ON | ESP32 Watchdog |
| 0xF0 | **0x60** | CMD_PINGWDG_OFF | ESP32 Watchdog |
| 0xF1 | **0x61** | CMD_TH_SETPOINT | ESP32 Thermostat |
| 0xF2 | **0x62** | CMD_TH_DIFF | ESP32 Thermostat |
| 0xF3 | **0x63** | CMD_TH_STATUS | ESP32 Thermostat |
| 0xF4 | **0x64** | CMD_TH_HEATING | ESP32 Thermostat |
| 0xF5 | **0x65** | CMD_TH_COOLING | ESP32 Thermostat |
| 0xF6 | **0x66** | CMD_TH_OFF | ESP32 Thermostat |
| 0xF7 | **0x67** | CMD_TH_ON | ESP32 Thermostat |
| 0xF8 | **0x68** | CMD_TH_EMA | ESP32 Thermostat |

### Dodaj nove IC komande:

| Hex | Komanda | Opis |
|-----|---------|------|
| **0xE1** | **CMD_GET_PASSWORD** | **Čitanje lozinke sa IC kontrolera** |

---

## IMPLEMENTACIJA - PLAN IZMJENA

### Faza 1: Ažuriraj enum CommandType u main.cpp

```cpp
enum CommandType
{
  CMD_UNKNOWN,
  // ESP32 lokalne komande (interni pinovi, senzori)
  CMD_ESP_GET_PINS = 0xA0,
  CMD_ESP_SET_PIN = 0xA1,
  CMD_ESP_RESET_PIN = 0xA2,
  CMD_ESP_PULSE_PIN = 0xA3,
  
  // ESP32 status
  CMD_GET_STATUS = 0xAA,
  
  // IC Kontroler komande (RS485) - NE MIJENJATI!
  CMD_GET_ROOM_TEMP = 0xAC,
  CMD_SET_PIN = 0xB1,
  CMD_GET_PINS = 0xB2,
  CMD_SET_THST_ON = 0xB4,
  CMD_GET_FAN_DIFFERENCE = 0xB5,
  CMD_GET_FAN_BAND = 0xB6,
  CMD_RESTART_CTRL = 0xC0,
  CMD_SET_GUEST_IN_TEMP = 0xD0,
  CMD_SET_GUEST_OUT_TEMP = 0xD1,
  CMD_SET_ROOM_TEMP = 0xD6,
  CMD_GET_GUEST_IN_TEMP = 0xD7,
  CMD_GET_GUEST_OUT_TEMP = 0xD8,
  CMD_OPEN_DOOR = 0xDB,            // HOTEL_SET_PIN_V2
  CMD_SET_THST_HEATING = 0xDC,
  CMD_SET_THST_COOLING = 0xDD,
  CMD_SET_THST_OFF = 0xDE,
  CMD_SET_PASSWORD = 0xDF,
  CMD_GET_PASSWORD = 0xE1,         // NOVO! IC kontroler
  
  // ESP32 lokalne komande - PREMJEŠTENO NA SIGURAN OPSEG
  CMD_GET_SSID_PSWRD = 0x50,
  CMD_SET_SSID_PSWRD = 0x51,
  CMD_GET_MDNS_NAME = 0x52,
  CMD_SET_MDNS_NAME = 0x53,
  CMD_GET_TCPIP_PORT = 0x54,
  CMD_SET_TCPIP_PORT = 0x55,
  CMD_GET_IP_ADDRESS = 0x56,
  CMD_RESTART = 0x57,
  CMD_GET_TIMER = 0x58,
  CMD_SET_TIMER = 0x59,
  CMD_GET_TIME = 0x5A,
  CMD_SET_TIME = 0x5B,
  CMD_OUTDOOR_LIGHT_ON = 0x5C,
  CMD_OUTDOOR_LIGHT_OFF = 0x5D,
  CMD_GET_PINGWDG = 0x5E,
  CMD_PINGWDG_ON = 0x5F,
  CMD_PINGWDG_OFF = 0x60,
  CMD_TH_SETPOINT = 0x61,
  CMD_TH_DIFF = 0x62,
  CMD_TH_STATUS = 0x63,
  CMD_TH_HEATING = 0x64,
  CMD_TH_COOLING = 0x65,
  CMD_TH_OFF = 0x66,
  CMD_TH_ON = 0x67,
  CMD_TH_EMA = 0x68
};
```

### Faza 2: Implementiraj SET_PASSWORD i GET_PASSWORD

Sada možemo sigurno implementirati ove komande jer je 0xE1 slobodan!

---

## CHECKLI STA ZA IMPLEMENTACIJU

- [ ] Ažuriraj enum CommandType sa novim hex kodovima
- [ ] Ažuriraj stringToCommand() funkciju
- [ ] Implementiraj CMD_SET_PASSWORD case
- [ ] Implementiraj CMD_GET_PASSWORD case
- [ ] Dodaj parsiranje odgovora za GET_PASSWORD
- [ ] Testiraj sa IC kontrolerom

---

**Kraj analize**
