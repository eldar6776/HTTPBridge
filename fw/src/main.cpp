#include <WiFi.h>
#include <Update.h>
#include <time.h>
#include <sys/time.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <cstring>
#include <Ticker.h>
#include <SunSet.h>
#include <ArduinoJson.h>
#include <IRac.h>
#include <IRutils.h>
#include "esp_task_wdt.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "ExternalFlash.h"
#include "FirmwareUpdateService.h"
#include "LogMacros.h"

extern "C"
{
#include "TinyFrame.h"
}

// Helper function for standard CRC32 calculation
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    while (len--) {
        crc ^= *data++;
        for (int k = 0; k < 8; k++) {
            crc = crc & 1 ? (crc >> 1) ^ 0xedb88320 : crc >> 1;
        }
    }
    return ~crc;
}

extern "C"
{
#include "TinyFrame.h"
}

#include "version.h"

// PIN definicije - AŽURIRANO ZA SPI FLASH
#define BOOT_PIN      0   // Boot dugme / WiFi Reset (ulaz)
#define LED_PIN       2   // Onboard LED (Status)
#define RS485_DE_PIN  4   // RS485 Transmit Enable

// SPI Flash pinovi (VSPI bus)
#define FLASH_CS      5   // SPI Flash Chip Select
#define FLASH_SCK     18  // SPI Flash Serial Clock
#define FLASH_MISO    19  // SPI Flash Data Out (fleš->ESP)
#define FLASH_MOSI    23  // SPI Flash Data In (ESP->fleš)

// VSPI instance for External Flash
SPIClass vspi(VSPI);

// Objects for Firmware Update
ExternalFlash extFlash(FLASH_CS, vspi);
// updateService moved below tfapp DEFINITION

// Wrapper for TinyFrame listener - forward declaration
TF_Result UpdateService_Listener(TinyFrame *tf, TF_Msg *msg);

// OneWire senzori - PREMJEŠTENI sa GPIO 5 i 18 radi SPI Flash
#define ONE_WIRE_PIN  13  // OneWire senzor 1 (bio na GPIO 5)
#define ONE_WIRE2_PIN 14  // OneWire senzor 2 (bio na GPIO 18)

#define IR_PIN        21 // IR Transmitter Pin

// Termostat i Ventilator (ostaje nepromijenjeno)
#define FAN_L         25  // Ventilator brzina 1 (Low)
#define FAN_M         26  // Ventilator brzina 2 (Medium)
#define FAN_H         27  // Ventilator brzina 3 (High)
#define LIGHT_PIN     32  // Vanjska rasvjeta
#define VALVE         33  // Termo ventil (Pumpa)
#define SET_RTC_DATE_TIME 213
#define DEF_TFBRA 255 // default broadcast address
#define S_CUSTOM 23
#define S_IR     20   // IR remote data za klima uređaj
#define S_SOS    40   // SOS signal iz toaleta (emergency button)


IRac ac(IR_PIN);

#define WDT_TIMEOUT 5
#define MAX_PING_FAILURES 10
#define PING_INTERVAL_MS 60000
#define READ_INTERVAL 10000 // refresh senora temperature
#define LATITUDE 43.8563
#define LONGITUDE 18.4131
#define TIMEZONE "CET-1CEST,M3.5.0/2,M10.5.0/3"
#define MAX_PULSE_PINS 16
#define HOLD_TIME 5000 // 5 sekundi WiFi reset putem dugmeta (BOOT dugme)


char _ssid[64] = "";
char _pass[64] = "";
char _mdns[64] = "";
char _resp[512] = "";
String timerOnType = "OFF", timerOffType = "OFF";
String timerOnTime = "0000", timerOffTime = "0000";

bool lightState = false;
bool tempSensorAvailable = false;
bool tempSensor2Available = false;
int _port = 80;
bool timeValid = false;
bool overrideActive = false;
bool overrideState = false;
bool lastTimerState = false;  // Prethodno stanje timera (za detekciju promene)
int rdy, replyDataLength;
volatile bool httpHandlerWaiting = false;  // Flag za blokiranje loop() čitanja
bool pingWatchdogEnabled = false;
unsigned long lastPingTime = 0;
int pingFailures = 0;
unsigned long lastReadTime = 0;

// ========== GET_STATUS WATCHDOG (Hardware Timer) ==========
#define CMD_GET_STATUS_TIMEOUT_SEC 600 // 10 minuta u sekundama
hw_timer_t *getStatusWatchdog = NULL;
volatile bool getStatusWatchdogTriggered = false;

// Timer callback - poziva se kada istekne 10 minuta bez GET_STATUS
void IRAM_ATTR onGetStatusTimeout() {
  getStatusWatchdogTriggered = true;
}

// Funkcija za reset watchdog tajmera (poziva se svaki put kad dođe GET_STATUS)
void resetGetStatusWatchdog() {
  if (getStatusWatchdog != NULL) {
    timerWrite(getStatusWatchdog, 0); // Reset timer counter na 0
    LOG_DEBUG("[Watchdog] Timer reset\n");
  }
}

// Funkcija za inicijalizaciju watchdog tajmera
void initGetStatusWatchdog() {
  // Timer 0, prescaler 80 (1 MHz clock), counting up
  getStatusWatchdog = timerBegin(0, 80, true);
  
  // Attach callback funkciju
  timerAttachInterrupt(getStatusWatchdog, &onGetStatusTimeout, true);
  
  // Postavi alarm na 600 sekundi (10 minuta)
  // 1 MHz clock -> 1,000,000 ticks = 1 sekunda
  timerAlarmWrite(getStatusWatchdog, CMD_GET_STATUS_TIMEOUT_SEC * 1000000ULL, false);
  
  // Pokreni timer
  timerAlarmEnable(getStatusWatchdog);
  
  LOG_INFO("[Watchdog] Initialized: %d seconds timeout\n", CMD_GET_STATUS_TIMEOUT_SEC);
}
// ===========================================================

float fluid = 0.0;          // Fluid temperature
float emaTemperature = 0.0; // EMA filter
float emaAlpha = 0.2;       // podesiv u prefs
float th_setpoint = 25.0;   // Termostat varijable
float th_treshold = 0.5;    // osnovni prag
int currentFanLevel = 0;    // Globalna varijabla, čuva trenutno aktivnu brzinu: 0 = off, 1 = L, 2 = M, 3 = H
uint8_t replyData[64] = {0};

// SOS i IR status varijable (primljeni događaji sa toalet uređaja)
bool sosStatus = false;        // SOS signal aktivan
unsigned long sosTimestamp = 0; // Vrijeme kada je SOS primljen
struct IRData {
  uint8_t th_ctrl = 0;   // 0=OFF, 1=COOLING, 2=HEATING
  uint8_t th_state = 0;  // 0=OFF, 1=ON
  uint16_t mv_temp = 0;  // Mjerena temperatura (x100)
  uint8_t mv_offset = 0; // Offset mjerene temperature
  uint8_t sp_temp = 0;   // Setpoint temperatura
  uint8_t fan_ctrl = 0;  // Fan kontrola
  uint8_t fan_speed = 0; // Fan brzina
  bool received = false; // Flag da je primljen novi IR paket
  unsigned long timestamp = 0;
} irData;

std::unique_ptr<AsyncWebServer> server;
// Lista pinova koje već koristiš ili koji su hardverski rizični
const int usedPins[] = { // NOLINT(cert-err58-cpp)
    BOOT_PIN,           // GPIO 0 - Boot dugme
    1,                  // UART0 TX (Serial Monitor)
    LED_PIN,            // GPIO 2 - Onboard LED
    3,                  // UART0 RX (Serial Monitor)
    RS485_DE_PIN,       // GPIO 4 - RS485 Direction Enable
    FLASH_CS,           // GPIO 5 - SPI Flash CS
    6, 7, 8, 9, 10, 11, // Povezani sa internim SPI flash – NE KORISTITI!
    ONE_WIRE_PIN,       // GPIO 13 - OneWire senzor 1
    ONE_WIRE2_PIN,      // GPIO 14 - OneWire senzor 2
    16,                 // Serial2 RX2 (RS485)
    17,                 // Serial2 TX2 (RS485)
    FLASH_SCK,          // GPIO 18 - SPI Flash SCK
    FLASH_MISO,         // GPIO 19 - SPI Flash MISO
    20,                 // Ne postoji na većini ESP32 modula
    IR_PIN,            // GPIO 21 - IR Transmitter
    22,                 // Ne postoji 
    FLASH_MOSI,         // GPIO 23 - SPI Flash MOSI
    24,                 // Ne postoji
    FAN_L, FAN_M, FAN_H, // GPIO 25, 26, 27 - Ventilator
    28, 29, 30, 31,     // Ne postoje
    LIGHT_PIN,          // GPIO 32 - Vanjska rasvjeta
    VALVE,              // GPIO 33 - Termo ventil
    37, 38, 39          // Samo ulaz (ADC), nema OUTPUT funkciju
};

const int numUsedPins = sizeof(usedPins) / sizeof(usedPins[0]);

struct PulsePinState
{
  int pin = -1;
  unsigned long pulseStart = 0;
  unsigned long duration = 2000; // default 2000 ms
  bool active = false;
};

enum ThMode
{
  TH_OFF = 0,
  TH_HEATING,
  TH_COOLING
};

enum LedState
{
  LED_OFF,
  LED_ON,
  LED_SLOW,
  LED_FAST
};

ThMode th_mode = TH_OFF;
ThMode th_saved_mode = TH_COOLING; // defaultni režim koji se pamti prije OFF
LedState led_state = LED_SLOW;
Ticker lightTicker;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature sensors(&oneWire);
OneWire oneWire2(ONE_WIRE2_PIN);
DallasTemperature sensors2(&oneWire2);
PulsePinState pulseStates[MAX_PULSE_PINS];
SunSet sun;
Ticker rtcSyncTicker;
Preferences preferences;
TinyFrame tfapp;
FirmwareUpdateService updateService(extFlash, tfapp); // Initialized here now

// Implementation of wrapper
TF_Result UpdateService_Listener(TinyFrame *tf, TF_Msg *msg) {
  return updateService.handlePacket(tf, msg);
}

WiFiManager wm; // NOLINT(cert-err58-cpp)
const char *update_form_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>OTA Firmware Update</title>
</head>
<body>
  <h2>OTA Firmware Update</h2>
  <form method="POST" action="/update" enctype="multipart/form-data">
    <input type="file" name="firmware">
    <input type="submit" value="Upload & Update">
  </form>
</body>
</html>
)rawliteral";
/**
 * ENUMERATOR PODRZANIH KOMANDI
 * 
 * IC KONTROLER KOMANDE (0xAC-0xE1) - NE MIJENJATI! Definisano u common.h
 * ESP32 LOKALNE KOMANDE (0x50-0x68) - Interni, korisnik ih ne vidi
 */
enum CommandType
{
  CMD_UNKNOWN,
  CMD_GET_ROOM_STATUS = 0x94,      // IC kontroler - Get Room Status (Card Stacker)
  // ESP32 lokalne komande - Interni pinovi i senzori
  CMD_ESP_GET_PINS = 0xA0,
  CMD_ESP_SET_PIN = 0xA1,
  CMD_ESP_RESET_PIN = 0xA2,
  CMD_ESP_PULSE_PIN = 0xA3,
  CMD_GET_STATUS = 0xAA,
  
  // IC Kontroler komande - RS485 (common.h) - NE MIJENJATI!
  CMD_GET_ROOM_TEMP = 0xAC,
  CMD_SET_PIN = 0xB1,
  CMD_GET_PINS = 0xB2,
  CMD_SET_THST_ON = 0xB4,
  CMD_GET_FAN_DIFFERENCE = 0xB5,
  CMD_GET_FAN_BAND = 0xB6,
  CMD_RESTART_CTRL = 0xC0,
  CMD_READ_LOG = 0xCE,             // IC kontroler - Read last log
  CMD_DELETE_LOG = 0xCF,           // IC kontroler - Delete last log
  CMD_SET_GUEST_IN_TEMP = 0xD0,
  CMD_SET_GUEST_OUT_TEMP = 0xD1,
  CMD_SET_ROOM_TEMP = 0xD6,
  CMD_GET_GUEST_IN_TEMP = 0xD7,
  CMD_GET_GUEST_OUT_TEMP = 0xD8,
  CMD_OPEN_DOOR = 0xDB,            // HOTEL_SET_PIN_V2
  CMD_SET_THST_HEATING = 0xDC,
  CMD_SET_THST_COOLING = 0xDD,
  CMD_SET_THST_OFF = 0xDE,
  CMD_SET_PASSWORD = 0x96,

  CMD_QR_CODE_GET = 0xE5,          // IC kontroler - Get QR Code
  CMD_QR_CODE_SET = 0xE6,          // IC kontroler - Set QR Code
  CMD_SET_LANG = 0xE9,             // IC kontroler - Set Language
  CMD_GET_SYSID = 0xEA,            // IC kontroler - Get System ID
  CMD_SET_SYSID = 0xEB,            // IC kontroler - Set System ID
  CMD_GET_PASSWORD = 0xEC,         // IC kontroler
  CMD_SET_FWD_HEATING = 0xED,      // IC kontroler - Set Forward Heating Flag
  CMD_SET_FWD_COOLING = 0xEE,      // IC kontroler - Set Forward Cooling Flag
  CMD_SET_ENABLE_HEATING = 0xEF,   // IC kontroler - Set Enable Heating Flag
  CMD_SET_ENABLE_COOLING = 0xF0,   // IC kontroler - Set Enable Cooling Flag
  CMD_GET_VERSION = 0xF1,          // IC kontroler / ESP32 - Get Firmware Versions
  // ESP32 lokalne komande - Premješteno na siguran opseg (0x50-0x68)
  
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
  CMD_TH_EMA = 0x68,
  CMD_SOS_RESET = 0x69,  // Resetuje SOS status nakon što je hitnost riješena
  CMD_SET_IR_PROTOCOL = 0x70, // Set IR Protocol ID
  CMD_GET_IR_PROTOCOL = 0x71, // Get IR Protocol ID
  CMD_SET_IR = 0x72           // Send basic IR command (ON/OFF, Mode, Temp)

};
/**
 * KONVERTOR HTTP KOMANDI U ENUMERATOR
 */
CommandType stringToCommand(const String &cmd)
{
  if (cmd == "RESTART")
    return CMD_RESTART;
  if (cmd == "RESTART_CTRL")
    return CMD_RESTART_CTRL;
  if (cmd == "GET_STATUS")
    return CMD_GET_STATUS;
  if (cmd == "GET_SSID_PSWRD")
    return CMD_GET_SSID_PSWRD;
  if (cmd == "SET_SSID_PSWRD")
    return CMD_SET_SSID_PSWRD;
  if (cmd == "GET_MDNS_NAME")
    return CMD_GET_MDNS_NAME;
  if (cmd == "SET_MDNS_NAME")
    return CMD_SET_MDNS_NAME;
  if (cmd == "GET_TCPIP_PORT")
    return CMD_GET_TCPIP_PORT;
  if (cmd == "SET_TCPIP_PORT")
    return CMD_SET_TCPIP_PORT;
  if (cmd == "GET_ROOM_TEMP")
    return CMD_GET_ROOM_TEMP;
  if (cmd == "ESP_GET_PINS")
    return CMD_ESP_GET_PINS;
  if (cmd == "ESP_SET_PIN")
    return CMD_ESP_SET_PIN;
  if (cmd == "ESP_RESET_PIN")
    return CMD_ESP_RESET_PIN;
  if (cmd == "ESP_PULSE_PIN")
    return CMD_ESP_PULSE_PIN;
  if (cmd == "GET_PINS")
    return CMD_GET_PINS;
  if (cmd == "SET_PIN")
    return CMD_SET_PIN;
  if (cmd == "OPEN_DOOR")
    return CMD_OPEN_DOOR;
  if (cmd == "SET_THST_ON")
    return CMD_SET_THST_ON;
  if (cmd == "GET_FAN_DIFFERENCE")
    return CMD_GET_FAN_DIFFERENCE;
  if (cmd == "GET_FAN_BAND")
    return CMD_GET_FAN_BAND;
  if (cmd == "SET_GUEST_IN_TEMP")
    return CMD_SET_GUEST_IN_TEMP;
  if (cmd == "SET_GUEST_OUT_TEMP")
    return CMD_SET_GUEST_OUT_TEMP;
  if (cmd == "SET_ROOM_TEMP")
    return CMD_SET_ROOM_TEMP;
  if (cmd == "GET_GUEST_IN_TEMP")
    return CMD_GET_GUEST_IN_TEMP;
  if (cmd == "GET_GUEST_OUT_TEMP")
    return CMD_GET_GUEST_OUT_TEMP;
  if (cmd == "SET_THST_HEATING")
    return CMD_SET_THST_HEATING;
  if (cmd == "SET_THST_COOLING")
    return CMD_SET_THST_COOLING;
  if (cmd == "SET_THST_OFF")
    return CMD_SET_THST_OFF;
  if (cmd == "SET_PASSWORD")
    return CMD_SET_PASSWORD;
  if (cmd == "GET_PASSWORD")
    return CMD_GET_PASSWORD;
  if (cmd == "READ_LOG")
    return CMD_READ_LOG;
  if (cmd == "DELETE_LOG")
    return CMD_DELETE_LOG;
  if (cmd == "GET_IP_ADDRESS")
    return CMD_GET_IP_ADDRESS;
  if (cmd == "GET_TIMER")
    return CMD_GET_TIMER;
  if (cmd == "SET_TIMER")
    return CMD_SET_TIMER;
  if (cmd == "GET_TIME")
    return CMD_GET_TIME;
  if (cmd == "SET_TIME")
    return CMD_SET_TIME;
  if (cmd == "OUTDOOR_LIGHT_ON")
    return CMD_OUTDOOR_LIGHT_ON;
  if (cmd == "OUTDOOR_LIGHT_OFF")
    return CMD_OUTDOOR_LIGHT_OFF;
  if (cmd == "GET_PINGWDG")
    return CMD_GET_PINGWDG;
  if (cmd == "PINGWDG_ON")
    return CMD_PINGWDG_ON;
  if (cmd == "PINGWDG_OFF")
    return CMD_PINGWDG_OFF;
  if (cmd == "TH_SETPOINT")
    return CMD_TH_SETPOINT;
  if (cmd == "TH_DIFF")
    return CMD_TH_DIFF;
  if (cmd == "TH_STATUS")
    return CMD_TH_STATUS;
  if (cmd == "TH_HEATING")
    return CMD_TH_HEATING;
  if (cmd == "TH_COOLING")
    return CMD_TH_COOLING;
  if (cmd == "TH_OFF")
    return CMD_TH_OFF;
  if (cmd == "TH_ON")
    return CMD_TH_ON;
  if (cmd == "TH_EMA")
    return CMD_TH_EMA;
  if (cmd == "SET_LANG")
    return CMD_SET_LANG;
  if (cmd == "GET_SYSID")
    return CMD_GET_SYSID;
  if (cmd == "SET_SYSID")
    return CMD_SET_SYSID;
  if (cmd == "QR_CODE_SET")
    return CMD_QR_CODE_SET;
  if (cmd == "QR_CODE_GET")
    return CMD_QR_CODE_GET;
  if (cmd == "GET_ROOM_STATUS")
    return CMD_GET_ROOM_STATUS;
  if (cmd == "SOS_RESET")
    return CMD_SOS_RESET;
  if (cmd == "SET_FWD_HEATING")
    return CMD_SET_FWD_HEATING;
  if (cmd == "SET_FWD_COOLING")
    return CMD_SET_FWD_COOLING;
  if (cmd == "SET_ENABLE_HEATING")
    return CMD_SET_ENABLE_HEATING;
  if (cmd == "SET_ENABLE_COOLING")
    return CMD_SET_ENABLE_COOLING;
  if (cmd == "GET_VERSION")
    return CMD_GET_VERSION;
  if (cmd == "SET_IR_PROTOCOL")
    return CMD_SET_IR_PROTOCOL;
  if (cmd == "GET_IR_PROTOCOL")
    return CMD_GET_IR_PROTOCOL;
  if (cmd == "SET_IR")
    return CMD_SET_IR;
  return CMD_UNKNOWN;
}
/**
 * HELPER FUNKCIJA - BCD to Decimal konverzija
 */
uint8_t bcdToDec(uint8_t bcd)
{
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}
/**
 * HELPER FUNKCIJA - Event code to event name
 */
String getEventName(uint8_t eventCode)
{
  if (eventCode == 0x90) return "SPI_DRIVER_ERROR";
  if (eventCode == 0x91) return "I2C_DRIVER_ERROR";
  if (eventCode == 0x92) return "USART_DRIVER_ERROR";
  if (eventCode == 0x93) return "RTC_DRIVER_ERROR";
  if (eventCode == 0x94) return "TIMER_DRIVER_ERROR";
  if (eventCode == 0x95) return "ETHERNET_DRIVER_ERROR";
  if (eventCode == 0x96) return "CRC_DRIVER_ERROR";
  if (eventCode == 0x97) return "ADC_DRIVER_ERROR";
  if (eventCode == 0x98) return "SYSTEM_CLOCK_ERROR";
  if (eventCode == 0x99) return "SYSTEM_EXCEPTION";
  if (eventCode == 0x9A) return "QSPI_DRIVER_ERROR";
  if (eventCode == 0x9B) return "FLASH_DRIVER_ERROR";
  if (eventCode == 0x9C) return "SOFTWARE_RESET";
  if (eventCode == 0xA0) return "CAPACITIVE_SENSOR_ERROR";
  if (eventCode == 0xA1) return "RC522_MIFARE_ERROR";
  if (eventCode == 0xA2) return "ONEWIRE_ERROR";
  if (eventCode == 0xA3) return "RS485_ERROR";
  if (eventCode == 0xA4) return "MAIN_FUNCTION_ERROR";
  if (eventCode == 0xA5) return "DISPLAY_ERROR";
  if (eventCode == 0xA6) return "LOGGER_ERROR";
  if (eventCode == 0xA7) return "DIO_ERROR";
  if (eventCode == 0xA8) return "EEPROM_ERROR";
  if (eventCode == 0xA9) return "ROOM_FUNCTION_ERROR";
  if (eventCode == 0xAA) return "TCPIP_STACK_FAILURE";
  if (eventCode == 0xAB) return "HOTEL_CTRL_FAILURE";
  if (eventCode == 0xAC) return "HTTPD_SERVER_FAILURE";
  if (eventCode == 0xAD) return "THERMOSTAT_FAILURE";
  if (eventCode == 0xAE) return "GENERAL_FAILURE";
  if (eventCode == 0xB0) return "FILE_SYSTEM_OK";
  if (eventCode == 0xB1) return "FILE_SYSTEM_DRIVE_ERROR";
  if (eventCode == 0xB2) return "FILE_SYSTEM_DIR_ERROR";
  if (eventCode == 0xB3) return "FILE_SYSTEM_FILE_ERROR";
  if (eventCode == 0xB4) return "OUT_OF_MEMORY";
  if (eventCode == 0xB5) return "ADDRESS_LIST_ERROR";
  if (eventCode == 0xB6) return "ADDRESS_LIST_SD_ERROR";
  if (eventCode == 0xB7) return "DEVICE_NOT_RESPONDING";
  if (eventCode == 0xB8) return "PIN_RESET";
  if (eventCode == 0xB9) return "POWER_ON_RESET";
  if (eventCode == 0xBA) return "SOFTWARE_RESET";
  if (eventCode == 0xBB) return "IWDG_RESET";
  if (eventCode == 0xBC) return "WWDG_RESET";
  if (eventCode == 0xBE) return "LOW_POWER_RESET";
  if (eventCode == 0xC8) return "RS485_BUS_ERROR";
  if (eventCode == 0xC9) return "RT_RPM_SENSOR_ERROR";
  if (eventCode == 0xCA) return "RT_FANCOIL_NTC_ERROR";
  if (eventCode == 0xCB) return "RT_LOW_TEMP_ERROR";
  if (eventCode == 0xCC) return "RT_HIGH_TEMP_ERROR";
  if (eventCode == 0xCD) return "RT_FREEZE_PROTECTION";
  if (eventCode == 0xCE) return "RT_DISPLAY_NTC_ERROR";
  if (eventCode == 0xCF) return "FIRMWARE_UPDATED";
  if (eventCode == 0xD0) return "FIRMWARE_UPDATE_FAILED";
  if (eventCode == 0xD1) return "BOOTLOADER_UPDATED";
  if (eventCode == 0xD2) return "BOOTLOADER_UPDATE_FAILED";
  if (eventCode == 0xD3) return "IMAGE_UPDATED";
  if (eventCode == 0xD4) return "IMAGE_UPDATE_FAILED";
  if (eventCode == 0xD5) return "FILE_COPY_FAILED";
  if (eventCode == 0xD6) return "FILE_BACKUP_FAILED";
  if (eventCode == 0xD7) return "UNKNOWN_CARD";
  if (eventCode == 0xD8) return "CARD_EXPIRED";
  if (eventCode == 0xD9) return "CARD_INVALID";
  if (eventCode == 0xDA) return "WRONG_ROOM";
  if (eventCode == 0xDB) return "WRONG_SYSTEM_ID";
  if (eventCode == 0xDC) return "GUEST_CARD";
  if (eventCode == 0xDD) return "HANDMAID_CARD";
  if (eventCode == 0xDE) return "MANAGER_CARD";
  if (eventCode == 0xDF) return "SERVICE_CARD";
  if (eventCode == 0xE0) return "ENTRY_DOOR_OPENED";
  if (eventCode == 0xE1) return "ENTRY_DOOR_CLOSED";
  if (eventCode == 0xE2) return "ENTRY_DOOR_NOT_CLOSED";
  if (eventCode == 0xE3) return "MINIBAR_USED";
  if (eventCode == 0xE4) return "BALCONY_DOOR_OPENED";
  if (eventCode == 0xE5) return "BALCONY_DOOR_CLOSED";
  if (eventCode == 0xE6) return "CARD_STACKER_ON";
  if (eventCode == 0xE7) return "CARD_STACKER_OFF";
  if (eventCode == 0xE8) return "DO_NOT_DISTURB_ON";
  if (eventCode == 0xE9) return "DO_NOT_DISTURB_OFF";
  if (eventCode == 0xEA) return "HANDMAID_SWITCH_ON";
  if (eventCode == 0xEB) return "HANDMAID_SWITCH_OFF";
  if (eventCode == 0xEC) return "HANDMAID_SERVICE_END";
  if (eventCode == 0xED) return "SOS_ALARM_TRIGGERED";
  if (eventCode == 0xEE) return "SOS_ALARM_RESET";
  if (eventCode == 0xEF) return "FIRE_ALARM_TRIGGERED";
  if (eventCode == 0xF0) return "FIRE_ALARM_RESET";
  if (eventCode == 0xF1) return "FLOOD_SENSOR_ACTIVE";
  if (eventCode == 0xF2) return "FLOOD_SENSOR_INACTIVE";
  if (eventCode == 0xF3) return "DOOR_BELL_ACTIVE";
  if (eventCode == 0xF4) return "DOOR_LOCK_USER_OPEN";
  if (eventCode == 0xF5) return "FIRE_EXIT_TRIGGERED";
  if (eventCode == 0xF6) return "FIRE_EXIT_RESET";
  if (eventCode == 0xF7) return "PASSWORD_VALID";
  if (eventCode == 0xF8) return "PASSWORD_INVALID";
  if (eventCode == 0xF9) return "ROOM_TIME_POWER_OFF";
  return "UNKNOWN_EVENT";
}
/**
 * HELPER FUNKCIJA - User-friendly access description
 */
String getAccessDescription(uint8_t eventCode, uint8_t userGroup)
{
  if (eventCode == 0xDC) return "Gost usao karticom";
  if (eventCode == 0xDD) return "Sobarica usla karticom";
  if (eventCode == 0xDE) return "Manager usao karticom";
  if (eventCode == 0xDF) return "Service usao karticom";
  if (eventCode == 0xF7) {
    if (userGroup == 1) return "Gost usao PIN-om";
    if (userGroup == 2) return "Sobarica usla PIN-om";
    if (userGroup == 3) return "Manager usao PIN-om";
    if (userGroup == 4) return "Service usao PIN-om";
    return "Korisnik usao PIN-om";
  }
  if (eventCode == 0xF8) return "Pogresan PIN";
  if (eventCode == 0xD7) return "Nepoznata kartica";
  if (eventCode == 0xD8) return "Kartica istekla";
  if (eventCode == 0xD9) return "Nevazeca kartica";
  if (eventCode == 0xDA) return "Pogresna soba";
  if (eventCode == 0xDB) return "Pogresan sistem ID";
  return getEventName(eventCode);
}
/**
 * HELPER FUNKCIJA ZA BLOKADU SETOVANJA INPUT ONLY PINOVA
 */
bool isInputOnlyPin(int pin)
{
  return (pin == 34 || pin == 35 || pin == 36 || pin == 39);
}
/**
 * PROVJERA POSTOJANJA PINA
 */
bool isValidPin(int pin)
{
  if (pin < 0 || pin > 39)
    return false;

  // Izbaci sve pinove koji su na listi zabranjenih
  for (int i = 0; i < numUsedPins; i++)
  {
    if (usedPins[i] == pin)
      return false;
  }

  return true;
}
/**
 * PROVJERA ZAUZEĆA PINA
 */
bool isPinAvailable(int pin)
{
  if (!isValidPin(pin))
    return false;

  for (int i = 0; i < numUsedPins; i++)
  {
    if (pin == usedPins[i])
      return false;
  }

  return true;
}
/**
 * SETOVANJE PINA
 */
void setPinHigh(int pin)
{
  if (!isPinAvailable(pin))
    return;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
}
/**
 * RESETOVANJE PINA
 */
void setPinLow(int pin)
{
  if (!isPinAvailable(pin))
    return;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}
/**
 * SETOVANJE PINA I TIMERA ZA RESET ISTOG PINA
 */
void startPulse(int pin, unsigned long durationMs = 2000)
{
  if (!isPinAvailable(pin))
    return;

  // Nađi prazan slot
  for (int i = 0; i < MAX_PULSE_PINS; i++)
  {
    if (!pulseStates[i].active)
    {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, HIGH);
      pulseStates[i].pin = pin;
      pulseStates[i].pulseStart = millis();
      pulseStates[i].duration = durationMs;
      pulseStates[i].active = true;
      return;
    }
  }

  LOG_ERROR("No available pulse slots!");
}
/**
 * ONBOARD LED ZA PRIKAZ AKTIVNOSTI
 */
void checkLed()
{
  static LedState prevState = LED_OFF;
  static unsigned long startMillis = 0;
  static bool timingActive = false;
  static bool ledOn = false;
  static unsigned long lastToggle = 0;

  unsigned long now = millis();

  // Koristi globalnu led_state, ne lokalni parametar
  if ((led_state == LED_FAST) && !timingActive)
  {
    timingActive = true;
    startMillis = now;
    prevState = (prevState == LED_FAST) ? LED_SLOW : prevState;
  }

  if (timingActive && (now - startMillis >= 3000))
  {
    timingActive = false;
    led_state = prevState; // vraćamo globalni led_state
  }

  if (!timingActive && led_state && led_state != LED_FAST)
  {
    prevState = led_state;
  }

  // Blink logika
  switch (led_state)
  {
  case LED_OFF:
    ledOn = false;
    break;
  case LED_ON:
    ledOn = true;
    break;
  case LED_SLOW:
    if (now - lastToggle >= 1000)
    {
      ledOn = !ledOn;
      lastToggle = now;
    }
    break;
  case LED_FAST:
    if (now - lastToggle >= 100)
    {
      ledOn = !ledOn;
      lastToggle = now;
    }
    break;
  }

  digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
}
/**
 * UKLJUĆI IZLAZNI RELEJ VENTILATORA
 */
void setThermoFanLevel(int newLevel)
{
  if (newLevel == currentFanLevel)
    return; // nema promjene

  // Isključi sve releje
  digitalWrite(FAN_L, LOW);
  digitalWrite(FAN_M, LOW);
  digitalWrite(FAN_H, LOW);

  delay(200); // mala pauza radi sigurnosti pre nego se uključi novi relej

  // Uključi samo novi relej ako je traženi nivo > 0
  switch (newLevel)
  {
  case 1:
    digitalWrite(FAN_L, HIGH);
    break;
  case 2:
    digitalWrite(FAN_M, HIGH);
    break;
  case 3:
    digitalWrite(FAN_H, HIGH);
    break;
  }

  currentFanLevel = newLevel;
}
/**
 * PODESI BRZINU VENTILATORA I VENTIL
 */
void setFansAndValve(float temp)
{
  if (!tempSensorAvailable || th_mode == TH_OFF)
  {
    setThermoFanLevel(0);
    digitalWrite(VALVE, LOW);
    return;
  }

  float t1 = th_treshold;
  float t2 = 2.0f * th_treshold;
  float t3 = 3.0f * th_treshold;

  if (th_mode == TH_HEATING)
  {
    if (currentFanLevel == 0)
    {
      if (temp < th_setpoint - t1)
        setThermoFanLevel(1);
    }
    else if (currentFanLevel == 1)
    {
      if (temp < th_setpoint - t2)
        setThermoFanLevel(2);
      else if (temp >= th_setpoint)
        setThermoFanLevel(0);
    }
    else if (currentFanLevel == 2)
    {
      if (temp < th_setpoint - t3)
        setThermoFanLevel(3);
      else if (temp >= th_setpoint - t1)
        setThermoFanLevel(1);
    }
    else if (currentFanLevel == 3)
    {
      if (temp >= th_setpoint - t2)
        setThermoFanLevel(2);
    }
 
  }
  else if (th_mode == TH_COOLING)
  {
    // Obrnuto za hlađenje, sa hysteresis logikom
    if (currentFanLevel == 0)
    {
      if (temp > th_setpoint + t1)
        setThermoFanLevel(1);
    }
    else if (currentFanLevel == 1)
    {
      if (temp > th_setpoint + t2)
        setThermoFanLevel(2);
      else if (temp <= th_setpoint)
        setThermoFanLevel(0);
    }
    else if (currentFanLevel == 2)
    {
      if (temp > th_setpoint + t3)
        setThermoFanLevel(3);
      else if (temp <= th_setpoint + t1)
        setThermoFanLevel(1);
    }
    else if (currentFanLevel == 3)
    {
      if (temp <= th_setpoint + t2)
        setThermoFanLevel(2);
    }
  }
  digitalWrite(VALVE, currentFanLevel > 0 ? HIGH : LOW);
}
/**
 * ISKLJUČI SVE IZLAZE TERMOSTATA
 */
void turnOffThermoRelays()
{
  digitalWrite(FAN_L, LOW);
  digitalWrite(FAN_M, LOW);
  digitalWrite(FAN_H, LOW);
  digitalWrite(VALVE, LOW);
}
/**
 * UČITAJ IZ MEMORIJE POSTAVKE TERMOSTATA
 */
void loadThermoPreferences()
{
  preferences.begin("thermo", false);
  th_setpoint = preferences.getFloat("setpoint", 25.0);
  th_treshold = preferences.getFloat("treshold", 0.5);
  th_mode = (ThMode)preferences.getInt("mode", 0);
  th_saved_mode = (ThMode)preferences.getInt("th_saved_mode", 0);
  emaAlpha = preferences.getFloat("emaAlpha", 0.2);
  emaTemperature = th_setpoint; // start EMA blizu setpointa
  preferences.end();
}
/**
 * PARSER ODGOVORA NA ZAHTJEV STANJA SLOBODNIH PINOVA
 */
String getAvailablePinsStatus()
{
  String result = "";
  for (int pin = 0; pin <= 39; pin++)
  {
    if (isPinAvailable(pin))
    {
      // Postavi pin u INPUT samo ako je bezbedan
      // pinMode(pin, INPUT);
      int state = digitalRead(pin);
      result += "GPIO" + String(pin) + "=" + (state ? "HIGH" : "LOW") + "\n";
    }
  }
  return result;
}
/**
 * TINYFRAME ODGOVOR
 */
TF_Result ID_Listener(TinyFrame *tf, TF_Msg *msg)
{
  LOG_INFO("=== ID_Listener: received %d bytes, frame_id=0x%02X, type=0x%02X ===\n", 
                msg->len, msg->frame_id, msg->type);
  LOG_DEBUG("Data: ");
  for (int i = 0; i < msg->len && i < 50; i++) {
    if (msg->data[i] < 0x10) LOG_DEBUG("0");
    LOG_DEBUG_HEX(msg->data[i]);
    LOG_DEBUG(" ");
  }
  LOG_DEBUG_LN();
  
  replyDataLength = msg->len;
  memcpy(replyData, msg->data, msg->len);
  rdy = -1;
  return TF_CLOSE;
}
/**
 * TINYFRAME LISTENER ZA SOS SIGNAL
 */
TF_Result SOS_Listener(TinyFrame *tf, TF_Msg *msg)
{
  LOG_INFO_LN("⚠️ SOS Signal primljen iz toaleta!");
  sosStatus = true;
  sosTimestamp = millis();
  
  // Sačuvaj u Preferences za perzistentnost (UNIX timestamp)
  preferences.begin("sos_event", false);
  preferences.putBool("active", true);
  preferences.putULong("timestamp", time(nullptr)); // UNIX timestamp u sekundama
  preferences.end();
  
  LOG_INFO_LN("✅ SOS događaj sačuvan u memoriju (timestamp: %lu)", time(nullptr));
  
  // Pošalji potvrdu prijema
  TF_Respond(tf, msg);
  return TF_STAY;
}
/**
 * TINYFRAME LISTENER ZA IR REMOTE DATA
 */
TF_Result IR_Listener(TinyFrame *tf, TF_Msg *msg)
{
  if (msg->len >= 8) {
    // 1. Handle Physical IR Transmission
    preferences.begin("ir_settings", true);
    int protoID = preferences.getInt("protocol", 0);
    preferences.end();

    if (protoID > 0) {
      // Configure Protocol
      ac.next.protocol = (decode_type_t)protoID;
      
      // Control Logic (Power & Mode)
      // msg->data[0] mapping: 0=OFF, 1=COOL, 2=HEAT
      uint8_t ctrl = msg->data[0];
      
      if (ctrl == 0) {
        ac.next.power = false;
      } else {
        ac.next.power = true;
        ac.next.mode = (ctrl == 1) ? stdAc::opmode_t::kCool : stdAc::opmode_t::kHeat;
      }

      // Setpoint (msg->data[5])
      ac.next.degrees = msg->data[5];
      ac.next.celsius = true; // Always Celsius
      
      // Fan Speed -> Force Auto for simplicity, or map if needed
      ac.next.fanspeed = stdAc::fanspeed_t::kAuto;

      // Send the signal
      LOG_INFO_LN("📡 Sending IR: Proto=%d, Power=%d, Mode=%d, Temp=%d", 
                    protoID, ac.next.power, (int)ac.next.mode, ac.next.degrees);
      ac.sendAc(); 
    }

    // 2. Original Logging Logic (Preserved)
    irData.th_ctrl = msg->data[0];
    irData.th_state = msg->data[1];
    irData.mv_temp = (msg->data[2] << 8) | msg->data[3];
    irData.mv_offset = msg->data[4];
    irData.sp_temp = msg->data[5];
    irData.fan_ctrl = msg->data[6];
    irData.fan_speed = msg->data[7];
    irData.received = true;
    irData.timestamp = millis();
    
    LOG_INFO_LN("📡 IR Data Received: ctrl=%d, state=%d, temp=%d.%02d°C, sp=%d°C, fan=%d/%d",
                  irData.th_ctrl, irData.th_state, 
                  irData.mv_temp / 100, irData.mv_temp % 100,
                  irData.sp_temp, irData.fan_ctrl, irData.fan_speed);
  } else {
    LOG_ERROR("⚠️ IR_Listener: Neočekivana dužina paketa (%d), očekivano 8", msg->len);
  }
  
  // Pošalji potvrdu prijema
  TF_Respond(tf, msg);
  return TF_STAY;
}
/**
 * KONVERTOR STRING HHMM u sate i minute
 */
bool parseHHMM(const String &str, int &hh, int &mm)
{
  if (str.length() != 4)
    return false;
  hh = str.substring(0, 2).toInt();
  mm = str.substring(2, 4).toInt();
  return hh >= 0 && hh < 24 && mm >= 0 && mm < 60;
}
/**
 * JSON RESPONSE HELPER - Success
 */
void sendJsonSuccess(AsyncWebServerRequest *request, const String &message, JsonDocument *data = nullptr)
{
  JsonDocument doc;
  doc["status"] = "success";
  doc["message"] = message;
  
  if (data != nullptr) {
    doc["data"] = *data;
  }
  
  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}
/**
 * JSON RESPONSE HELPER - Error
 */
void sendJsonError(AsyncWebServerRequest *request, int code, const String &message)
{
  JsonDocument doc;
  doc["status"] = "error";
  doc["code"] = code;
  doc["message"] = message;
  
  String response;
  serializeJson(doc, response);
  request->send(code, "application/json", response);
}
/**
 * RUČNA KONTROLA VANJSKE RASVJETE
 */
void setOutdoorLightOverride(bool state)
{
  overrideActive = true;
  overrideState = state;
  digitalWrite(LIGHT_PIN, state ? HIGH : LOW);
  lightState = state;
}
/**
 * RESETUJ FLAG RUČNE KONTROLE RELEJA VANJSKE RASVJETE
 */
void clearOutdoorLightOverride()
{
  overrideActive = false;
}
/**
 * SAČUVAJ POSTAVKE TIMERA U MEMORIJU
 */
void saveTimerPreferences(const String &onType, const String &offType, const String &onTime, const String &offTime)
{
  preferences.begin("timerset", false);
  preferences.putString("onType", onType);
  preferences.putString("offType", offType);
  preferences.putString("onTime", onTime);
  preferences.putString("offTime", offTime);
  preferences.end();
}
/**
 * UČITAJ TIMER IZ MEMORIJE
 */
void loadTimerPreferences()
{
  preferences.begin("timerset", true);
  timerOnType = preferences.getString("onType", "OFF");
  timerOffType = preferences.getString("offType", "OFF");
  timerOnTime = preferences.getString("onTime", "0000");
  timerOffTime = preferences.getString("offTime", "0000");
  preferences.end();
}
/**
 * IZRAČUN MINUTA OD PONOĆI
 */
int getCurrentMinutes()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
    return -1;
  return timeinfo.tm_hour * 60 + timeinfo.tm_min;
}
/**
 * KONVERZIJA TEKSTA U MINUTE
 */
int timeStringToMinutes(String t)
{
  int h, m;
  if (!parseHHMM(t, h, m))
    return -1;
  return h * 60 + m;
}
/**
 * PROVJERA LJETNOG / ZIMSKOG OFFSETA VREMENA
 */
bool isDST(int year, int month, int day, int hour)
{
  // Posljednja nedjelja u martu
  int lastSundayMarch = 31;
  while (true)
  {
    struct tm timeinfo = {0};
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = 2; // Mart (0-based)
    timeinfo.tm_mday = lastSundayMarch;
    mktime(&timeinfo);
    if (timeinfo.tm_wday == 0)
      break; // 0 = nedjelja
    lastSundayMarch--;
  }

  // Posljednja nedjelja u oktobru
  int lastSundayOctober = 31;
  while (true)
  {
    struct tm timeinfo = {0};
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = 9; // Oktobar (0-based)
    timeinfo.tm_mday = lastSundayOctober;
    mktime(&timeinfo);
    if (timeinfo.tm_wday == 0)
      break;
    lastSundayOctober--;
  }

  // Kreiraj uporedne datume za granice DST
  struct tm startDST = {0};
  startDST.tm_year = year - 1900;
  startDST.tm_mon = 2; // Mart
  startDST.tm_mday = lastSundayMarch;
  startDST.tm_hour = 2;
  time_t tStartDST = mktime(&startDST);

  struct tm endDST = {0};
  endDST.tm_year = year - 1900;
  endDST.tm_mon = 9; // Oktobar
  endDST.tm_mday = lastSundayOctober;
  endDST.tm_hour = 3;
  time_t tEndDST = mktime(&endDST);

  struct tm current = {0};
  current.tm_year = year - 1900;
  current.tm_mon = month - 1;
  current.tm_mday = day;
  current.tm_hour = hour;
  time_t tCurrent = mktime(&current);

  return (tCurrent >= tStartDST) && (tCurrent < tEndDST);
}
/**
 * RELEJ VANJSKE RASVJETE
 */
void updateLightState()
{
  if (timeValid == false)
    return;

  time_t now;
  time(&now);
  struct tm utc_tm = *gmtime(&now); // Koristi UTC datum

  bool dstActive = isDST(utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday, utc_tm.tm_hour);
  int tzOffsetMinutes = dstActive ? 120 : 60;

  // DEBUG
  LOG_DEBUG_LN("----- updateLightState -----");
  LOG_DEBUG_F("UTC: %02d:%02d\n", utc_tm.tm_hour, utc_tm.tm_min);
  LOG_DEBUG_F("DST active: %s\n", dstActive ? "YES" : "NO");
  LOG_DEBUG_F("Timezone offset: %d min\n", tzOffsetMinutes);

  // Postavi sunčev datum tačno
  sun.setPosition(LATITUDE, LONGITUDE, 0);
  sun.setCurrentDate(utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday);

  float sunriseUTC = sun.calcSunrise();
  float sunsetUTC = sun.calcSunset();

  int sunriseMin = ((int)sunriseUTC + tzOffsetMinutes + 1440) % 1440;
  int sunsetMin = ((int)sunsetUTC + tzOffsetMinutes + 1440) % 1440;

  LOG_DEBUG_F("Sunrise: %.2f -> %02d:%02d\n", sunriseUTC, sunriseMin / 60, sunriseMin % 60);
  LOG_DEBUG_F("Sunset: %.2f -> %02d:%02d\n", sunsetUTC, sunsetMin / 60, sunsetMin % 60);

  int onMin = -1, offMin = -1;

  if (timerOnType == "SUNRISE")
    onMin = sunriseMin;
  else if (timerOnType == "SUNSET")
    onMin = sunsetMin;
  else if (timerOnType != "OFF" && timerOnTime.length() == 4)
    onMin = timerOnTime.substring(0, 2).toInt() * 60 + timerOnTime.substring(2, 4).toInt();

  if (timerOffType == "SUNRISE")
    offMin = sunriseMin;
  else if (timerOffType == "SUNSET")
    offMin = sunsetMin;
  else if (timerOffType != "OFF" && timerOffTime.length() == 4)
    offMin = timerOffTime.substring(0, 2).toInt() * 60 + timerOffTime.substring(2, 4).toInt();

  int localMinutes = (utc_tm.tm_hour * 60 + utc_tm.tm_min + tzOffsetMinutes) % 1440;

  LOG_DEBUG_F("Current time (local min): %d (%02d:%02d)\n", localMinutes, localMinutes / 60, localMinutes % 60);
  LOG_DEBUG_F("ON=%d (%02d:%02d), OFF=%d (%02d:%02d)\n",
                onMin, onMin / 60, onMin % 60, offMin, offMin / 60, offMin % 60);

  bool lightShouldBeOn = false;

  if (onMin >= 0 && offMin >= 0)
  {
    if (onMin < offMin)
      lightShouldBeOn = (localMinutes >= onMin && localMinutes < offMin);
    else
      lightShouldBeOn = (localMinutes >= onMin || localMinutes < offMin);
  }

  // OVERRIDE MEHANIZAM - Resetuj override samo kada timer PROMENI stanje (sunrise/sunset event)
  if (overrideActive)
  {
    // Proveri da li je došlo do PROMENE stanja timera (timer event)
    if (lastTimerState != lightShouldBeOn)
    {
      // Timer je promenio stanje (npr. došao je sunrise ili sunset)
      LOG_INFO("Timer event detected: %s -> %s. Override reset.\n", 
               lastTimerState ? "ON" : "OFF", lightShouldBeOn ? "ON" : "OFF");
      clearOutdoorLightOverride(); // Resetuj override i pusti timer da preuzme
      lastTimerState = lightShouldBeOn;
      digitalWrite(LIGHT_PIN, lightShouldBeOn ? HIGH : LOW);
      lightState = lightShouldBeOn;
    }
    else
    {
      // Timer nije promenio stanje, zadržavamo override
      LOG_DEBUG_F("Override active, timer unchanged. Keeping override state: %s\n", 
                overrideState ? "ON" : "OFF");
      return; // Ne diraj pin, ostavi override stanje
    }
  }
  else
  {  
    // Nema override-a, primeni timer stanje
    lastTimerState = lightShouldBeOn;
    digitalWrite(LIGHT_PIN, lightShouldBeOn ? HIGH : LOW);
    lightState = lightShouldBeOn;
  }

  LOG_DEBUG_F("Light should be: %s\n", lightShouldBeOn ? "ON" : "OFF");
  LOG_DEBUG_LN("----------------------------");
}
/**
 * TIMER PROVJERE KONTROLE VANJSKE RASVJETE
 */
void setupLightControl()
{
  lightTicker.attach(60.0, updateLightState); // Provjerava svakih 60 sekundi
}
/**
 * OBRADA HTTP CGI ZAHTJEVA
 */
void handleSysctrlRequest(AsyncWebServerRequest *request)
{
  String bin = "";
  String body = "";  // Dodano za kompatibilnost sa CMD_READ_LOG koji još nije konvertovan

  if (!request->hasParam("CMD"))
  {
    sendJsonError(request, 400, "Missing CMD parameter");
    return;
  }

  String cmdStr = request->getParam("CMD")->value();
  CommandType cmd = stringToCommand(cmdStr);

  uint8_t buf[64] = {0};
  int length = 0;
  bool isLocalCommand = false;  // Flag: true za komande koje se obrađuju lokalno (bez RS485)
  led_state = LED_FAST;

  switch (cmd)
  {

  case CMD_RESTART:
  {
    LOG_INFO_LN("Device restart...");
    isLocalCommand = true;  // ✅ Lokalna komanda
    sendJsonSuccess(request, "Restart in 3s");
    delay(3000);
    ESP.restart();
    break;
  }
  case CMD_RESTART_CTRL:
  {
    if (!request->hasParam("ID"))
    {
      sendJsonError(request, 400, "Missing ID parameter");
      return;
    }

    int id = request->getParam("ID")->value().toInt();

    // ✅ Validacija ID-a
    if (id < 1 || id > 254)
    {
      sendJsonError(request, 400, "Invalid ID (must be 1-254)");
      return;
    }
    buf[0] = cmd;
    buf[1] = id;
    length = 2;
    break;
  }
  case CMD_GET_SSID_PSWRD:
  {
    preferences.begin("wifi", false);
    preferences.getString("ssid", _ssid, sizeof(_ssid));
    preferences.getString("password", _pass, sizeof(_pass));
    preferences.end();

    JsonDocument data;
    data["ssid"] = String(_ssid);
    data["password"] = strlen(_pass) == 0 ? "" : String(_pass);
    data["open_network"] = strlen(_pass) == 0;
    
    sendJsonSuccess(request, "WiFi credentials retrieved", &data);
    return;
  }
  case CMD_SET_SSID_PSWRD:
  {
    if (!request->hasParam("SSID"))
    {
      sendJsonError(request, 400, "Missing SSID parameter");
      return;
    }
    strlcpy(_ssid, request->getParam("SSID")->value().c_str(), sizeof(_ssid));
    strlcpy(_pass, request->hasParam("PSWRD") ? request->getParam("PSWRD")->value().c_str() : "", sizeof(_pass));

    preferences.begin("wifi", false);
    preferences.putString("ssid", _ssid);
    preferences.putString("password", _pass);
    preferences.end();
    
    JsonDocument data;
    data["ssid"] = String(_ssid);
    sendJsonSuccess(request, "WiFi credentials saved", &data);
    return;
  }
  case CMD_GET_MDNS_NAME:
  {
    isLocalCommand = true;  // ✅ Lokalna komanda

    preferences.begin("_mdns", false);
    preferences.getString("mdns", _mdns, sizeof(_mdns));
    preferences.end();
    
    JsonDocument data;
    data["mdns"] = String(_mdns);
    sendJsonSuccess(request, "mDNS name retrieved", &data);
    return;
  }
  case CMD_SET_MDNS_NAME:
  {
    if (!request->hasParam("MDNS"))
    {
      sendJsonError(request, 400, "Missing MDNS parameter");
      return;
    }
    strlcpy(_mdns, request->getParam("MDNS")->value().c_str(), sizeof(_mdns));
    preferences.begin("_mdns", false);
    preferences.putString("mdns", _mdns);
    preferences.end();
    
    JsonDocument data;
    data["mdns"] = String(_mdns);
    sendJsonSuccess(request, "mDNS name saved", &data);
    return;
  }
  case CMD_GET_IP_ADDRESS:
  {
    JsonDocument data;
    data["ip"] = WiFi.localIP().toString();
    data["subnet"] = WiFi.subnetMask().toString();
    data["gateway"] = WiFi.gatewayIP().toString();
    sendJsonSuccess(request, "IP address retrieved", &data);
    return;
  }
  case CMD_GET_TCPIP_PORT:
  {
    preferences.begin("_port", false);
    _port = preferences.getInt("port", 0);
    preferences.end();
    
    JsonDocument data;
    data["port"] = _port;
    sendJsonSuccess(request, "TCP/IP port retrieved", &data);
    return;
  }
  case CMD_SET_TCPIP_PORT:
  {
    if (!request->hasParam("PORT"))
    {
      sendJsonError(request, 400, "Missing PORT parameter");
      return;
    }
    _port = request->getParam("PORT")->value().toInt();
    preferences.begin("_port", false);
    preferences.putInt("port", _port);
    preferences.end();
    
    JsonDocument data;
    data["port"] = _port;
    sendJsonSuccess(request, "TCP/IP port saved", &data);
    return;
  }
  case CMD_SET_PASSWORD:
  {
    if (!request->hasParam("ID") || !request->hasParam("TYPE") || !request->hasParam("PASSWORD"))
    {
      sendJsonError(request, 400, "Missing ID, TYPE or PASSWORD parameter");
      return;
    }
    
    int id = request->getParam("ID")->value().toInt();
    String type = request->getParam("TYPE")->value();
    String password = request->getParam("PASSWORD")->value();
    
    // Validacija ID-a
    if (id < 1 || id > 254)
    {
      sendJsonError(request, 400, "Invalid ID (must be 1-254)");
      return;
    }
    
    // Validacija lozinke (samo numerički karakteri)
    for (size_t i = 0; i < password.length(); i++)
    {
      if (!isdigit(password[i]))
      {
        sendJsonError(request, 400, "Password must contain only digits");
        return;
      }
    }
    
    String dataString = "";
    
    if (type == "GUEST")
    {
      // Guest lozinka zahtijeva GUEST_ID i EXPIRY
      if (!request->hasParam("GUEST_ID") || !request->hasParam("EXPIRY"))
      {
        sendJsonError(request, 400, "Missing GUEST_ID or EXPIRY for GUEST type");
        return;
      }
      
      int guestId = request->getParam("GUEST_ID")->value().toInt();
      String expiry = request->getParam("EXPIRY")->value();
      
      // Validacija GUEST_ID
      if (guestId < 1 || guestId > 8)
      {
        sendJsonError(request, 400, "Invalid GUEST_ID (must be 1-8)");
        return;
      }
      
      // Validacija EXPIRY formata (mora biti 10 karaktera: HHMMDDMMYY)
      if (expiry.length() != 10)
      {
        sendJsonError(request, 400, "Invalid EXPIRY format (must be HHMMDDMMYY)");
        return;
      }
      
      // Kreiraj string: G{ID},{PASSWORD},{EXPIRY}
      dataString = "G" + String(guestId) + "," + password + "," + expiry;
    }
    else if (type == "MAID")
    {
      dataString = "H" + password;
    }
    else if (type == "MANAGER")
    {
      dataString = "M" + password;
    }
    else if (type == "SERVICE")
    {
      dataString = "S" + password;
    }
    else if (type == "DELETE_GUEST")
    {
      // Brisanje Guest lozinke: G{ID}X
      if (!request->hasParam("GUEST_ID"))
      {
        sendJsonError(request, 400, "Missing GUEST_ID for DELETE_GUEST");
        return;
      }
      
      int guestId = request->getParam("GUEST_ID")->value().toInt();
      
      if (guestId < 1 || guestId > 8)
      {
        sendJsonError(request, 400, "Invalid GUEST_ID (must be 1-8)");
        return;
      }
      
      dataString = "G" + String(guestId) + "X";
    }
    else
    {
      sendJsonError(request, 400, "Invalid TYPE (must be GUEST, MAID, MANAGER, SERVICE or DELETE_GUEST)");
      return;
    }
    
    // Kreiraj RS485 poruku
    buf[0] = CMD_SET_PASSWORD;
    buf[1] = id;
    strlcpy((char *)buf + 2, dataString.c_str(), sizeof(buf) - 2);
    length = 2 + dataString.length() + 1;  // +1 za null terminator
    
    break;
  }
  case CMD_SET_ROOM_TEMP:
  case CMD_SET_GUEST_IN_TEMP:
  case CMD_SET_GUEST_OUT_TEMP:
  {
    if (!request->hasParam("ID") || !request->hasParam("VALUE"))
    {
      sendJsonError(request, 400, "Missing ID or VALUE");
      return;
    }

    int id = request->getParam("ID")->value().toInt();
    int value = request->getParam("VALUE")->value().toInt();

    // ✅ Validacija ID-a
    if (id < 1 || id > 254)
    {
      sendJsonError(request, 400, "Invalid ID (must be 1-254)");
      return;
    }

    // ✅ Validacija VALUE-a
    if (value < 5 || value > 40)
    {
      sendJsonError(request, 400, "Invalid VALUE (must be 5-40)");
      return;
    }

    buf[0] = cmd;
    buf[1] = id;
    buf[2] = value;
    length = 3;
    break;
  }
  case CMD_GET_PASSWORD:
  {
    if (!request->hasParam("ID") || !request->hasParam("TYPE"))
    {
      sendJsonError(request, 400, "Missing ID or TYPE");
      return;
    }
    
    int id = request->getParam("ID")->value().toInt();
    String type = request->getParam("TYPE")->value();
    
    // Validacija ID-a
    if (id < 1 || id > 254)
    {
      sendJsonError(request, 400, "Invalid ID (must be 1-254)");
      return;
    }
    
    char userGroup = 0;
    uint8_t guestId = 0;
    
    if (type == "GUEST")
    {
      if (!request->hasParam("GUEST_ID"))
      {
        sendJsonError(request, 400, "Missing GUEST_ID for GUEST type");
        return;
      }
      
      guestId = request->getParam("GUEST_ID")->value().toInt();
      
      // Validacija GUEST_ID
      if (guestId < 1 || guestId > 8)
      {
        sendJsonError(request, 400, "Invalid GUEST_ID (must be 1-8)");
        return;
      }
      
      userGroup = 'G';
      buf[0] = CMD_GET_PASSWORD;
      buf[1] = id;
      buf[2] = userGroup;
      buf[3] = guestId;
      length = 4;
    }
    else if (type == "MAID")
    {
      userGroup = 'H';
      buf[0] = CMD_GET_PASSWORD;
      buf[1] = id;
      buf[2] = userGroup;
      length = 3;
    }
    else if (type == "MANAGER")
    {
      userGroup = 'M';
      buf[0] = CMD_GET_PASSWORD;
      buf[1] = id;
      buf[2] = userGroup;
      length = 3;
    }
    else if (type == "SERVICE")
    {
      userGroup = 'S';
      buf[0] = CMD_GET_PASSWORD;
      buf[1] = id;
      buf[2] = userGroup;
      length = 3;
    }
    else
    {
      sendJsonError(request, 400, "Invalid TYPE (must be GUEST, MAID, MANAGER or SERVICE)");
      return;
    }
    
    break;
  }
  case CMD_READ_LOG:
  case CMD_DELETE_LOG:
  {
    if (!request->hasParam("ID"))
    {
      sendJsonError(request, 400, "Missing ID parameter");
      return;
    }
    
    int id = request->getParam("ID")->value().toInt();
    
    if (id < 1 || id > 254)
    {
      sendJsonError(request, 400, "Invalid ID (must be 1-254)");
      return;
    }
    
    buf[0] = cmd;
    buf[1] = id;
    length = 2;
    break;
  }
  case CMD_GET_ROOM_TEMP:
  case CMD_GET_GUEST_IN_TEMP:
  case CMD_GET_GUEST_OUT_TEMP:
  case CMD_SET_THST_OFF:
  case CMD_SET_THST_ON:
  case CMD_SET_THST_HEATING:
  case CMD_SET_THST_COOLING:
  case CMD_GET_FAN_DIFFERENCE:
  case CMD_GET_FAN_BAND:
  {
    if (!request->hasParam("ID"))
    {
      sendJsonError(request, 400, "Missing ID");
      return;
    }
    
    int id = request->getParam("ID")->value().toInt();
    
    // ✅ Validacija 1-bajtne adrese
    if (id < 1 || id > 254)
    {
      sendJsonError(request, 400, "Invalid ID (must be 1-254)");
      return;
    }
    
    buf[0] = cmd;
    buf[1] = id;
    length = 2;
    break;
  }
  case CMD_SET_PIN:
  {
    if (!request->hasParam("ID") || !request->hasParam("PIN") || !request->hasParam("VALUE"))
    {
      sendJsonError(request, 400, "Missing ID or PIN or VALUE");
      return;
    }

    int id = request->getParam("ID")->value().toInt();
    int pin = request->getParam("PIN")->value().toInt();
    int value = request->getParam("VALUE")->value().toInt();

    // ✅ Validacija ID-a
    if (id < 1 || id > 254)
    {
      sendJsonError(request, 400, "Invalid ID (must be 1-254)");
      return;
    }

    // ✅ Validacija PIN-a
    if (pin < 1 || pin > 6)
    {
      sendJsonError(request, 400, "Invalid PIN (must be 1-6)");
      return;
    }

    // ✅ Validacija VALUE-a
    if (value != 0 && value != 1)
    {
      sendJsonError(request, 400, "Invalid VALUE (must be 0 or 1)");
      return;
    }

    buf[0] = cmd;
    buf[1] = id;
    buf[2] = 254;
    buf[3] = pin;
    buf[4] = value;
    length = 5;
    break;
  }
  case CMD_OPEN_DOOR:
  {
    // Jednostavna "one-shot" komanda za otvaranje vrata
    // Prima samo ID (broj sobe), automatski šalje PORT=C, PIN=8, VALUE=1
    if (!request->hasParam("ID"))
    {
      sendJsonError(request, 400, "Missing ID");
      return;
    }

    int id = request->getParam("ID")->value().toInt();

    // ✅ Validacija 1-bajtne adrese
    if (id < 1 || id > 254)
    {
      sendJsonError(request, 400, "Invalid ID (must be 1-254)");
      return;
    }

    // Komanda za otvaranje vrata (bez dodatnih parametara)
    buf[0] = cmd;       // 0xDB (OPEN_DOOR)
    buf[1] = id;        // 1-bajtna adresa (1-254)
    length = 2;         // Samo CMD i ID
    break;
  }
  case CMD_GET_PINS:
  {
    if (!request->hasParam("ID"))
    {
      sendJsonError(request, 400, "Missing ID");
      return;
    }
    int id = request->getParam("ID")->value().toInt();
    // ✅ Validacija ID-a
    if (id < 1 || id > 254)
    {
      sendJsonError(request, 400, "Invalid ID (must be 1-254)");
      return;
    }
    buf[0] = 0xB1;
    buf[1] = id;
    buf[2] = 0xB2;
    length = 3;
    break;
  }
  case CMD_SET_LANG:
  {
    if (!request->hasParam("ID") || !request->hasParam("VALUE"))
    {
      sendJsonError(request, 400, "Missing ID or VALUE");
      return;
    }
    int id = request->getParam("ID")->value().toInt();
    int lang = request->getParam("VALUE")->value().toInt();

    if (id < 1 || id > 254) {
      sendJsonError(request, 400, "Invalid ID");
      return;
    }

    if (lang < 0 || lang > 2) {
      sendJsonError(request, 400, "Invalid VALUE (0=SRB, 1=ENG, 2=GER)");
      return;
    }
    
    buf[0] = cmd;
    buf[1] = id;
    buf[2] = (uint8_t)lang;
    length = 3;
    break;
  }
  case CMD_QR_CODE_SET:
  {
    if (!request->hasParam("ID") || !request->hasParam("QR_CODE"))
    {
      sendJsonError(request, 400, "Missing ID or QR_CODE");
      return;
    }
    int id = request->getParam("ID")->value().toInt();
    String qr = request->getParam("QR_CODE")->value();

    if (id < 1 || id > 254) {
      sendJsonError(request, 400, "Invalid ID");
      return;
    }
    if (qr.length() > 128) {
      sendJsonError(request, 400, "QR Code too long (max 128)");
      return;
    }

    buf[0] = cmd;
    buf[1] = id;
    memcpy(buf + 2, qr.c_str(), qr.length());
    buf[2 + qr.length()] = 0; // Null terminate just in case, though length handles it
    length = 2 + qr.length(); // CMD + ID + String
    break;
  }
  case CMD_QR_CODE_GET:
  case CMD_GET_ROOM_STATUS:
  {
    if (!request->hasParam("ID"))
    {
      sendJsonError(request, 400, "Missing ID");
      return;
    }
    int id = request->getParam("ID")->value().toInt();
    if (id < 1 || id > 254) {
      sendJsonError(request, 400, "Invalid ID");
      return;
    }
    buf[0] = cmd;
    buf[1] = id;
    length = 2;
    break;
  }
  case CMD_GET_SYSID:
  {
    if (!request->hasParam("ID"))
    {
      sendJsonError(request, 400, "Missing ID");
      return;
    }
    int id = request->getParam("ID")->value().toInt();
    if (id < 1 || id > 254) {
      sendJsonError(request, 400, "Invalid ID");
      return;
    }
    buf[0] = cmd;
    buf[1] = id;
    length = 2;
    break;
  }
  case CMD_SET_SYSID:
  {
    if (!request->hasParam("ID") || !request->hasParam("VALUE"))
    {
      sendJsonError(request, 400, "Missing ID or VALUE");
      return;
    }
    int id = request->getParam("ID")->value().toInt();
    int sysid_val = request->getParam("VALUE")->value().toInt(); // Očekujemo decimalnu vrijednost (npr. 43981 za 0xABCD)

    if (id < 1 || id > 254) {
      sendJsonError(request, 400, "Invalid ID");
      return;
    }
    
    // Rastavljanje 16-bitnog SYSID na 2 bajta
    // Napomena: common.h koristi format [CMD][ADDR][VAL_MSB][VAL_LSB] za neke komande, 
    // ali provjeri rs485_ulaz.c implementaciju:
    // RS_SetSysID: sysid[0] = msg->data[2]; sysid[1] = msg->data[3];
    
    buf[0] = cmd;
    buf[1] = id;
    buf[2] = (uint8_t)((sysid_val >> 8) & 0xFF); // MSB
    buf[3] = (uint8_t)(sysid_val & 0xFF);        // LSB
    length = 4;
    break;
  }
  case CMD_GET_TIMER:
  {
    time_t now;
    time(&now);
    struct tm utc_tm = *gmtime(&now);

    // Ručno računaj DST za Sarajevo
    bool dstActive = isDST(utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday, utc_tm.tm_hour);

    // Offset u minutama: 60 za CET (zimsko), 120 za CEST (ljetno)
    int tzOffsetMinutes = dstActive ? 120 : 60;

    LOG_DEBUG_F("UTC Time    : %02d:%02d\n", utc_tm.tm_hour, utc_tm.tm_min);
    LOG_DEBUG_F("DST active  : %d\n", dstActive);
    LOG_DEBUG_F("Offset (min): %d\n", tzOffsetMinutes);

    sun.setPosition(LATITUDE, LONGITUDE, 0);
    sun.setCurrentDate(utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday);

    char sunriseStr[6] = "----";
    char sunsetStr[6] = "----";

    float sunriseUTC = sun.calcSunrise();
    float sunsetUTC = sun.calcSunset();

    if (!isnan(sunriseUTC) && !isnan(sunsetUTC))
    {
      int sunriseMin = (int)sunriseUTC + tzOffsetMinutes;
      int sunsetMin = (int)sunsetUTC + tzOffsetMinutes;

      sunriseMin = (sunriseMin + 1440) % 1440;
      sunsetMin = (sunsetMin + 1440) % 1440;

      int sunriseHour = sunriseMin / 60;
      int sunriseMinute = sunriseMin % 60;
      int sunsetHour = sunsetMin / 60;
      int sunsetMinute = sunsetMin % 60;

      sprintf(sunriseStr, "%02d:%02d", sunriseHour, sunriseMinute);
      sprintf(sunsetStr, "%02d:%02d", sunsetHour, sunsetMinute);

      LOG_DEBUG_F("SUNRISE local: %s\n", sunriseStr);
      LOG_DEBUG_F("SUNSET  local: %s\n", sunsetStr);
    }

    bool relayState = digitalRead(LIGHT_PIN);

    JsonDocument responseDoc;
    responseDoc["timer_on"] = timerOnType;
    responseDoc["timer_off"] = timerOffType;
    responseDoc["on_time"] = timerOnTime;
    responseDoc["off_time"] = timerOffTime;
    responseDoc["sunrise"] = String(sunriseStr);
    responseDoc["sunset"] = String(sunsetStr);
    responseDoc["relay_state"] = relayState ? "ON" : "OFF";
    responseDoc["dst_active"] = dstActive;
    
    sendJsonSuccess(request, "Timer settings retrieved", &responseDoc);
    break;
  }
  case CMD_SET_TIMER:
  {
    String on = request->hasParam("TIMERON") ? request->getParam("TIMERON")->value() : "";
    String off = request->hasParam("TIMEROFF") ? request->getParam("TIMEROFF")->value() : "";

    String onType = "OFF";
    String offType = "OFF";
    String onTime = "0000";
    String offTime = "0000";
    int temp_hh, temp_mm;
    
    if (on == "SUNSET" || on == "OFF")
      onType = on;
    else if (on.length() == 4 && parseHHMM(on, temp_hh, temp_mm))
    {
      onType = "MANUAL";
      onTime = on;
    }
    else
    {
      sendJsonError(request, 400, "Invalid TIMERON value.");
      return;
    }

    if (off == "SUNRISE" || off == "OFF")
      offType = off;
    else if (off.length() == 4 && parseHHMM(off, temp_hh, temp_mm))
    {
      offType = "MANUAL";
      offTime = off;
    }
    else
    {
      sendJsonError(request, 400, "Invalid TIMEROFF value");
      return;
    }

    saveTimerPreferences(onType, offType, onTime, offTime);
    loadTimerPreferences();
    
    JsonDocument responseDoc;
    responseDoc["timer_on"] = onType;
    responseDoc["timer_off"] = offType;
    responseDoc["on_time"] = onTime;
    responseDoc["off_time"] = offTime;
    
    sendJsonSuccess(request, "Timer set successfully", &responseDoc);
    break;
  }
  case CMD_GET_TIME:
  {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
      sendJsonError(request, 500, "RTC Time invalid.");
      return;
    }

    char datum[11];  // YYYY-MM-DD
    char vrijeme[9]; // HH:MM:SS
    char dan[12];
    strftime(datum, sizeof(datum), "%Y-%m-%d", &timeinfo);
    strftime(vrijeme, sizeof(vrijeme), "%H:%M:%S", &timeinfo);
    strftime(dan, sizeof(dan), "%A", &timeinfo); // Puni naziv dana

    time_t epoch = mktime(&timeinfo);

    JsonDocument responseDoc;
    responseDoc["date"] = String(datum);
    responseDoc["time"] = String(vrijeme);
    responseDoc["day"] = String(dan);
    responseDoc["epoch"] = epoch;
    
    sendJsonSuccess(request, "Current time retrieved", &responseDoc);
    break;
  }
  case CMD_SET_TIME:
  {
    String date = request->hasParam("DATE") ? request->getParam("DATE")->value() : "";
    String timeStr = request->hasParam("TIME") ? request->getParam("TIME")->value() : "";

    if (date.length() != 6 || timeStr.length() != 6)
    {
      sendJsonError(request, 400, "Invalid DATE or TIME format. Expected DDMMYY and HHMMSS");
      break; // dodato umjesto return (jer je već u switch-case)
    }

    int day = date.substring(0, 2).toInt();
    int month = date.substring(2, 4).toInt();
    int year = date.substring(4, 6).toInt() + 2000;

    int hour = timeStr.substring(0, 2).toInt();
    int minute = timeStr.substring(2, 4).toInt();
    int second = timeStr.substring(4, 6).toInt();

    struct tm tm_time = {};
    tm_time.tm_year = year - 1900; // godina od 1900.
    tm_time.tm_mon = month - 1;    // mjesec od 0
    tm_time.tm_mday = day;
    tm_time.tm_hour = hour;
    tm_time.tm_min = minute;
    tm_time.tm_sec = second;
    tm_time.tm_isdst = -1; // automatska DST korekcija ako je podržana

    time_t t = mktime(&tm_time);
    if (t == -1)
    {
      sendJsonError(request, 500, "Failed to convert time.");
      break;
    }
    else
    {
      timeValid = true;
    }

    struct timeval now = {.tv_sec = t};
    settimeofday(&now, nullptr);

    JsonDocument responseDoc;
    responseDoc["date"] = date;
    responseDoc["time"] = timeStr;
    
    sendJsonSuccess(request, "RTC date and time set successfully", &responseDoc);
    break;
  }
  case CMD_OUTDOOR_LIGHT_OFF:
  case CMD_OUTDOOR_LIGHT_ON:
  {
    int new_state = (cmd == CMD_OUTDOOR_LIGHT_ON) ? HIGH : LOW;
    setOutdoorLightOverride(new_state);

    JsonDocument data;
    data["state"] = (new_state == HIGH) ? "ON" : "OFF";
    sendJsonSuccess(request, "Outdoor light state changed", &data);
    return;
  }
  case CMD_GET_PINGWDG:
  {
    JsonDocument data;
    data["enabled"] = pingWatchdogEnabled;
    sendJsonSuccess(request, "Ping watchdog state retrieved", &data);
    break;
  }
  case CMD_PINGWDG_ON:
  {
    pingWatchdogEnabled = true;
    preferences.begin("_pingwdg", false); // false = read/write
    preferences.putBool("pingwdg", true);
    preferences.end();
    
    JsonDocument data;
    data["enabled"] = true;
    sendJsonSuccess(request, "Ping watchdog enabled", &data);
    break;
  }
  case CMD_PINGWDG_OFF:
  {
    pingWatchdogEnabled = false;
    preferences.begin("_pingwdg", false); // false = read/write
    preferences.putBool("pingwdg", false);
    preferences.end();
    
    JsonDocument data;
    data["enabled"] = false;
    sendJsonSuccess(request, "Ping watchdog disabled", &data);
    break;
  }
  case CMD_GET_VERSION:
  {
    // Ako nema ID parametar -> ESP32 firmware info
    if (!request->hasParam("ID"))
    {
      char versionStr[32];
      sprintf(versionStr, "%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
      
      JsonDocument data;
      data["device_type"] = "ESP32";
      data["firmware_version"] = String(versionStr);
      data["build_number"] = VERSION_PATCH;
      data["build_date"] = BUILD_DATE;
      sendJsonSuccess(request, "ESP32 firmware version retrieved", &data);
      return;
    }
    
    // Ako ima ID parametar -> šalji GET_VERSION na STM32
    int id = request->getParam("ID")->value().toInt();
    
    if (id < 1 || id > 254)
    {
      sendJsonError(request, 400, "Invalid ID (must be 1-254)");
      return;
    }
    
    buf[0] = CMD_GET_VERSION;
    buf[1] = id;
    length = 2;
    break;
  }
  case CMD_GET_STATUS:
  {
    // Reset watchdog timer - dobili smo GET_STATUS komandu
    resetGetStatusWatchdog();
    
    isLocalCommand = true;  // ✅ Lokalna komanda, bez RS485
    
    time_t now;
    time(&now);
    struct tm utc_tm = *gmtime(&now);

    bool dstActive = isDST(utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday, utc_tm.tm_hour);
    int tzOffsetMinutes = dstActive ? 120 : 60;

    // Sun position
    sun.setPosition(LATITUDE, LONGITUDE, 0);
    sun.setCurrentDate(utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday);

    char sunriseStr[6] = "----";
    char sunsetStr[6] = "----";

    float sunriseUTC = sun.calcSunrise();
    float sunsetUTC = sun.calcSunset();

    if (!isnan(sunriseUTC) && !isnan(sunsetUTC))
    {
      int sunriseMin = ((int)sunriseUTC + tzOffsetMinutes + 1440) % 1440;
      int sunsetMin = ((int)sunsetUTC + tzOffsetMinutes + 1440) % 1440;

      sprintf(sunriseStr, "%02d:%02d", sunriseMin / 60, sunriseMin % 60);
      sprintf(sunsetStr, "%02d:%02d", sunsetMin / 60, sunsetMin % 60);
    }

    // Formatiraj ON/OFF vrijeme iz stringa "HHMM"
    char onStr[6] = "----";
    char offStr[6] = "----";
    if (timerOnTime.length() == 4)
      sprintf(onStr, "%02d:%02d", timerOnTime.substring(0, 2).toInt(), timerOnTime.substring(2, 4).toInt());
    if (timerOffTime.length() == 4)
      sprintf(offStr, "%02d:%02d", timerOffTime.substring(0, 2).toInt(), timerOffTime.substring(2, 4).toInt());

    // Wi-Fi info
    String ipStr = WiFi.localIP().toString();
    String ssidStr = WiFi.SSID();
    String passStr = WiFi.psk();

    // Relay
    bool relayState = digitalRead(LIGHT_PIN);

    // Format UTC time kao string
    char utcTimeStr[30];
    snprintf(utcTimeStr, sizeof(utcTimeStr), "%04d-%02d-%02d %02d:%02d:%02d",
             utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday,
             utc_tm.tm_hour, utc_tm.tm_min, utc_tm.tm_sec);

    // Kreiraj JSON response
    JsonDocument doc;
    
    // WiFi info
    doc["wifi"]["ssid"] = ssidStr;
    doc["wifi"]["password"] = String(passStr);
    doc["wifi"]["mdns"] = String(_mdns);
    doc["wifi"]["ip"] = ipStr;
    doc["wifi"]["port"] = _port;
    
    // Time info
    doc["time"]["utc"] = String(utcTimeStr);
    doc["time"]["timezone_offset_minutes"] = tzOffsetMinutes;
    doc["time"]["dst_active"] = dstActive;
    
    // Sun info
    doc["sun"]["sunrise"] = String(sunriseStr);
    doc["sun"]["sunset"] = String(sunsetStr);
    
    // Light control
    doc["light"]["relay_state"] = relayState ? "ON" : "OFF";
    doc["light"]["control_mode"] = overrideActive ? "MANUAL" : "AUTO";
    doc["light"]["timer_on_type"] = timerOnType;
    doc["light"]["timer_off_type"] = timerOffType;
    doc["light"]["on_time"] = String(onStr);
    doc["light"]["off_time"] = String(offStr);
    
    // Ping watchdog
    doc["ping_watchdog"] = pingWatchdogEnabled;
    
    // Thermostat
    doc["thermostat"]["temperature"] = emaTemperature;
    doc["thermostat"]["setpoint"] = th_setpoint;
    doc["thermostat"]["threshold"] = th_treshold;
    doc["thermostat"]["mode"] = th_mode == TH_HEATING ? "HEATING" : th_mode == TH_COOLING ? "COOLING" : "OFF";
    doc["thermostat"]["valve"] = digitalRead(VALVE) ? "ON" : "OFF";
    
    String fanState = "OFF";
    if (digitalRead(FAN_H)) fanState = "HIGH";
    else if (digitalRead(FAN_M)) fanState = "MEDIUM";
    else if (digitalRead(FAN_L)) fanState = "LOW";
    doc["thermostat"]["fan"] = fanState;
    doc["thermostat"]["ema_alpha"] = emaAlpha;
    doc["thermostat"]["fluid_temp"] = tempSensor2Available ? fluid : -999;
    doc["thermostat"]["fluid_available"] = tempSensor2Available;
    
    // SOS Status
    if (sosStatus) {
      preferences.begin("sos_event", true);
      unsigned long sosUnixTime = preferences.getULong("timestamp", 0);
      preferences.end();
      
      struct tm sosTime = *localtime((time_t*)&sosUnixTime);
      char sosTimeStr[20];
      snprintf(sosTimeStr, sizeof(sosTimeStr), "%04d-%02d-%02d %02d:%02d:%02d",
               sosTime.tm_year + 1900, sosTime.tm_mon + 1, sosTime.tm_mday,
               sosTime.tm_hour, sosTime.tm_min, sosTime.tm_sec);
      
      doc["sos"]["active"] = true;
      doc["sos"]["timestamp"] = String(sosTimeStr);
    } else {
      doc["sos"]["active"] = false;
    }
    
    // IR Remote Data
    preferences.begin("ir_settings", true);
    int proto = preferences.getInt("protocol", 0);
    preferences.end();

    doc["ir"]["protocol_id"] = proto;
    doc["ir"]["protocol_name"] = typeToString((decode_type_t)proto);

    JsonDocument finalDoc;
    finalDoc["status"] = "success";
    finalDoc["message"] = "System status retrieved";
    finalDoc["data"] = doc;

    String response;
    serializeJson(finalDoc, response);
    request->send(200, "application/json", response);
    break;
  }
  case CMD_ESP_GET_PINS:
  {
    String pinsStatus = getAvailablePinsStatus();
    
    JsonDocument responseDoc;
    responseDoc["pins_status"] = pinsStatus;
    
    sendJsonSuccess(request, "ESP32 pins status retrieved", &responseDoc);
    break;
  }
  case CMD_ESP_SET_PIN:
  {
    if (!request->hasParam("PIN"))
    {
      sendJsonError(request, 400, "Missing PIN parameter");
      return;
    }
    int _pin = request->getParam("PIN")->value().toInt();

    if (!isPinAvailable(_pin) || isInputOnlyPin(_pin))
    {
      sendJsonError(request, 400, "PIN is invalid, already in use or input-only");
      return;
    }

    setPinHigh(_pin);
    JsonDocument data;
    data["pin"] = _pin;
    data["state"] = "HIGH";
    sendJsonSuccess(request, "PIN set HIGH", &data);
    break;
  }
  case CMD_ESP_RESET_PIN:
  {
    if (!request->hasParam("PIN"))
    {
      sendJsonError(request, 400, "Missing PIN parameter");
      return;
    }
    int pin = request->getParam("PIN")->value().toInt();

    if (!isPinAvailable(pin) || isInputOnlyPin(pin))
    {
      sendJsonError(request, 400, "PIN is invalid, already in use or input-only");
      return;
    }

    setPinLow(pin);
    JsonDocument data;
    data["pin"] = pin;
    data["state"] = "LOW";
    sendJsonSuccess(request, "PIN set LOW", &data);
    break;
  }
  case CMD_ESP_PULSE_PIN:
  {
    if (!request->hasParam("PIN"))
    {
      sendJsonError(request, 400, "Missing PIN");
      return;
    }

    int pin = request->getParam("PIN")->value().toInt();

    if (!isPinAvailable(pin) || isInputOnlyPin(pin))
    {
      sendJsonError(request, 400, "PIN is invalid, already in use or input-only");
      return;
    }

    bool limit = false;
    int pauseSeconds = 2; // default
    if (request->hasParam("PAUSE"))
    {
      pauseSeconds = request->getParam("PAUSE")->value().toInt();
      if (pauseSeconds < 1 || pauseSeconds > 86400)
        pauseSeconds = 2; // sigurnosni limit
      limit = true;
    }

    startPulse(pin, pauseSeconds * 1000);
    
    JsonDocument data;
    data["pin"] = pin;
    data["duration_seconds"] = pauseSeconds;
    sendJsonSuccess(request, "PIN pulsed", &data);
    break;
  }
  case CMD_TH_SETPOINT:
  {
    if (!request->hasParam("VALUE"))
    {
      sendJsonError(request, 400, "Missing VALUE parameter for TH_SETPOINT");
      return;
    }
    int value = request->arg("VALUE").toInt();
    if (value < 5 || value > 40)
    {
      sendJsonError(request, 400, "Setpoint must be between 5 and 40");
      return;
    }
    th_setpoint = (float)value / 1.0f;
    preferences.begin("thermo", false);
    preferences.putFloat("setpoint", th_setpoint);
    preferences.end();
    
    JsonDocument data;
    data["setpoint"] = th_setpoint;
    sendJsonSuccess(request, "Thermostat setpoint updated", &data);
    return;
  }
  case CMD_TH_DIFF:
  {
    if (!request->hasArg("VALUE"))
    {
      sendJsonError(request, 400, "Missing VALUE parameter for TH_DIFF");
      return;
    }
    int value = request->arg("VALUE").toInt();
    if (value < 1 || value > 50)
    {
      sendJsonError(request, 400, "Threshold must be between 1 and 50 (0.1*C - 5.0*C)");
      return;
    }
    th_treshold = (float)value / 10.0f;
    preferences.begin("thermo", false);
    preferences.putFloat("treshold", th_treshold);
    preferences.end();
    
    JsonDocument data;
    data["threshold"] = th_treshold;
    sendJsonSuccess(request, "Thermostat threshold updated", &data);
    return;
  }
  case CMD_TH_STATUS:
  {
    String status = "Temp=" + String(emaTemperature, 1) + "*C\n";
    status += "Setpoint=" + String(th_setpoint, 1) + "*C\n";
    status += "Diff=" + String(th_treshold, 1) + "*C\n";
    status += "Mode=" + String(th_mode == TH_HEATING ? "HEATING" : th_mode == TH_COOLING ? "COOLING"
                                                                                         : "OFF") +
              "\n";
    status += "Pump/Valve=" + String(digitalRead(VALVE) ? "ON" : "OFF") + "\n";
    status += "Fan=";
    if (digitalRead(FAN_H))
    {
      status += "HIGH";
    }
    else if (digitalRead(FAN_M))
    {
      status += "MEDIUM";
    }
    else if (digitalRead(FAN_L))
    {
      status += "LOW";
    }
    else
    {
      status += "OFF";
    }
    
    JsonDocument responseDoc;
    responseDoc["status_text"] = status;
    responseDoc["ema_alpha"] = emaAlpha;
    responseDoc["fluid_temp"] = tempSensor2Available ? fluid : -999;
    responseDoc["fluid_available"] = tempSensor2Available;
    
    sendJsonSuccess(request, "Thermostat status retrieved", &responseDoc);
    return;
  }
  case CMD_TH_HEATING:
  {
    th_mode = TH_HEATING;
    th_saved_mode = TH_HEATING;
    preferences.begin("thermo", false);
    preferences.putInt("mode", (int)th_mode);
    preferences.putInt("th_saved_mode", (int)th_saved_mode);
    preferences.end();
    
    JsonDocument data;
    data["mode"] = "HEATING";
    sendJsonSuccess(request, "Thermostat mode set to HEATING", &data);
    return;
  }
  case CMD_TH_COOLING:
  {
    th_mode = TH_COOLING;
    th_saved_mode = TH_COOLING;
    preferences.begin("thermo", false);
    preferences.putInt("mode", (int)th_mode);
    preferences.putInt("th_saved_mode", (int)th_saved_mode);
    preferences.end();
    
    JsonDocument data;
    data["mode"] = "COOLING";
    sendJsonSuccess(request, "Thermostat mode set to COOLING", &data);
    return;
  }
  case CMD_TH_OFF:
  {
    th_saved_mode = th_mode;
    th_mode = TH_OFF;
    turnOffThermoRelays();
    preferences.begin("thermo", false);
    preferences.putInt("mode", (int)th_mode);
    preferences.putInt("th_saved_mode", (int)th_saved_mode);
    preferences.end();
    
    JsonDocument data;
    data["mode"] = "OFF";
    sendJsonSuccess(request, "Thermostat turned OFF", &data);
    return;
  }
  case CMD_TH_ON:
  {
    if (th_mode == TH_OFF)
    {
      preferences.begin("thermo", false);
      th_saved_mode = (ThMode)preferences.getInt("th_saved_mode", TH_HEATING); // ili TH_OFF kao default
      preferences.end();

      th_mode = th_saved_mode;
    }
    
    JsonDocument data;
    data["mode"] = th_mode == TH_HEATING ? "HEATING" : "COOLING";
    sendJsonSuccess(request, "Thermostat resumed", &data);
    return;
  }
  case CMD_TH_EMA:
  {
    if (!request->hasArg("VALUE"))
    {
      sendJsonError(request, 400, "Missing VALUE for TH_EMA");
      return;
    }

    String valStr = request->arg("VALUE");
    if (!valStr.equals(String(valStr.toInt()))) // jednostavna provjera da je cijeli broj
    {
      sendJsonError(request, 400, "VALUE must be an integer");
      return;
    }

    int value = valStr.toInt();
    if (value < 1 || value > 10)
    {
      sendJsonError(request, 400, "EMA must be between 1 and 10 (which corresponds to 0.1 - 1.0)");
      return;
    }

    emaAlpha = (float)value / 10.0f;
    preferences.begin("thermo", false);
    preferences.putFloat("emaAlpha", emaAlpha);
    preferences.end();
    
    JsonDocument data;
    data["ema_alpha"] = emaAlpha;
    sendJsonSuccess(request, "EMA filter updated", &data);
    return;
  }
  case CMD_SOS_RESET:
  {
    // Resetuj SOS status i obriši iz Preferences
    sosStatus = false;
    sosTimestamp = 0;
    
    preferences.begin("sos_event", false);
    preferences.clear(); // Briše sve iz sos_event namespace-a
    preferences.end();
    
    LOG_INFO_LN("✅ SOS događaj resetovan i obrisan iz memorije");
    sendJsonSuccess(request, "SOS event cleared from memory");
    return;
  }
  case CMD_SET_IR_PROTOCOL:
  {
    if (request->hasParam("VALUE")) {
        int proto = request->getParam("VALUE")->value().toInt();
        preferences.begin("ir_settings", false);
        preferences.putInt("protocol", proto);
        preferences.end();
        
        JsonDocument data;
        data["protocol_id"] = proto;
        data["protocol_name"] = typeToString((decode_type_t)proto);
        
        sendJsonSuccess(request, "IR Protocol set", &data);
    } else {
        sendJsonError(request, 400, "Missing VALUE parameter");
    }
    return;
  }
  case CMD_GET_IR_PROTOCOL:
  {
      preferences.begin("ir_settings", true);
      int proto = preferences.getInt("protocol", 0); // Default 0 (Disabled)
      preferences.end();
      
      JsonDocument data;
      data["protocol_id"] = proto;
      data["protocol_name"] = typeToString((decode_type_t)proto); 
      
      sendJsonSuccess(request, "IR Protocol retrieved", &data);
      return;
  }
  case CMD_SET_IR:
  {
      if (!request->hasParam("MOD")) {
          sendJsonError(request, 400, "Missing MOD parameter (OFF, HEATING, COOLING)");
          return;
      }
      
      String modeStr = request->getParam("MOD")->value();
      int temp = 25; // Default temp
      if (request->hasParam("VALUE")) {
          temp = request->getParam("VALUE")->value().toInt();
      }

      // 1. Učitaj konfigurisani protokol
      preferences.begin("ir_settings", true);
      int protoID = preferences.getInt("protocol", 0);
      preferences.end();

      if (protoID == 0) {
          sendJsonError(request, 400, "IR Protocol not configured. Use SET_IR_PROTOCOL first.");
          return;
      }

      // 2. Proveri da li je protokol podržan za AC
      if (!ac.isProtocolSupported((decode_type_t)protoID)) {
          LOG_ERROR("Protocol ID=%d (%s) NOT supported for AC!\n", 
                    protoID, typeToString((decode_type_t)protoID).c_str());
          sendJsonError(request, 400, "Protocol not supported. Use AC protocol: COOLIX, DAIKIN, MIDEA, GREE, etc.");
          return;
      }

      LOG_INFO("IR CMD: protocol=%d (%s) mode=%s temp=%d\n", 
               protoID, typeToString((decode_type_t)protoID).c_str(), modeStr.c_str(), temp);

      // 3. Podesi parametre na IRac objektu
      ac.next.protocol = (decode_type_t)protoID;
      ac.next.model = 1;
      ac.next.fanspeed = stdAc::fanspeed_t::kAuto;
      ac.next.celsius = true;

      if (modeStr == "OFF") {
          ac.next.power = false;
      } else if (modeStr == "HEATING") {
          ac.next.power = true;
          ac.next.mode = stdAc::opmode_t::kHeat;
          ac.next.degrees = temp;
      } else if (modeStr == "COOLING") {
          ac.next.power = true;
          ac.next.mode = stdAc::opmode_t::kCool;
          ac.next.degrees = temp;
      } else {
          sendJsonError(request, 400, "Invalid MOD. Use OFF, HEATING or COOLING");
          return;
      }

      // 4. Pošalji signal
      LOG_INFO("IR Sending on pin %d...\n", IR_PIN);
      bool result = ac.sendAc();
      LOG_INFO("IR Send result: %s\n", result ? "OK" : "FAIL");
      
      if (!result) {
          LOG_ERROR("Failed to send IR signal!\n");
      }

      JsonDocument data;
      data["protocol"] = typeToString(ac.next.protocol);
      data["power"] = ac.next.power ? "ON" : "OFF";
      data["mode"] = modeStr;
      data["temp"] = ac.next.degrees;
      
      sendJsonSuccess(request, "IR Command Sent", &data);
      return;
  }
  case CMD_SET_FWD_HEATING:
  case CMD_SET_FWD_COOLING:
  case CMD_SET_ENABLE_HEATING:
  case CMD_SET_ENABLE_COOLING:
  {
    if (!request->hasParam("ID") || !request->hasParam("VALUE"))
    {
      sendJsonError(request, 400, "Missing ID or VALUE parameter");
      return;
    }

    int id = request->getParam("ID")->value().toInt();
    int value = request->getParam("VALUE")->value().toInt();

    if (id < 1 || id > 254)
    {
      sendJsonError(request, 400, "Invalid ID (must be 1-254)");
      return;
    }

    if (value != 0 && value != 1)
    {
      sendJsonError(request, 400, "Invalid VALUE (must be 0 or 1)");
      return;
    }

    buf[0] = cmd;
    buf[1] = id;
    buf[2] = value;
    length = 3;
    break;
  }
  default:
    isLocalCommand = false;
    sendJsonError(request, 400, "Unknown command");
    return;
  }

  // ===== RUTA: Odredite da li je ovo lokalna komanda (ESP32) ili remote (RS485) =====
  if (isLocalCommand)
  {
    // Sve lokalne komande koje koriste return ili break su već obrađene
    // Ova sekcija je samo kao fallback ako se greška dogodi
    LOG_INFO_LN("[handleSysctrlRequest] Local command completed");
    return;
  }

  // ===== RUTA: Remote komande koje trebali RS485 bus =====
  if (buf[0] && length)
  {
    LOG_DEBUG("Sending command: ");
    for (int i = 0; i < length; i++)
    {
      if (buf[i] < 0x10)
        LOG_DEBUG("0");
      LOG_DEBUG_HEX(buf[i]);
      LOG_DEBUG(" ");
    }
    LOG_DEBUG_LN();
    
    // Blokiraj loop() od čitanja Serial2 ŠTO PRIJE!
    httpHandlerWaiting = true;
    
    // KLJUČNO: Očisti Serial2 buffer prije slanja komande!
    int flushed = 0;
    while (Serial2.available()) {
      Serial2.read();
      flushed++;
    }
    if (flushed > 0) {
      LOG_DEBUG_F(">>> Flushed %d old bytes from Serial2 buffer\n", flushed);
    }
    
    LOG_DEBUG_F(">>> Starting TF_QuerySimple, rdy=%d\n", TF_PARSER_TIMEOUT_TICKS * 10);
    // TF_SendSimple(&tfapp, S_CUSTOM, buf, length);
    TF_QuerySimple(&tfapp, S_CUSTOM, buf, length, ID_Listener, TF_PARSER_TIMEOUT_TICKS * 10);
    rdy = TF_PARSER_TIMEOUT_TICKS * 10;
    int bytesRead = 0;
    do
    {
      // WHILE ne IF - mora čitati SVE dostupne bajtove odmah!
      while (Serial2.available())
      {
        uint8_t b = Serial2.read();
        bytesRead++;
        LOG_DEBUG_F("[0x%02X]", b);
        TF_AcceptChar(&tfapp, b);
        
        // PREKINI ODMAH ako je ID_Listener postavio rdy = -1
        if (rdy < 0) {
          break;
        }
      }
      
      // Provjeri ponovo nakon while petlje
      if (rdy < 0) {
        break;
      }
      
      --rdy;
      delay(1);
      
      // Resetuj WDT tokom dugog čekanja na odgovor
      if (rdy % 10 == 0) {
        esp_task_wdt_reset();
      }
    } while (rdy > 0);
    
    // Deblokiraj loop()
    httpHandlerWaiting = false;
    
    LOG_DEBUG_F("\n>>> Wait loop finished, rdy=%d, bytes read=%d\n", rdy, bytesRead);

    if (rdy == 0)
    {
      LOG_ERROR_LN(">>> TIMEOUT detected!");
      
      // Očisti zaglavljen buffer
      httpHandlerWaiting = false;
      while (Serial2.available()) {
        Serial2.read();
      }
      Serial2.flush();
      
      // Loguj upozorenje o mogućem Command buffer error-u
      LOG_ERROR_LN("[WARNING] Timeout may have left TinyFrame in inconsistent state");
      LOG_ERROR_LN("[WARNING] If Command buffer error occurs on next request, device will auto-restart");
      
      sendJsonError(request, 408, "Timeout: No response from device");
    }
    else
    {
      LOG_INFO_LN(">>> Response received, processing...");
      JsonDocument responseDoc;

      // Special handling for QR_CODE_GET because response might not start with CMD byte
      if (cmd == CMD_QR_CODE_GET) {
         // Assuming the response is just the string data
         char qrBuf[129] = {0};
         memcpy(qrBuf, replyData, (replyDataLength < 128) ? replyDataLength : 128);
         responseDoc["qr_code"] = String(qrBuf);
      } else
      switch (replyData[0])
      {
      case CMD_GET_ROOM_TEMP:
        if (replyDataLength >= 3) { // Minimum response length for GET_ROOM_TEMP (CMD + room_temp + setpoint_temp)
          responseDoc["room_temperature"] = replyData[1];
          responseDoc["setpoint_temperature"] = replyData[2];

          if (replyDataLength >= 12) { // Full thermostat data from rs485_termostat.c (CMD + 11 data bytes)
            responseDoc["fan_speed"] = replyData[3];
            responseDoc["thermostat_control_mode"] = replyData[4];
            // Polja thermostat_state i setpoint_difference su uklonjena iz novog formata
            responseDoc["setpoint_max"] = replyData[5]; // Pomaknuto na index 5
            responseDoc["setpoint_min"] = replyData[6]; // Pomaknuto na index 6
            responseDoc["fan_control_mode"] = replyData[7]; // Pomaknuto na index 7
            responseDoc["forward_heating"] = (bool)replyData[8]; // Pomaknuto na index 8
            responseDoc["forward_cooling"] = (bool)replyData[9]; // Pomaknuto na index 9
            responseDoc["thst_enable_heating"] = (bool)replyData[10]; // Novo polje
            responseDoc["thst_enable_cooling"] = (bool)replyData[11]; // Novo polje
          } else { // Limited data from rs485_scene.c, set other fields to null
            responseDoc["fan_speed"] = nullptr;
            responseDoc["thermostat_control_mode"] = nullptr;
            // Polja thermostat_state i setpoint_difference su uklonjena
            // Postavi nova polja na null za rs485_scene.c
            responseDoc["setpoint_max"] = nullptr;
            responseDoc["setpoint_min"] = nullptr;
            responseDoc["fan_control_mode"] = nullptr;
            responseDoc["forward_heating"] = nullptr;
            responseDoc["forward_cooling"] = nullptr;
            responseDoc["thst_enable_heating"] = nullptr;
            responseDoc["thst_enable_cooling"] = nullptr;
          }
        } else {
          // Handle case where response is too short or invalid
          responseDoc["room_temperature"] = nullptr;
          responseDoc["setpoint_temperature"] = nullptr;
          responseDoc["fan_speed"] = nullptr;
          responseDoc["thermostat_control_mode"] = nullptr;
          responseDoc["thermostat_state"] = nullptr;
          responseDoc["setpoint_difference"] = nullptr;
          responseDoc["setpoint_max"] = nullptr;
          responseDoc["setpoint_min"] = nullptr;
          responseDoc["fan_control_mode"] = nullptr;
          responseDoc["forward_heating"] = nullptr;
          responseDoc["forward_cooling"] = nullptr;
          responseDoc["thst_enable_heating"] = nullptr;
          responseDoc["thst_enable_cooling"] = nullptr;
          responseDoc["error"] = "Incomplete GET_ROOM_TEMP response";
        }
        break;
      case 0xB1:
        if ((replyDataLength == 3) && (replyData[1] == 0xB2))
        {
          for (int i = 7; i >= 0; i--)
            bin += String(bitRead(replyData[2], i));
          responseDoc["pin_states"] = bin;
        }
        else
        {
          responseDoc["result"] = "Pin set OK";
        }
        break;

      case CMD_GET_FAN_DIFFERENCE:
        responseDoc["fan_difference"] = replyData[1];
        break;

      case CMD_GET_FAN_BAND:
        responseDoc["fan_low_band"] = replyData[1];
        responseDoc["fan_high_band"] = replyData[2];
        break;

      case CMD_GET_GUEST_IN_TEMP:
        responseDoc["guest_in_temperature"] = replyData[1];
        break;

      case CMD_GET_GUEST_OUT_TEMP:
        responseDoc["guest_out_temperature"] = replyData[1];
        break;

      case CMD_SET_PASSWORD:
        responseDoc["result"] = "Password set OK";
        break;

      case CMD_GET_PASSWORD:
      {
        if (replyDataLength < 2)
        {
          responseDoc["error"] = "Invalid response length";
          break;
        }
        
        uint8_t ack = replyData[1];
        
        if (ack != 0x06)  // ACK = 0x06
        {
          responseDoc["error"] = "Password read FAILED (NAK)";
          break;
        }
        
        // Provjeri tip korisnika prema dužini odgovora
        if (replyDataLength == 10)  // Guest: CMD + ACK + 8 bytes
        {
          uint8_t guestId = replyData[2];
          uint32_t password = ((uint32_t)replyData[3] << 16) | ((uint32_t)replyData[4] << 8) | replyData[5];
          uint32_t expiry = ((uint32_t)replyData[6] << 24) | ((uint32_t)replyData[7] << 16) | 
                            ((uint32_t)replyData[8] << 8) | replyData[9];
          
          // Konvertuj expiry Unix timestamp u datum
          time_t expiryTime = (time_t)expiry;
          struct tm *timeInfo = gmtime(&expiryTime);  // Koristi gmtime jer IC kontroler vraća UTC timestamp
          char expiryStr[20];
          strftime(expiryStr, sizeof(expiryStr), "%Y-%m-%d %H:%M", timeInfo);
          
          responseDoc["user_type"] = "guest";
          responseDoc["guest_id"] = guestId;
          responseDoc["password"] = password;
          responseDoc["expiry"] = String(expiryStr);
        }
        else if (replyDataLength == 5)  // Maid/Manager/Service: CMD + ACK + 3 bytes
        {
          uint32_t password = ((uint32_t)replyData[2] << 16) | ((uint32_t)replyData[3] << 8) | replyData[4];
          responseDoc["user_type"] = "staff";
          responseDoc["password"] = password;
        }
        else
        {
          responseDoc["error"] = "Unexpected response format (length=" + String(replyDataLength) + ")";
        }
        
        break;
      }

      case CMD_OPEN_DOOR:
        responseDoc["result"] = "Door opened";
        break;

      case CMD_SET_ROOM_TEMP:
      case CMD_SET_GUEST_IN_TEMP:
      case CMD_SET_GUEST_OUT_TEMP:
        responseDoc["result"] = "Temperature set OK";
        break;

      case CMD_SET_THST_ON:
      case CMD_SET_THST_OFF:
      case CMD_SET_THST_HEATING:
      case CMD_SET_THST_COOLING:
        responseDoc["result"] = "Thermostat set OK";
        break;

      case CMD_SET_FWD_HEATING:
        if (replyDataLength >= 2) {
          responseDoc["result"] = "Forward heating flag set";
          responseDoc["value"] = (bool)replyData[1];
        }
        break;

      case CMD_SET_FWD_COOLING:
        if (replyDataLength >= 2) {
          responseDoc["result"] = "Forward cooling flag set";
          responseDoc["value"] = (bool)replyData[1];
        }
        break;

      case CMD_SET_ENABLE_HEATING:
        if (replyDataLength >= 2) {
          responseDoc["result"] = "Enable heating flag set";
          responseDoc["value"] = (bool)replyData[1];
        }
        break;

      case CMD_SET_ENABLE_COOLING:
        if (replyDataLength >= 2) {
          responseDoc["result"] = "Enable cooling flag set";
          responseDoc["value"] = (bool)replyData[1];
        }
        break;

      case CMD_READ_LOG:
      {
        // Response format: [CMD][LOG_DSIZE][16-byte log data][device_addr_H][device_addr_L]
        // Total: 20 bytes
        if (replyDataLength < 20)
        {
          body += "Invalid response length (expected 20, got " + String(replyDataLength) + ")";
          break;
        }
        
        if (replyData[1] != 16)
        {
          body += "Invalid log data size (expected 16, got " + String(replyData[1]) + ")";
          break;
        }
        
        // Parse 16-byte log data (bytes 2-17)
        uint16_t logId = (replyData[2] << 8) | replyData[3];
        
        // Check if log list is empty (log_id = 0x0000 indicates LOGGER_EMPTY)
        if (logId == 0)
        {
          int deviceId = request->getParam("ID")->value().toInt();
          body = "{";
          body += "\"status\":\"success\",";
          body += "\"message\":\"Log list is empty\",";
          body += "\"data\":{";
          body += "\"status\":\"EMPTY\",";
          body += "\"device_id\":" + String(deviceId);
          body += "}}";
          request->send(200, "application/json", body);
          return;
        }
        
        uint8_t logEvent = replyData[4];
        uint8_t logType = replyData[5];
        uint8_t logGroup = replyData[6];
        
        // Card ID (5 bytes) - from log bytes [5-9]
        char cardIdHex[11];
        sprintf(cardIdHex, "%02X%02X%02X%02X%02X", 
                replyData[7], replyData[8], replyData[9], replyData[10], replyData[11]);
        
        // Debug: Print raw bytes to Serial
        LOG_DEBUG("LOG RAW DATA: ");
        for (int i = 2; i < 18; i++) {
          if (replyData[i] < 0x10) LOG_DEBUG("0");
          LOG_DEBUG_HEX(replyData[i]);
          LOG_DEBUG(" ");
        }
        LOG_DEBUG_LN();
        
        // Date/Time (BCD format)
        uint8_t day = bcdToDec(replyData[12]);
        uint8_t month = bcdToDec(replyData[13]);
        uint8_t year = bcdToDec(replyData[14]);
        uint8_t hour = bcdToDec(replyData[15]);
        uint8_t minute = bcdToDec(replyData[16]);
        uint8_t second = bcdToDec(replyData[17]);
        
        // Format date and time strings
        char dateStr[12];
        char timeStr[9];
        sprintf(dateStr, "%02d.%02d.20%02d", day, month, year);
        sprintf(timeStr, "%02d:%02d:%02d", hour, minute, second);
        
        // Device ID from request parameter
        int deviceId = request->getParam("ID")->value().toInt();
        
        // Build JSON response
        body = "{";
        body += "\"status\":\"success\",";
        body += "\"data\":{";
        body += "\"status\":\"OK\",";
        body += "\"device_id\":" + String(deviceId) + ",";
        body += "\"log_id\":" + String(logId) + ",";
        body += "\"event_code\":\"0x" + String(logEvent, HEX) + "\",";
        body += "\"event_name\":\"" + getEventName(logEvent) + "\",";
        body += "\"event_description\":\"" + getAccessDescription(logEvent, logGroup) + "\",";
        body += "\"type\":" + String(logType) + ",";
        body += "\"group\":" + String(logGroup) + ",";
        body += "\"card_id\":\"" + String(cardIdHex) + "\",";
        body += "\"date\":\"" + String(dateStr) + "\",";
        body += "\"time\":\"" + String(timeStr) + "\",";
        body += "\"timestamp\":\"" + String(dateStr) + " " + String(timeStr) + "\"";
        body += "}}";
        
        request->send(200, "application/json", body);
        return;
      }

      case CMD_DELETE_LOG:
      {
        // Response format: [CMD][Status][device_addr_H][device_addr_L]
        // Status byte: 0 = LOGGER_OK, 1 = LOGGER_EMPTY
        // Total: 4 bytes
        if (replyDataLength < 4)
        {
          body += "Invalid response length (expected 4, got " + String(replyDataLength) + ")";
          break;
        }
        
        uint8_t status = replyData[1];  // LOGGER_OK=0, LOGGER_EMPTY=1
        int deviceId = request->getParam("ID")->value().toInt();
        
        // Build JSON response
        body = "{";
        body += "\"status\":\"success\",";
        body += "\"message\":\"" + String(status == 0 ? "Log deleted successfully" : "Log list is empty") + "\",";
        body += "\"data\":{";
        body += "\"status\":\"" + String(status == 0 ? "OK" : "EMPTY") + "\",";
        body += "\"device_id\":" + String(deviceId);
        body += "}}";
        
        request->send(200, "application/json", body);
        return;
      }
      
      case CMD_SET_LANG:
        responseDoc["result"] = "Language set OK";
        break;

      case CMD_QR_CODE_SET:
        if (replyDataLength >= 2 && replyData[1] == 0x06) // ACK
            responseDoc["result"] = "QR code set OK";
        else
            responseDoc["result"] = "QR code set FAILED";
        break;

      case CMD_GET_ROOM_STATUS:
        if (replyDataLength >= 2) {
            bool cardIn = replyData[1];
            responseDoc["room_status"] = cardIn ? "GUEST_IN" : "EMPTY";
            responseDoc["card_inserted"] = cardIn;
        }
        break;

      case CMD_RESTART_CTRL:
        responseDoc["result"] = "Controller restart OK";
        break;
      
      case CMD_SET_SYSID:
        if (replyDataLength >= 2 && replyData[1] == 0x06) // ACK
          responseDoc["result"] = "System ID set OK";
        else
          responseDoc["result"] = "System ID set FAILED";
        break;

      case CMD_GET_SYSID:
        if (replyDataLength >= 3)
        {
          uint16_t sysidVal = (replyData[1] << 8) | replyData[2];
          char hexStr[10];
          sprintf(hexStr, "0x%04X", sysidVal);
          responseDoc["system_id"] = sysidVal;
          responseDoc["system_id_hex"] = String(hexStr);
        }
        else
        {
          responseDoc["error"] = "Invalid response length";
        }
        break;

      case CMD_GET_VERSION:
      {
        // Response format: [CMD][bootloader_ver][app_ver][bldr_backup_ver][app_backup_ver][new_file_ver]
        // Each version is 4 bytes (big-endian uint32_t)
        // Total: 1 + 5*4 = 21 bytes
        if (replyDataLength < 21)
        {
          responseDoc["error"] = "Invalid response length (expected 21, got " + String(replyDataLength) + ")";
          break;
        }

        // Helper lambda to parse 4-byte version
        auto parseVersion = [](uint8_t* data, int offset) -> uint32_t {
          return ((uint32_t)data[offset] << 24) | 
                 ((uint32_t)data[offset + 1] << 16) | 
                 ((uint32_t)data[offset + 2] << 8) | 
                 data[offset + 3];
        };

        // Parse 5 firmware versions
        uint32_t bootloaderVer = parseVersion(replyData, 1);
        uint32_t applicationVer = parseVersion(replyData, 5);
        uint32_t bootloaderBackupVer = parseVersion(replyData, 9);
        uint32_t applicationBackupVer = parseVersion(replyData, 13);
        uint32_t newFileVer = parseVersion(replyData, 17);

        // Format versions as hex strings
        char hexBuf[16];
        
        if (bootloaderVer != 0) {
          sprintf(hexBuf, "0x%08X", bootloaderVer);
          responseDoc["bootloader_version"] = String(hexBuf);
        } else {
          responseDoc["bootloader_version"] = nullptr;
        }

        if (applicationVer != 0) {
          sprintf(hexBuf, "0x%08X", applicationVer);
          responseDoc["application_version"] = String(hexBuf);
        } else {
          responseDoc["application_version"] = nullptr;
        }

        if (bootloaderBackupVer != 0) {
          sprintf(hexBuf, "0x%08X", bootloaderBackupVer);
          responseDoc["bootloader_backup_version"] = String(hexBuf);
        } else {
          responseDoc["bootloader_backup_version"] = nullptr;
        }

        if (applicationBackupVer != 0) {
          sprintf(hexBuf, "0x%08X", applicationBackupVer);
          responseDoc["application_backup_version"] = String(hexBuf);
        } else {
          responseDoc["application_backup_version"] = nullptr;
        }

        if (newFileVer != 0) {
          sprintf(hexBuf, "0x%08X", newFileVer);
          responseDoc["new_file_version"] = String(hexBuf);
        } else {
          responseDoc["new_file_version"] = nullptr;
        }

        break;
      }

      default:
      {
        char hexCmd[5];
        sprintf(hexCmd, "0x%02X", replyData[0]);
        responseDoc["error"] = "Unknown response";
        responseDoc["command"] = String(hexCmd);
        
        String hexData = "";
        for (size_t i = 0; i < replyDataLength; i++)
        {
          if (replyData[i] < 0x10)
            hexData += "0";
          hexData += String(replyData[i], HEX);
          hexData += " ";
        }
        responseDoc["raw_data"] = hexData;
        responseDoc["data_length"] = replyDataLength;
        break;
      }
      }

      JsonDocument finalDoc;
      if (responseDoc["error"].is<const char*>()) {
        finalDoc["status"] = "error";
        finalDoc["message"] = responseDoc["error"];
        finalDoc["code"] = 500;
      } else {
        finalDoc["status"] = "success";
        finalDoc["data"] = responseDoc;
      }

      String response;
      serializeJson(finalDoc, response);
      request->send(200, "application/json", response);
    }
  }
  else
  {
    // KRITIČNA GREŠKA - Komunikacija je zaglavljena
    LOG_ERROR_LN("==========================================");
    LOG_ERROR_LN("[CRITICAL] Command buffer error detected!");
    LOG_ERROR_LN("[CRITICAL] RS485/TinyFrame communication is stuck!");
    LOG_ERROR_LN("[CRITICAL] Device will restart in 2 seconds...");
    LOG_ERROR_LN("==========================================" );
    
    // Loguj debug info
    LOG_ERROR("[DEBUG] buf[0]=0x%02X, length=%d, isLocalCommand=%d\n", 
                buf[0], length, isLocalCommand);
    
    sendJsonError(request, 500, "Command buffer error - device restarting");
    
    // Resetuj WDT prije restarty
    esp_task_wdt_reset();
    
    // Čekaj malo da klijent primi odgovor
    delay(500);
    
    // Reinicijalizuj prije ponovnog pokušaja
    LOG_INFO_LN(">>> Attempting TinyFrame/Serial2 reinitialization...");
    
    // Očisti Serial2 buffer
    while (Serial2.available()) {
      Serial2.read();
    }
    Serial2.flush();
    
    // Restart kompletnog sistema
    LOG_ERROR_LN(">>> Performing hard reset of device");
    delay(1000);
    ESP.restart();
  }
}
/**
 *  TINYFRAME NA RS485 BUS TRANSMITER
 */
void TF_WriteImpl(TinyFrame *const tf, const uint8_t *buff, uint32_t len)
{
  digitalWrite(RS485_DE_PIN, HIGH);

  Serial2.write(buff, len);
  Serial2.flush(); // čekaj da svi bajtovi izađu iz UART-a

  digitalWrite(RS485_DE_PIN, LOW);
}
/**
 * KONVERTOR SA UINT8 NA BCD FORMAT
 */
uint8_t toBCD(uint8_t val)
{
  return ((val / 10) << 4) | (val % 10);
}
/**
 *  UPDATE DATUMA I VREMENA UREĐAJA NA RS485 BUSU
 */
void sendRtcToBus()
{
  if (timeValid == false)
    return;

  time_t rawTime = time(nullptr);

  struct tm *timeInfo = localtime(&rawTime);

  if (timeInfo == nullptr)
  {
    LOG_ERROR_LN("[RTC SEND] Greška: timeInfo je nullptr");
    return;
  }

  uint8_t buf[9];
  buf[0] = SET_RTC_DATE_TIME;
  buf[1] = DEF_TFBRA;
  buf[2] = toBCD(timeInfo->tm_wday == 0 ? 7 : timeInfo->tm_wday); // RTC: Sunday = 7
  buf[3] = toBCD(timeInfo->tm_mday);                              // Dan u BCD
  buf[4] = toBCD(timeInfo->tm_mon + 1);                           // Mjesec u BCD
  buf[5] = toBCD(timeInfo->tm_year % 100);                        // Godina u BCD (npr. 2024 → 24)
  buf[6] = toBCD(timeInfo->tm_hour);                              // Sat u BCD
  buf[7] = toBCD(timeInfo->tm_min);                               // Minut u BCD
  buf[8] = toBCD(timeInfo->tm_sec);                               // Sekund u BCD

  LOG_DEBUG("RTC Buf: ");
  for (int i = 0; i < 9; i++)
  {
    LOG_DEBUG_F("%02X ", buf[i]);
  }
  LOG_DEBUG_LN();

  bool sent = TF_SendSimple(&tfapp, S_CUSTOM, buf, sizeof(buf));
  if (!sent)
  {
    LOG_ERROR_LN("RTC Update ERROR !");
  }
  else
  {
    LOG_INFO_LN("RTC Update OK");
  }
}
/**
 * WIFI KONEKCIJA
 */
void tryConnectWiFi()
{
  preferences.begin("wifi", false);
  preferences.getString("ssid", _ssid, sizeof(_ssid));
  preferences.getString("password", _pass, sizeof(_pass));
  preferences.end();

  WiFiManager wm;
  wm.setConfigPortalTimeout(60);

  bool connected = false;

  if (strlen(_ssid) > 0) // ako postoji SSID, pokušaj se spojiti
  {
    if (strlen(_pass) > 0)
    {
      WiFi.begin(_ssid, _pass);
      LOG_INFO("🔌 Connecting on WiFi (with password): ");
      LOG_INFO_LN("%s", _ssid);
    }
    else
    {
      WiFi.begin(_ssid); // pokušaj spojiti bez lozinke
      LOG_INFO("🔌 Connecting on WiFi (no password): "); // NOLINT(performance-avoid-endl)
      LOG_INFO_LN("%s", _ssid);
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
    {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      connected = true;
      LOG_INFO_LN("✅ Connected on previous network !");
      LOG_INFO_LN("%s", WiFi.localIP().toString().c_str());
    }
    else
    {
      LOG_ERROR_LN("❌ Not connected on previous network.");
    }
  }

  if (!connected)
  {
    LOG_INFO_LN("📶 Starting WiFiManager portal...");

    connected = wm.autoConnect("WiFiManager");

    if (connected)
    {
      LOG_INFO_LN("✅ Connected with portal!");
      LOG_INFO_LN("%s", WiFi.localIP().toString().c_str());

      preferences.begin("wifi", false);
      preferences.putString("ssid", WiFi.SSID());
      preferences.putString("password", WiFi.psk());
      preferences.end();
    }
    else
    {
      LOG_ERROR_LN("❌ Not connected. Restart...");
      delay(3000);
      ESP.restart();
    }
  }
}
/**
 * PING GOOGLE SERVER
 */
bool pingGoogle()
{
  // koristi `ping` preko `esp_ping` biblioteke ako koristiš ESP32
  // ili ručno preko `WiFiClient` ako ne koristiš ping biblioteku
  WiFiClient client;
  return client.connect("8.8.8.8", 53); // DNS port (radi brže nego ICMP ping)
}
/**
 * SETUP
 */
void setup()
{
  // esp_task_wdt_init(WDT_TIMEOUT, true); // init watchdog, true = reset na timeout
  // esp_task_wdt_add(NULL);

  Serial.begin(115200);
  Serial2.begin(115200);

  // Init VSPI for External Flash
  LOG_INFO_LN("\n\n=== Initializing SPI Flash ===");
  LOG_INFO("CS Pin: %d, SCK: %d, MISO: %d, MOSI: %d\n", FLASH_CS, FLASH_SCK, FLASH_MISO, FLASH_MOSI);
  
  vspi.begin(FLASH_SCK, FLASH_MISO, FLASH_MOSI, -1); // -1 = manual CS control
  delay(100); // Give SPI time to stabilize
  
  if (extFlash.begin()) {
    LOG_INFO_LN("External Flash Initialized");
  } else {
    LOG_ERROR_LN("External Flash Init FAILED");
  }

  pinMode(BOOT_PIN, INPUT_PULLUP);
  pinMode(RS485_DE_PIN, OUTPUT);
  digitalWrite(RS485_DE_PIN, LOW);
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, LOW);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(FAN_L, OUTPUT);
  digitalWrite(FAN_L, LOW);
  pinMode(FAN_M, OUTPUT);
  digitalWrite(FAN_M, LOW);
  pinMode(FAN_H, OUTPUT);
  digitalWrite(FAN_H, LOW);
  pinMode(VALVE, OUTPUT);
  digitalWrite(VALVE, LOW);

  // IR Transmitter Init
  pinMode(IR_PIN, OUTPUT);
  digitalWrite(IR_PIN, LOW);
  ac.next.protocol = decode_type_t::GREE; // Default fallback, overridden by prefs

  preferences.begin("_port", false);
  _port = preferences.getInt("port", 80);
  preferences.end();

  preferences.begin("_mdns", false);
  String mdns_str = preferences.getString("mdns", "soba");
  strncpy(_mdns, mdns_str.c_str(), sizeof(_mdns) - 1);
  preferences.end();

  preferences.begin("_pingwdg", true); // true = read-only
  pingWatchdogEnabled = preferences.getBool("pingwdg", false);
  preferences.end();

  sensors.begin();
  tempSensorAvailable = sensors.getDeviceCount() > 0;

  if (tempSensorAvailable)
  {
    loadThermoPreferences();
    th_mode = th_saved_mode;
    sensors.setResolution(12);
    sensors.requestTemperatures();
  }
  else
  {
    LOG_ERROR_LN("Room Temperature sensor NOT found!");
    turnOffThermoRelays(); // sigurnosno
    th_mode = TH_OFF;      // i postavi mod u OFF
  }

  sensors2.begin();
  tempSensor2Available = sensors2.getDeviceCount() > 0;

  if (tempSensor2Available)
  {
    sensors2.setResolution(12);
    sensors2.requestTemperatures();
  }
  else
  {
    LOG_ERROR_LN("\nFluid Temperature sensor NOT found!");
  }

  server = std::unique_ptr<AsyncWebServer>(new AsyncWebServer(_port)); // Dinamička alokacija servera s portom iz Preferences

  TF_InitStatic(&tfapp, TF_MASTER);
  
  // Registruj listenere za SOS i IR događaje
  TF_AddTypeListener(&tfapp, S_SOS, SOS_Listener);
  TF_AddTypeListener(&tfapp, S_IR, IR_Listener);
  // Registruj Firmware Update Listener
  TF_AddTypeListener(&tfapp, TF_TYPE_FIRMWARE_UPDATE, UpdateService_Listener);
  LOG_INFO_LN("✅ TinyFrame listeneri registrovani: S_SOS, S_IR, FW_UPDATE");
  
  // Učitaj SOS status iz Preferences (perzistentnost)
  preferences.begin("sos_event", true);
  sosStatus = preferences.getBool("active", false);
  if (sosStatus) {
    unsigned long sosUnixTime = preferences.getULong("timestamp", 0);
    sosTimestamp = millis(); // Reset millis() timestamp za internal tracking
    LOG_INFO("⚠️ SOS događaj učitan iz memorije (timestamp: %lu)\n", sosUnixTime);
  }
  preferences.end();
  
  rtcSyncTicker.attach(60.0, sendRtcToBus);

  tryConnectWiFi(); // 👈 Ovde se sada samo poziva čista funkcija

  if (!MDNS.begin(_mdns))
    LOG_ERROR_LN("⚠️ mDNS not started!");
  else
  {
    MDNS.addService("http", "tcp", _port);
    LOG_INFO_LN("🌐 mDNS responder started. Available on http://%s.local:%d", _mdns, _port);
  }

  configTzTime(TIMEZONE, "pool.ntp.org");
  // Čekaj da se vreme sinhronizuje
  unsigned long start = millis();
  while (time(nullptr) < 100000 && millis() - start < 10000)
  {
    LOG_INFO(".");
    delay(500);
  }
  if (time(nullptr) < 100000)
  {
    timeValid = false;
    LOG_ERROR_LN("Time sync failed!");
  }
  else
  {
    timeValid = true;
    LOG_INFO_LN("Time synchronized!");
  }

  loadTimerPreferences();
  setupLightControl();
  updateLightState();

  server->on("/sysctrl.cgi", HTTP_GET, handleSysctrlRequest);  // Handler za sysctrl.cgi
  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) // Osnovni endpoint root
             { sendJsonError(request, 200, "Online"); });
  server->on("/update", HTTP_GET, [](AsyncWebServerRequest *request) // /update GET vraća isto kao root (bez forme)
             { sendJsonError(request, 200, "Online"); });
  server->on( // Obrada OTA upload POST zahteva
      "/update", HTTP_POST, [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, String filename, size_t index,
         uint8_t *data, size_t len, bool final)
      {
        if (!index)
        {
          LOG_INFO("Update begin: %s\n", filename.c_str());
          if (!Update.begin())
          {
            Update.printError(Serial);
          }
        }
        if (!Update.hasError())
        {
          if (Update.write(data, len) != len)
          {
            Update.printError(Serial);
          }
        }
        if (final)
        {
          if (Update.end(true))
          {
            LOG_INFO("Update finished, size: %u\n", index + len);
          }
          else
          {
            Update.printError(Serial);
          }
        }
      });
  // --- FIRMWARE UPDATE ENDPOINTS ---

  // 1. Upload Firmware File to External Flash Slot
  server->on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Ovaj handler se izvršava NA KRAJU, nakon što je upload završen
    LOG_INFO_LN("Upload: Request Finished.");
    
    // Provjeri parametre primljene u zahtjevu
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      LOG_DEBUG_F("Upload: Param[%s] = %s\n", p->name().c_str(), p->value().c_str());
    }

    if (!request->hasParam("slot", true) && !request->hasParam("slot")) { // Provjeri i POST i GET
        LOG_ERROR_LN("Upload: ERROR - Missing 'slot' parameter");
        sendJsonError(request, 400, "Missing 'slot' parameter (0-7)");
        return;
    }
    
    // Uzmi slot (preferiraj POST body, pa onda URL query)
    AsyncWebParameter* p = request->hasParam("slot", true) ? request->getParam("slot", true) : request->getParam("slot");
    int slot = p->value().toInt();
    
    if (slot < 0 || slot >= FW_SLOT_COUNT) {
        LOG_ERROR("Upload: ERROR - Invalid slot %d\n", slot);
        sendJsonError(request, 400, "Invalid slot (0-7)");
        return;
    }
    
    sendJsonSuccess(request, "Upload Complete");
  }, 
  [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    // Static variables to hold state across multiple chunks of the same upload
    static int uploadSlot = -1;
    static uint32_t uploadOffset = 0;
    static unsigned long startTime = 0;
    static String uploadFilename = "";

    if (index == 0) {
        startTime = millis();
        uploadFilename = filename;
        LOG_INFO("Upload: START filename='%s'\n", filename.c_str());
        
        // Moramo ručno tražiti parametar jer request->getParam() možda nije spreman u ovom trenutku upload handlera
        // ESPAsyncWebServer parsira parametre dok stižu. 
        // HACK: Ako koristimo curl -F "slot=0" -F "file=@...", parametar 'slot' možda stigne PRIJE ili POSLIJE fajla.
        // Sigurnije je proslijediti slot u URL-u: /upload?slot=0
        
        if (request->hasParam("slot")) {
             uploadSlot = request->getParam("slot")->value().toInt();
             LOG_DEBUG_F("Upload: Slot detected in URL: %d\n", uploadSlot);
        } else {
             // Ako nije u URL, nadamo se da je stigao prije fajla u body-u (multipart)
             // Ovo je nepouzdano kod multiparta, ali pokušajmo
             // Za sada, ispišimo upozorenje korisniku da koristi URL parametar
             LOG_INFO_LN("Upload: WARNING - 'slot' not found in URL query. Checking body...");
             // Defaultamo na -1 i čekamo kraj
             uploadSlot = -1; 
        }

        // Dodatna provjera za multipart parametre koji su već stigli
        int params = request->params();
        for(int i=0;i<params;i++){
             AsyncWebParameter* p = request->getParam(i);
             if(p->name() == "slot") {
                 uploadSlot = p->value().toInt();
                 LOG_DEBUG_F("Upload: Slot found in multipart body: %d\n", uploadSlot);
             }
        }

        if (uploadSlot >= 0 && uploadSlot < FW_SLOT_COUNT) {
            if (uploadSlot < 4) { // Standard firmware slots
                LOG_INFO("Upload: Erasing Slot %d... (This may take time)\n", uploadSlot);
                extFlash.eraseSlot(uploadSlot);
                
                // VERIFY ERASE
                uint8_t checkBuf[16];
                extFlash.readBufferFromSlot(uploadSlot, 0, checkBuf, 16);
                bool erased = true;
                for(int k=0; k<16; k++) if(checkBuf[k] != 0xFF) erased = false;
                
                if(erased) {
                    LOG_INFO_LN("Upload: Erase VERIFIED (First 16 bytes are 0xFF). Writing...");
                } else {
                    LOG_ERROR("Upload: Erase FAILED! First byte: 0x%02X\n", checkBuf[0]);
                }
                uploadOffset = 0;
            } else {
                // Raw binary slots (4-7)
                LOG_INFO("Upload: Erasing Slot %d for raw binary... (This may take time)\n", uploadSlot);
                extFlash.eraseSlot(uploadSlot); // Erase the whole slot first
                uploadOffset = 0; // Reset offset, we will calculate it based on index for raw slots
            }
        } else {
            LOG_ERROR_LN("Upload: ERROR - Invalid or Missing Slot at start of upload. Use /upload?slot=X");
        }
        
        uploadOffset = 0;
    }

    // Piši samo ako imamo validan slot
    if (uploadSlot >= 0 && uploadSlot < FW_SLOT_COUNT) {
        uint32_t currentWriteOffset = index;
        if (uploadSlot >= 4) {
            // For raw slots, the file data starts after the header space
            currentWriteOffset = RAW_SLOT_DATA_OFFSET + index;
        }
        if (!extFlash.writeBufferToSlot(uploadSlot, currentWriteOffset, data, len)) {
             LOG_ERROR("Upload: WRITE ERROR at offset %u\n", uploadOffset);
        }
        uploadOffset += len;
        
        // Log svakih 50KB da vidimo da je živo
        if ((uploadOffset % 51200) < len) {
             LOG_INFO("Upload: %u bytes written...\n", uploadOffset);
        }
    }

    if (final) {
        unsigned long duration = millis() - startTime;
        uint32_t finalSize = index + len;
        LOG_INFO("Upload: END. Total %u bytes in %lu ms. Slot used: %d\n", finalSize, duration, uploadSlot);

        if (uploadSlot == -1) {
            LOG_ERROR_LN("Upload: FAILED - Slot was never identified.");
            return;
        }

        // Post-upload validation and metadata writing
        if (uploadSlot < 4) { // Standard firmware slots
            FwInfoTypeDef info;
            extFlash.getSlotInfo(uploadSlot, &info);
            if (info.size != finalSize) {
                LOG_ERROR("Upload VALIDATION FAILED: File size mismatch! Header: %u, Actual: %u\n", info.size, finalSize);
                // Optionally, invalidate the slot here
            } else {
                LOG_INFO_LN("Upload VALIDATION PASSED: File size matches header.");
            }
        } else { // Raw binary slots
            LOG_INFO_LN("Upload: Writing metadata for raw slot...");
            RawSlotInfoTypeDef rawInfo;
            memset(&rawInfo, 0, sizeof(RawSlotInfoTypeDef));

            rawInfo.magic = RAW_SLOT_MAGIC;
            rawInfo.size = finalSize;
            strlcpy(rawInfo.filename, uploadFilename.c_str(), sizeof(rawInfo.filename));

            // Calculate CRC32 of the written file
            uint32_t crc = 0;
            uint8_t crc_buf[256];
            for (uint32_t offset = 0; offset < finalSize; offset += sizeof(crc_buf)) {
                size_t to_read = (finalSize - offset < sizeof(crc_buf)) ? (finalSize - offset) : sizeof(crc_buf);
                extFlash.readBufferFromSlot(uploadSlot, RAW_SLOT_DATA_OFFSET + offset, crc_buf, to_read);
                crc = crc32_update(crc, crc_buf, to_read);
            }
            rawInfo.crc32 = crc;
            rawInfo.valid = 1;

            extFlash.writeBufferToSlot(uploadSlot, RAW_SLOT_HEADER_OFFSET, (uint8_t*)&rawInfo, sizeof(RawSlotInfoTypeDef));
            LOG_INFO("Upload: Raw metadata written. CRC: 0x%08X\n", rawInfo.crc32);
        }
    }
  });

  // 2. List Slots Status
  server->on("/slots", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    JsonArray slots = doc["slots"].to<JsonArray>();
    
    for (int i = 0; i < FW_SLOT_COUNT; i++) {
        JsonObject slotObj = slots.add<JsonObject>();
        slotObj["id"] = i;

        if (i < 4) { // Standard Firmware Slots
            slotObj["type"] = "firmware";
            FwInfoTypeDef info;
            if (extFlash.getSlotInfo(i, &info)) {
                slotObj["size"] = info.size;
                slotObj["version"] = info.version;
                slotObj["crc32"] = info.crc32;
                
                // Stricter validation
                bool isValid = true;
                if (info.size == 0 || info.size == 0xFFFFFFFF || info.size > FW_SLOT_SIZE) isValid = false;
                if (info.version == 0 || info.version == 0xFFFFFFFF) isValid = false;
                
                // Validate firmware type from version's MSB
                uint8_t fw_type = (info.version >> 24) & 0xFF;
                if (isValid && !((fw_type >= 0x10 && fw_type <= 0x19) || // HC
                                 (fw_type >= 0x20 && fw_type <= 0x29) || // RC
                                 (fw_type >= 0x30 && fw_type <= 0x39) || // RT
                                 (fw_type >= 0x40 && fw_type <= 0x49))) { // CS
                    isValid = false;
                }
                slotObj["valid"] = isValid;
            } else {
                slotObj["error"] = "Read Failed";
                slotObj["valid"] = false;
            }
        } else { // Raw Binary Slots
            slotObj["type"] = "raw_binary";
            RawSlotInfoTypeDef rawInfo;
            if (extFlash.readBufferFromSlot(i, RAW_SLOT_HEADER_OFFSET, (uint8_t*)&rawInfo, sizeof(RawSlotInfoTypeDef))) {
                if (rawInfo.magic == RAW_SLOT_MAGIC && rawInfo.valid == 1) {
                    slotObj["filename"] = rawInfo.filename;
                    slotObj["size"] = rawInfo.size;
                    slotObj["crc32"] = rawInfo.crc32;
                    slotObj["valid"] = true;
                } else {
                    slotObj["valid"] = false;
                }
            } else {
                slotObj["error"] = "Read Failed";
                slotObj["valid"] = false;
            }
        }
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // 3. Start Update Process
  server->on("/start_update", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Robust check: Look in default locations (Query/Body) AND explicitly in POST Body
    bool hasSlot = request->hasParam("slot") || request->hasParam("slot", true);
    bool hasAddr = request->hasParam("addr") || request->hasParam("addr", true);

    if (!hasSlot || !hasAddr) {
        sendJsonError(request, 400, "Missing slot or addr");
        return;
    }
    
    // Retrieve values (Prioritize POST body if available)
    AsyncWebParameter* pSlot = request->hasParam("slot", true) ? request->getParam("slot", true) : request->getParam("slot");
    AsyncWebParameter* pAddr = request->hasParam("addr", true) ? request->getParam("addr", true) : request->getParam("addr");
    
    int slot = pSlot->value().toInt();
    int addr = pAddr->value().toInt();

    // --- LOGIKA ADRESIRANJA PO SLOTOVIMA ---
    const uint32_t ADDR_FW_STAGING = 0x90F00000; // RT_NEW_FILE_ADDR (Firmware)
    const uint32_t ADDR_EXT_FLASH  = 0x90000000; // EXT_FLASH_ADDR (Resursi)

    uint32_t staging = ADDR_EXT_FLASH; // Default inicijalizacija

    // Provjera da li je korisnik poslao custom adresu
    bool hasStagingParam = request->hasParam("staging_addr") || request->hasParam("staging_addr", true);
    uint32_t customStaging = 0;
    if (hasStagingParam) {
        AsyncWebParameter* pStaging = request->hasParam("staging_addr", true) ? request->getParam("staging_addr", true) : request->getParam("staging_addr");
        customStaging = strtoul(pStaging->value().c_str(), NULL, 0);
    }

    if (slot >= 0 && slot <= 2) {
        staging = ADDR_FW_STAGING; // UVIJEK Firmware adresa, bez override-a
    } else if (slot == 3) {
        staging = hasStagingParam ? customStaging : ADDR_FW_STAGING; // Default FW, dozvoljen override
    } else if (slot >= 4 && slot <= 6) {
        staging = ADDR_EXT_FLASH;  // UVIJEK Ext Flash adresa, bez override-a
    } else if (slot == 7) {
        staging = hasStagingParam ? customStaging : ADDR_EXT_FLASH;  // Default Ext, dozvoljen override
    }
    
    if (updateService.startUpdate(slot, addr, staging)) {
        sendJsonSuccess(request, "Update Started");
    } else {
        sendJsonError(request, 500, "Failed to start update (Busy or Invalid Slot)");
    }
  });

  // 4. Update Status
  server->on("/update_status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["active"] = updateService.isActive();
    doc["state"] = updateService.getState();
    doc["progress"] = updateService.getProgress();
    doc["lastError"] = updateService.getLastError();
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // 4a. Reset Update Service
  server->on("/reset_update", HTTP_POST, [](AsyncWebServerRequest *request) {
    updateService.reset();
    sendJsonSuccess(request, "Update service reset to IDLE");
  });

  // 5. Hex Dump for Debugging
  server->on("/hexdump", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("slot") || !request->hasParam("offset")) {
        request->send(400, "text/plain", "Missing slot or offset");
        return;
    }
    int slot = request->getParam("slot")->value().toInt();
    int offset = request->getParam("offset")->value().toInt();
    int length = request->hasParam("len") ? request->getParam("len")->value().toInt() : 64;
    
    if (length > 1024) length = 1024; // Limit size

    uint8_t buf[1024];
    extFlash.readBufferFromSlot(slot, offset, buf, length);
    
    String output = "Hex Dump Slot " + String(slot) + " Offset " + String(offset) + ":\n";
    for(int i=0; i<length; i++) {
        if (i % 16 == 0) output += String(offset + i, HEX) + ": ";
        if (buf[i] < 0x10) output += "0";
        output += String(buf[i], HEX) + " ";
        if (i % 16 == 15) output += "\n";
    }
    request->send(200, "text/plain", output);
  });

  server->begin();
  LOG_INFO_LN("Update server on http://%s:%d/update", WiFi.localIP().toString().c_str(), _port);
  
  // Inicijalizuj GET_STATUS watchdog na kraju setup-a
  initGetStatusWatchdog();
}
/**
 * GLAVNA PETLJA
 */
void loop()
{
  // Provera GET_STATUS watchdog-a (PRVO!)
  if (getStatusWatchdogTriggered) {
    LOG_ERROR("[Watchdog] No GET_STATUS for %d seconds. Restarting...\n", CMD_GET_STATUS_TIMEOUT_SEC);
    delay(1000);
    ESP.restart();
  }
  
  updateService.loop();

  unsigned long buttonPressStart = 0;
  static uint32_t tick = millis();
  static unsigned long wifiCheckTimer = 0;

  esp_task_wdt_reset();

  checkLed(); // onboard LED signal aktivnosti

  if (tick != millis()) // TinyFrame tick
  {
    tick = millis();
    TF_Tick(&tfapp);
  }

  while (Serial2.available()) // Obrada dolaznih bajtova preko Serial2 (RS485)
  {
    // NE čitaj Serial2 ako HTTP handler trenutno čeka odgovor!
    if (!httpHandlerWaiting) {
      TF_AcceptChar(&tfapp, Serial2.read());
    } else {
      break;  // Pusti HTTP handler da čita
    }
  }

  if (digitalRead(BOOT_PIN) == LOW) // WiFi reset putem dugmeta (BOOT dugme)
  {
    digitalWrite(LED_PIN, HIGH);
    buttonPressStart = millis();
    while (digitalRead(BOOT_PIN) == LOW)
    {
      // čekaj dok je dugme i dalje pritisnuto
      if (millis() - buttonPressStart >= HOLD_TIME)
      {
        LOG_INFO_LN("BOOT taster hold for 5s, starting portal...");
        wm.resetSettings();    // obriši stare podatke
        WiFi.disconnect(true); // prekini vezu i zaboravi sve
        delay(1000);
        wm.startConfigPortal("WiFiManager");
        break;
      }
      delay(10); // mali delay da CPU ne troši bez veze
    }
  }

  if (millis() - wifiCheckTimer > 30000) // Povremeno provjeri WiFi konekciju (svakih 30 sekundi)
  {
    wifiCheckTimer = millis();
    if (WiFi.status() != WL_CONNECTED)
    {
      LOG_ERROR_LN("WiFi not connected, Restart...");
      delay(2000);
      ESP.restart();
    }
  }

  if (pingWatchdogEnabled && WiFi.isConnected()) // Povremeno provjeri ping google servera
  {
    if (millis() - lastPingTime >= PING_INTERVAL_MS)
    {
      lastPingTime = millis();

      if (pingGoogle())
      {
        pingFailures = 0;
        LOG_INFO_LN("[PingWdg] Google Response OK...");
      }
      else
      {
        pingFailures++;
        LOG_ERROR("[PingWdg] Ping failed (%d/%d)\n", pingFailures, MAX_PING_FAILURES);
        if (pingFailures >= MAX_PING_FAILURES)
        {
          LOG_ERROR_LN("[PingWdg] Max failures reached. Restarting...");
          ESP.restart();
        }
      }
    }
  }

  for (int i = 0; i < MAX_PULSE_PINS; i++) // reset pina setovanog sa puls komandom
  {
    if (pulseStates[i].active && (millis() - pulseStates[i].pulseStart >= pulseStates[i].duration))
    {
      digitalWrite(pulseStates[i].pin, LOW);
      pulseStates[i].active = false;
    }
  }

  if (tick - lastReadTime >= (tempSensorAvailable ? READ_INTERVAL : READ_INTERVAL * 10)) // provjera senzora temperature i update termostata
  {
    lastReadTime = tick;

    if (tempSensorAvailable)
    {
      sensors.requestTemperatures();
      float temp = sensors.getTempCByIndex(0);

      if (temp == DEVICE_DISCONNECTED_C)
      {
        tempSensorAvailable = false;
        turnOffThermoRelays();
        th_mode = TH_OFF; // sigurnosno
        LOG_ERROR_LN("Temperature sensor disconnected!");
      }
      else
      {
        // senzor validan → obradi podatak
        emaTemperature = (emaAlpha * temp) + ((1 - emaAlpha) * emaTemperature);
        setFansAndValve(emaTemperature);
        LOG_DEBUG_F("Room Temperature: %.2f C (EMA: %.2f)\n", temp, emaTemperature);
      }
    }
    else
    {
      // pokušaj ponovo otkriti senzor
      if (sensors.getDeviceCount() > 0)
      {
        LOG_INFO_LN("Room Temperature Sensor reconnected!");
        tempSensorAvailable = true;

        // inicijalna EMA (da ne skoči naglo)
        sensors.setResolution(12);
        sensors.requestTemperatures();
        float temp = sensors.getTempCByIndex(0);
        if (temp != DEVICE_DISCONNECTED_C)
        {
          emaTemperature = temp;
          loadThermoPreferences(); // vrati postavke i režim
          th_mode = th_saved_mode;
          LOG_INFO("Room Temperature: %.2f C | Settings restored | Mode: %d\n", temp, (int)th_mode);
        }
        else
        {
          LOG_ERROR_LN("Sensor reconnected, but not responding!");
        }
      }
      else
      {
        // i dalje nema senzora
        turnOffThermoRelays();
        th_mode = TH_OFF; // ostani u OFF
      }
    }

    if (tempSensor2Available)
    {
      sensors2.requestTemperatures();
      fluid = sensors2.getTempCByIndex(0);
      LOG_DEBUG_F("Fluid Temperature: %.2f*C\n", fluid);
    }
  }
}
