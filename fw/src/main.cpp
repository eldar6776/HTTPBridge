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
#include "esp_task_wdt.h"
#include <OneWire.h>
#include <DallasTemperature.h>
extern "C"
{
#include "TinyFrame.h"
}

// PIN definicije
#define BOOT_PIN 0
#define LED_PIN 2
#define RS485_DE_PIN 4
#define ONE_WIRE_PIN 5
#define ONE_WIRE2_PIN 18
#define FAN_L 25
#define FAN_M 26
#define FAN_H 27
#define LIGHT_PIN 32 
#define VALVE 33
#define SET_RTC_DATE_TIME 213
#define DEF_TFBRA 255 // default broadcast address
#define S_CUSTOM 23
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
int rdy, replyDataLength;
bool pingWatchdogEnabled = false;
unsigned long lastPingTime = 0;
int pingFailures = 0;
unsigned long lastReadTime = 0;
float fluid = 0.0;          // Fluid temperature
float emaTemperature = 0.0; // EMA filter
float emaAlpha = 0.2;       // podesiv u prefs
float th_setpoint = 25.0;   // Termostat varijable
float th_treshold = 0.5;    // osnovni prag
int currentFanLevel = 0;    // Globalna varijabla, čuva trenutno aktivnu brzinu: 0 = off, 1 = L, 2 = M, 3 = H
uint8_t replyData[64] = {0};
std::unique_ptr<AsyncWebServer> server;
// Lista pinova koje već koristiš ili koji su hardverski rizični
const int usedPins[] = { // NOLINT(cert-err58-cpp)
    BOOT_PIN,     // npr. GPIO0
    1,            // UART0 TX (Serial)
    LED_PIN,      // npr. GPIO2
    3,            // UART0 RX (Serial)
    RS485_DE_PIN, // npr. GPIO4
    ONE_WIRE_PIN,
    6, 7, 8, 9, 10, 11, // povezani sa SPI flash – ne koristiti
    16,                 // Serial2 RX2
    17,                 // Serial2 TX2
    ONE_WIRE2_PIN,
    20, // ne postoji na većini ESP32 modula
    LIGHT_PIN,
    24, // ne postoji
    FAN_L, FAN_M, FAN_H,
    28, // ne postoji
    29, // ne postoji
    30, // ne postoji
    31, // ne postoji
    VALVE,
    37, 38, 39 // samo za ulaz, nema izlaznu funkciju
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
 */
enum CommandType
{
  CMD_UNKNOWN,
  CMD_ESP_GET_PINS = 0xA0,
  CMD_ESP_SET_PIN = 0xA1,
  CMD_ESP_RESET_PIN = 0xA2,
  CMD_ESP_PULSE_PIN = 0xA3,
  CMD_GET_STATUS = 0xAA,
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
  CMD_SET_THST_HEATING = 0xDC,
  CMD_SET_THST_COOLING = 0xDD,
  CMD_SET_THST_OFF = 0xDE,
  CMD_SET_PASSWORD = 0xDF,
  CMD_GET_SSID_PSWRD = 0xE0,
  CMD_SET_SSID_PSWRD = 0xE1,
  CMD_GET_MDNS_NAME = 0xE2,
  CMD_SET_MDNS_NAME = 0xE3,
  CMD_GET_TCPIP_PORT = 0xE4,
  CMD_SET_TCPIP_PORT = 0xE5,
  CMD_GET_IP_ADDRESS = 0xE6,
  CMD_RESTART = 0xE7,
  CMD_GET_TIMER = 0xE8,
  CMD_SET_TIMER = 0xE9,
  CMD_GET_TIME = 0xEA,
  CMD_SET_TIME = 0xEB,
  CMD_OUTDOOR_LIGHT_ON = 0xEC,
  CMD_OUTDOOR_LIGHT_OFF = 0xED,
  CMD_GET_PINGWDG = 0xEE,
  CMD_PINGWDG_ON = 0xEF,
  CMD_PINGWDG_OFF = 0xF0,
  CMD_TH_SETPOINT = 0xF1,
  CMD_TH_DIFF = 0xF2,
  CMD_TH_STATUS = 0xF3,
  CMD_TH_HEATING = 0xF4,
  CMD_TH_COOLING = 0xF5,
  CMD_TH_OFF = 0xF6,
  CMD_TH_ON = 0xF7,
  CMD_TH_EMA = 0xF8

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
  return CMD_UNKNOWN;
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

  Serial.println("No available pulse slots!");
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

  delay(100); // mala pauza radi sigurnosti pre nego se uključi novi relej

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
  replyDataLength = msg->len;
  memcpy(replyData, msg->data, msg->len);
  rdy = -1;
  return TF_CLOSE;
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
  Serial.println("----- updateLightState -----");
  Serial.printf("UTC: %02d:%02d\n", utc_tm.tm_hour, utc_tm.tm_min);
  Serial.printf("DST active: %s\n", dstActive ? "YES" : "NO");
  Serial.printf("Timezone offset: %d min\n", tzOffsetMinutes);

  // Postavi sunčev datum tačno
  sun.setPosition(LATITUDE, LONGITUDE, 0);
  sun.setCurrentDate(utc_tm.tm_year + 1900, utc_tm.tm_mon + 1, utc_tm.tm_mday);

  float sunriseUTC = sun.calcSunrise();
  float sunsetUTC = sun.calcSunset();

  int sunriseMin = ((int)sunriseUTC + tzOffsetMinutes + 1440) % 1440;
  int sunsetMin = ((int)sunsetUTC + tzOffsetMinutes + 1440) % 1440;

  Serial.printf("Sunrise: %.2f -> %02d:%02d\n", sunriseUTC, sunriseMin / 60, sunriseMin % 60);
  Serial.printf("Sunset: %.2f -> %02d:%02d\n", sunsetUTC, sunsetMin / 60, sunsetMin % 60);

  int onMin = -1, offMin = -1;

  if (timerOnType == "SUNRISE")
    onMin = sunriseMin;
  else if (timerOnType == "SUNSET")
    onMin = sunsetMin;
  else if (timerOnTime.length() == 4)
    onMin = timerOnTime.substring(0, 2).toInt() * 60 + timerOnTime.substring(2, 4).toInt();

  if (timerOffType == "SUNRISE")
    offMin = sunriseMin;
  else if (timerOffType == "SUNSET")
    offMin = sunsetMin;
  else if (timerOffTime.length() == 4)
    offMin = timerOffTime.substring(0, 2).toInt() * 60 + timerOffTime.substring(2, 4).toInt();

  int localMinutes = (utc_tm.tm_hour * 60 + utc_tm.tm_min + tzOffsetMinutes) % 1440;

  Serial.printf("Current time (local min): %d (%02d:%02d)\n", localMinutes, localMinutes / 60, localMinutes % 60);
  Serial.printf("ON=%d (%02d:%02d), OFF=%d (%02d:%02d)\n",
                onMin, onMin / 60, onMin % 60, offMin, offMin / 60, offMin % 60);

  bool lightShouldBeOn = false;

  if (onMin >= 0 && offMin >= 0)
  {
    if (onMin < offMin)
      lightShouldBeOn = (localMinutes >= onMin && localMinutes < offMin);
    else
      lightShouldBeOn = (localMinutes >= onMin || localMinutes < offMin);
  }

  // OVDJE DODAJEMO OVERRIDE MEHANIZAM
  if (overrideActive)
  {
    // Ako se trenutni izlaz po timeru razlikuje od onoga što je korisnik ručno postavio
    if (lightShouldBeOn != overrideState)
    {
      Serial.println("Timer event take control, override reset.");
      clearOutdoorLightOverride(); // resetuj override
    }
    else
    {
      // Još uvek smo u skladu sa ručnim stanjem, ne diramo izlaz
      Serial.println("Override activ, skip change.");
      return;
    }
  }

  // Ako nismo u override režimu, upravljaj izlazom
  digitalWrite(LIGHT_PIN, lightShouldBeOn ? HIGH : LOW);
  lightState = lightShouldBeOn;

  Serial.printf("Light should be: %s\n", lightShouldBeOn ? "ON" : "OFF");
  Serial.println("----------------------------");
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

  if (!request->hasParam("CMD"))
  {
    request->send(400, "text/plain", "Missing CMD");
    return;
  }

  String cmdStr = request->getParam("CMD")->value();
  CommandType cmd = stringToCommand(cmdStr);

  uint8_t buf[64] = {0};
  int length = 0;
  led_state = LED_FAST;

  switch (cmd)
  {

  case CMD_RESTART:
  {
    Serial.println("Device restart...");
    request->send(200, "text/plain", "Restart in 3s");
    delay(3000);
    ESP.restart();
    break;
  }
  case CMD_RESTART_CTRL:
  {
    if (!request->hasParam("ID"))
    {
      request->send(400, "text/plain", "Missing ID");
      return;
    }

    int id = request->getParam("ID")->value().toInt();

    // ✅ Validacija ID-a
    if (id < 1 || id > 254)
    {
      request->send(400, "text/plain", "Invalid ID (must be 1-254)");
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

    if (strlen(_pass) == 0)
    {
      snprintf(_resp, sizeof(_resp), "SSID = %s\nPASSWORD = Not Set (OPEN NETWORK)", _ssid);
    }
    else
    {
      snprintf(_resp, sizeof(_resp), "SSID = %s\nPASSWORD = %s", _ssid, _pass);
    }
    request->send(200, "text/plain", _resp);
    return;
  }
  case CMD_SET_SSID_PSWRD:
  {
    if (!request->hasParam("SSID"))
    {
      request->send(400, "text/plain", "Missing SSID");
      return;
    }
    strlcpy(_ssid, request->getParam("SSID")->value().c_str(), sizeof(_ssid));
    strlcpy(_pass, request->hasParam("PSWRD") ? request->getParam("PSWRD")->value().c_str() : "", sizeof(_pass));

    preferences.begin("wifi", false);
    preferences.putString("ssid", _ssid);
    preferences.putString("password", _pass);
    preferences.end();
    request->send(200, "text/plain", "WIFI SSID & PASSWORD Setup OK");
    return;
  }
  case CMD_GET_MDNS_NAME:
  {

    preferences.begin("_mdns", false);
    preferences.getString("mdns", _mdns, sizeof(_mdns));
    preferences.end();
    snprintf(_resp, sizeof(_resp), "mDNS = %s", _mdns);
    request->send(200, "text/plain", _resp);
    return;
  }
  case CMD_SET_MDNS_NAME:
  {
    if (!request->hasParam("MDNS"))
    {
      request->send(400, "text/plain", "Missing MDNS");
      return;
    }
    strlcpy(_mdns, request->getParam("MDNS")->value().c_str(), sizeof(_mdns));
    preferences.begin("_mdns", false);
    preferences.putString("mdns", _mdns);
    preferences.end();
    request->send(200, "text/plain", "mDNS Setup OK");
    return;
  }
  case CMD_GET_IP_ADDRESS:
  {
    snprintf(_resp, sizeof(_resp), "TCP/IP Address = %s", WiFi.localIP().toString().c_str());
    request->send(200, "text/plain", _resp);
    return;
  }
  case CMD_GET_TCPIP_PORT:
  {
    preferences.begin("_port", false);
    _port = preferences.getInt("port", 0);
    preferences.end();
    snprintf(_resp, sizeof(_resp), "TCP/IP Port = %d", _port);
    request->send(200, "text/plain", _resp);
    return;
  }
  case CMD_SET_TCPIP_PORT:
  {
    if (!request->hasParam("PORT"))
    {
      request->send(400, "text/plain", "Missing PORT");
      return;
    }
    _port = request->getParam("PORT")->value().toInt();
    preferences.begin("_port", false);
    preferences.putInt("port", _port);
    preferences.end();
    request->send(200, "text/plain", "TCP/IP Port Setup OK");
    return;
  }
  case CMD_SET_PASSWORD:
  {
    if (!request->hasParam("ID") || !request->hasParam("PASSWORD"))
    {
      request->send(400, "text/plain", "Missing ID or PASSWORD");
      return;
    }
    buf[0] = CMD_SET_PASSWORD;
    {
      int id = request->getParam("ID")->value().toInt();
      buf[1] = id >> 8;
      buf[2] = id & 0xFF;

      const char *pw = request->getParam("PASSWORD")->value().c_str();
      strlcpy((char *)buf + 3, pw, sizeof(buf) - 3);
      length = 3 + strlen(pw) + 1;
    }
    break;
  }
  case CMD_SET_ROOM_TEMP:
  case CMD_SET_GUEST_IN_TEMP:
  case CMD_SET_GUEST_OUT_TEMP:
  {
    if (!request->hasParam("ID") || !request->hasParam("VALUE"))
    {
      request->send(400, "text/plain", "Missing ID or VALUE");
      return;
    }

    int id = request->getParam("ID")->value().toInt();
    int value = request->getParam("VALUE")->value().toInt();

    // ✅ Validacija ID-a
    if (id < 1 || id > 254)
    {
      request->send(400, "text/plain", "Invalid ID (must be 1-254)");
      return;
    }

    // ✅ Validacija VALUE-a
    if (value < 5 || value > 40)
    {
      request->send(400, "text/plain", "Invalid VALUE (must be 5-40)");
      return;
    }

    buf[0] = cmd;
    buf[1] = id;
    buf[2] = value;
    length = 3;
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
      request->send(400, "text/plain", "Missing ID");
      return;
    }
    buf[0] = cmd;
    buf[1] = request->getParam("ID")->value().toInt();
    length = 2;
    break;
  }
  case CMD_SET_PIN:
  {
    if (!request->hasParam("ID") || !request->hasParam("PIN") || !request->hasParam("VALUE"))
    {
      request->send(400, "text/plain", "Missing ID or PIN or VALUE");
      return;
    }

    int id = request->getParam("ID")->value().toInt();
    int pin = request->getParam("PIN")->value().toInt();
    int value = request->getParam("VALUE")->value().toInt();

    // ✅ Validacija ID-a
    if (id < 1 || id > 254)
    {
      request->send(400, "text/plain", "Invalid ID (must be 1-254)");
      return;
    }

    // ✅ Validacija PIN-a
    if (pin < 1 || pin > 6)
    {
      request->send(400, "text/plain", "Invalid PIN (must be 1-6)");
      return;
    }

    // ✅ Validacija VALUE-a
    if (value != 0 && value != 1)
    {
      request->send(400, "text/plain", "Invalid VALUE (must be 0 or 1)");
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
  case CMD_GET_PINS:
  {
    if (!request->hasParam("ID"))
    {
      request->send(400, "text/plain", "Missing ID");
      return;
    }
    int id = request->getParam("ID")->value().toInt();
    // ✅ Validacija ID-a
    if (id < 1 || id > 254)
    {
      request->send(400, "text/plain", "Invalid ID (must be 1-254)");
      return;
    }
    buf[0] = 0xB1;
    buf[1] = id;
    buf[2] = 0xB2;
    length = 3;
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

    Serial.printf("UTC Time    : %02d:%02d\n", utc_tm.tm_hour, utc_tm.tm_min);
    Serial.printf("DST active  : %d\n", dstActive);
    Serial.printf("Offset (min): %d\n", tzOffsetMinutes);

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

      Serial.printf("SUNRISE local: %s\n", sunriseStr);
      Serial.printf("SUNSET  local: %s\n", sunsetStr);
    }

    bool relayState = digitalRead(LIGHT_PIN);
    String lightStateStr = String("Light Relay State : ") + (relayState ? "ON" : "OFF");

    // Pošalji odgovor sa DST flagom
    request->send(200, "text/plain",
                  "TIMERON=" + timerOnType + "\n" +
                      "TIMEROFF=" + timerOffType + "\n" +
                      "ONTIME=" + timerOnTime + "\n" +
                      "OFFTIME=" + timerOffTime + "\n" +
                      "SUNRISE=" + String(sunriseStr) + "\n" +
                      "SUNSET=" + String(sunsetStr) + "\n" +
                      lightStateStr + "\n" +
                      "DST=" + String(dstActive ? "1" : "0") + "\n");

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
      request->send(400, "text/plain", "Invalid TIMERON value.");
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
      request->send(400, "text/plain", "Invalid TIMEROFF value.");
      return;
    }

    saveTimerPreferences(onType, offType, onTime, offTime);
    loadTimerPreferences();
    request->send(200, "text/plain", "Timer Set OK");
    break;
  }
  case CMD_GET_TIME:
  {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
      request->send(500, "text/plain", "RTC Time invalid.");
      return;
    }

    char datum[11];  // YYYY-MM-DD
    char vrijeme[9]; // HH:MM:SS
    char dan[12];
    strftime(datum, sizeof(datum), "%Y-%m-%d", &timeinfo);
    strftime(vrijeme, sizeof(vrijeme), "%H:%M:%S", &timeinfo);
    strftime(dan, sizeof(dan), "%A", &timeinfo); // Puni naziv dana

    time_t epoch = mktime(&timeinfo);

    String response = "DATE=" + String(datum) + "\n" +
                      "TIME=" + String(vrijeme) + "\n" +
                      "DAY=" + String(dan) + "\n" +
                      "EPOCH=" + String(epoch) + "\n";

    request->send(200, "text/plain", response);
    break;
  }
  case CMD_SET_TIME:
  {
    String date = request->hasParam("DATE") ? request->getParam("DATE")->value() : "";
    String timeStr = request->hasParam("TIME") ? request->getParam("TIME")->value() : "";

    if (date.length() != 6 || timeStr.length() != 6)
    {
      request->send(400, "text/plain", "Invalid DATE or TIME format.");
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
      request->send(500, "text/plain", "Failed to convert time.");
      break;
    }
    else
    {
      timeValid = true;
    }

    struct timeval now = {.tv_sec = t};
    settimeofday(&now, nullptr);

    request->send(200, "text/plain", "RTC Date & Time Set OK.");
    break;
  }
  case CMD_OUTDOOR_LIGHT_OFF:
  case CMD_OUTDOOR_LIGHT_ON:
  {
    int new_state = (cmd == CMD_OUTDOOR_LIGHT_ON) ? HIGH : LOW;
    setOutdoorLightOverride(new_state);

    String response = "Outdoor Lights Set: ";
    response += (new_state == HIGH) ? "ON" : "OFF";

    request->send(200, "text/plain", response);
    return;
  }
  case CMD_GET_PINGWDG:
  {
    request->send(200, "text/plain", String("Ping Watchdog State : ") + (pingWatchdogEnabled ? "ON" : "OFF"));
    break;
  }
  case CMD_PINGWDG_ON:
  {
    pingWatchdogEnabled = true;
    preferences.begin("_pingwdg", false); // false = read/write
    preferences.putBool("pingwdg", true);
    preferences.end();
    request->send(200, "text/plain", String("Ping Watchdog State : ") + (pingWatchdogEnabled ? "ON" : "OFF"));
    break;
  }
  case CMD_PINGWDG_OFF:
  {
    pingWatchdogEnabled = false;
    preferences.begin("_pingwdg", false); // false = read/write
    preferences.putBool("pingwdg", false);
    preferences.end();
    request->send(200, "text/plain", String("Ping Watchdog State : ") + (pingWatchdogEnabled ? "ON" : "OFF"));
    break;
  }
  case CMD_GET_STATUS:
  {
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

    // Sastavi sve u jedan tekst
    String result;
    result += "WIFI SSID=" + ssidStr + "\n";
    result += "WIFI PASSWORD=" + String(passStr) + "\n";
    result += "mDNS=" + String(_mdns) + "\n";
    result += "TCP/IP Port=" + String(_port) + "\n";
    result += "TCP/IP Address=" + ipStr + "\n";
    result += "Current Time (UTC)=: " + String(utcTimeStr) + "\n";
    result += "Timezone Offset=" + String(tzOffsetMinutes) + " min\n";
    result += "Daylight Saving Time=" + String(dstActive ? "YES" : "NO") + "\n";
    result += "Light Relay State=" + String(relayState ? "ON" : "OFF") + "\n";
    result += "TIMERON=" + timerOnType + "\n";
    result += "TIMEROFF=" + timerOffType + "\n";
    result += "ONTIME=" + String(onStr) + "\n";
    result += "OFFTIME=" + String(offStr) + "\n";
    result += "SUNRISE=" + String(sunriseStr) + "\n";
    result += "SUNSET=" + String(sunsetStr) + "\n";
    result += "Ping Watchdog=" + String(pingWatchdogEnabled ? "ENABLED" : "DISABLED") + "\n";
    result += "ESP Pin States:\n" + getAvailablePinsStatus();
    result += "ESP Thermostat State:\n";
    result += "Temp=" + String(emaTemperature, 1) + "*C\n";
    result += "Setpoint=" + String(th_setpoint, 1) + "*C\n";
    result += "Diff=" + String(th_treshold, 1) + "*C\n";
    result += "Mode=" + String(th_mode == TH_HEATING ? "HEATING" : th_mode == TH_COOLING ? "COOLING"
                                                                                         : "OFF") +
              "\n";
    result += "Pump/Valve=" + String(digitalRead(VALVE) ? "ON" : "OFF") + "\n";
    result += "Fan=";
    if (digitalRead(FAN_H))
      result += "HIGH";
    else if (digitalRead(FAN_M))
      result += "MEDIUM";
    else if (digitalRead(FAN_L))
      result += "LOW";
    else
      result += "OFF";
    result += "\nEMA Filter=" + String(emaAlpha, 1);
    result += "\nFluid=" + (tempSensor2Available ? String(fluid, 1) + "*C\n" : "NOT AVAILABLE");

    request->send(200, "text/plain", result);
    break;
  }
  case CMD_ESP_GET_PINS:
  {
    String pinsStatus = getAvailablePinsStatus();
    strncpy(_resp, pinsStatus.c_str(), sizeof(_resp) - 1);
    _resp[sizeof(_resp) - 1] = '\0'; // Ensure null termination
    request->send(200, "text/plain", _resp);
    break;
  }
  case CMD_ESP_SET_PIN:
  {
    if (!request->hasParam("PIN"))
    {
      request->send(400, "text/plain", "Missing PIN");
      return;
    }
    int _pin = request->getParam("PIN")->value().toInt();

    if (!isPinAvailable(_pin) || isInputOnlyPin(_pin))
    {
      request->send(400, "text/plain", "PIN is invalid, already in use or input-only");
      return;
    }

    setPinHigh(_pin);
    request->send(200, "text/plain", "PIN set HIGH");
    break;
  }
  case CMD_ESP_RESET_PIN:
  {
    if (!request->hasParam("PIN"))
    {
      request->send(400, "text/plain", "Missing PIN");
      return;
    }
    int pin = request->getParam("PIN")->value().toInt();

    if (!isPinAvailable(pin) || isInputOnlyPin(pin))
    {
      request->send(400, "text/plain", "PIN is invalid, already in use or input-only");
      return;
    }

    setPinLow(pin);
    request->send(200, "text/plain", "PIN set LOW");
    break;
  }
  case CMD_ESP_PULSE_PIN:
  {
    if (!request->hasParam("PIN"))
    {
      request->send(400, "text/plain", "Missing PIN");
      return;
    }

    int pin = request->getParam("PIN")->value().toInt();

    if (!isPinAvailable(pin) || isInputOnlyPin(pin))
    {
      request->send(400, "text/plain", "PIN is invalid, already in use or input-only");
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
    request->send(200, "text/plain", limit ? "PIN Pulsed" : "PIN Pulsed Default 2 sec");
    break;
  }
  case CMD_TH_SETPOINT:
  {
    if (!request->hasParam("VALUE"))
    {
      request->send(400, "text/plain", "Missing VALUE for TH_SETPOINT");
      return;
    }
    int value = request->arg("VALUE").toInt();
    if (value < 5 || value > 40)
    {
      request->send(400, "text/plain", "Setpoint must be between 5 and 40");
      return;
    }
    th_setpoint = (float)value / 1.0f;
    preferences.begin("thermo", false);
    preferences.putFloat("setpoint", th_setpoint);
    preferences.end();
    request->send(200, "text/plain", "Thermostat Setpoint set to " + String(th_setpoint));
    return;
  }
  case CMD_TH_DIFF:
  {
    if (!request->hasArg("VALUE"))
    {
      request->send(400, "text/plain", "Missing VALUE for TH_DIFF");
      return;
    }
    int value = request->arg("VALUE").toInt();
    if (value < 1 || value > 50)
    {
      request->send(400, "text/plain", "Threshold must be between 1 and 50 (0.1*C - 5.0*C)");
      return;
    }
    th_treshold = (float)value / 10.0f;
    preferences.begin("thermo", false);
    preferences.putFloat("treshold", th_treshold);
    preferences.end();
    request->send(200, "text/plain", "Thermostat Difference set to " + String(th_treshold, 1) + "*C");
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
    status += "\nEMA=" + String(emaAlpha, 1);
    status += "\nFluid=" + (tempSensor2Available ? String(fluid, 1) + "*C" : "NOT AVAILABLE") + "\n";
    request->send(200, "text/plain", status);
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
    request->send(200, "text/plain", "Mode set to HEATING");
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
    request->send(200, "text/plain", "Mode set to COOLING");
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
    request->send(200, "text/plain", "Thermostat turned OFF");
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
    request->send(200, "text/plain", "Thermostat resumed, mode: " + String(th_mode == TH_HEATING ? "HEATING" : "COOLING"));
    return;
  }
  case CMD_TH_EMA:
  {
    if (!request->hasArg("VALUE"))
    {
      request->send(400, "text/plain", "Missing VALUE for TH_EMA");
      return;
    }

    String valStr = request->arg("VALUE");
    if (!valStr.equals(String(valStr.toInt()))) // jednostavna provjera da je cijeli broj
    {
      request->send(400, "text/plain", "VALUE must be an integer");
      return;
    }

    int value = valStr.toInt();
    if (value < 1 || value > 10)
    {
      request->send(400, "text/plain", "EMA must be between 1 and 10 (which corresponds to 0.1 - 1.0)");
      return;
    }

    emaAlpha = (float)value / 10.0f;
    preferences.begin("thermo", false);
    preferences.putFloat("emaAlpha", emaAlpha);
    preferences.end();
    request->send(200, "text/plain", "EMA filter set to " + String(emaAlpha, 1));
    return;
  }
  default:
    request->send(400, "text/plain", "Unknown command");
    return;
  }

  if (buf[0] && length)
  {
    Serial.print("Sending command: ");
    for (int i = 0; i < length; i++)
    {
      if (buf[i] < 0x10)
        Serial.print('0');
      Serial.print(buf[i], HEX);
      Serial.print(' ');
    }
    Serial.println();
    // TF_SendSimple(&tfapp, S_CUSTOM, buf, length);
    TF_QuerySimple(&tfapp, S_CUSTOM, buf, length, ID_Listener, TF_PARSER_TIMEOUT_TICKS * 4);
    rdy = TF_PARSER_TIMEOUT_TICKS * 4;
    do
    {
      --rdy;
      delay(1);
    } while (rdy > 0);

    if (rdy == 0)
    {
      request->send(200, "text/plain", "TIMEOUT");
    }
    else
    {
      String body = "";

      switch (replyData[0])
      {

      case 0xB1:
        if ((replyDataLength == 3) && (replyData[1] == 0xB2))
        {
          for (int i = 7; i >= 0; i--)
            bin += String(bitRead(replyData[2], i));
          body += "Pins States = " + bin;
        }
        else
        {
          body += "Pin Set OK ";
        }
        break;

      case CMD_GET_ROOM_TEMP:
        body += "Room Temperature = " + String(replyData[1]) + "*C";
        body += "\nSetpoint Temperature = " + String(replyData[2]) + "*C";
        break;

      case CMD_GET_FAN_DIFFERENCE:
        body += "Fan Difference = " + String(replyData[1]) + "*C";
        break;

      case CMD_GET_FAN_BAND:
        body += "Fan Low Band = " + String(replyData[1]) + "*C";
        body += "\nFan High Band = " + String(replyData[2]) + "*C";
        break;

      case CMD_GET_GUEST_IN_TEMP:
        body += "Guest In Temperature = " + String(replyData[1]) + "*C";
        break;

      case CMD_GET_GUEST_OUT_TEMP:
        body += "Guest Out Temperature = " + String(replyData[1]) + "*C";
        break;

      case CMD_SET_PASSWORD:
        body += "Password set OK";
        break;

      case CMD_SET_ROOM_TEMP:
      case CMD_SET_GUEST_IN_TEMP:
      case CMD_SET_GUEST_OUT_TEMP:
        body += "Temperature set OK";
        break;

      case CMD_SET_THST_ON:
      case CMD_SET_THST_OFF:
      case CMD_SET_THST_HEATING:
      case CMD_SET_THST_COOLING:
        body += "Thermostat set OK";
        break;

      case CMD_RESTART_CTRL:
        body += "Controler Restart OK";
        break;

      default:
      {
        body += "Unknown response, cmd = 0x";
        if (replyData[0] < 0x10)
          body += "0";
        body += String(replyData[0], HEX);

        body += "\nFull replyData[] (" + String(replyDataLength) + " bytes): ";
        for (size_t i = 0; i < replyDataLength; i++)
        {
          if (replyData[i] < 0x10)
            body += "0"; // leading zero
          body += String(replyData[i], HEX);
          body += " "; // razmak između bajtova
        }

        break;
      }
      }

      request->send(200, "text/plain", body);
    }
  }
  else
  {
    request->send(200, "text/plain", "ERROR");
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
    Serial.println("[RTC SEND] Greška: timeInfo je nullptr");
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

  Serial.print("RTC Buf: ");
  for (int i = 0; i < 9; i++)
  {
    Serial.printf("%02X ", buf[i]);
  }
  Serial.println();

  bool sent = TF_SendSimple(&tfapp, S_CUSTOM, buf, sizeof(buf));
  if (!sent)
  {
    Serial.println("RTC Update ERROR !");
  }
  else
  {
    Serial.println("RTC Update OK");
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
      Serial.print("🔌 Connecting on WiFi (with password): ");
      Serial.println(_ssid);
    }
    else
    {
      WiFi.begin(_ssid); // pokušaj spojiti bez lozinke
      Serial.print("🔌 Connecting on WiFi (no password): "); // NOLINT(performance-avoid-endl)
      Serial.println(_ssid);
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
    {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      connected = true;
      Serial.println("✅ Connected on previous network !");
      Serial.println(WiFi.localIP());
    }
    else
    {
      Serial.println("❌ Not connected on previous network.");
    }
  }

  if (!connected)
  {
    Serial.println("📶 Starting WiFiManager portal...");

    connected = wm.autoConnect("WiFiManager");

    if (connected)
    {
      Serial.println("✅ Connected with portal!");
      Serial.println(WiFi.localIP());

      preferences.begin("wifi", false);
      preferences.putString("ssid", WiFi.SSID());
      preferences.putString("password", WiFi.psk());
      preferences.end();
    }
    else
    {
      Serial.println("❌ Not connected. Restart...");
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
    Serial.println("Room Temperature sensor NOT found!");
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
    Serial.println("\nFluid Temperature sensor NOT found!");
  }

  server = std::unique_ptr<AsyncWebServer>(new AsyncWebServer(_port)); // Dinamička alokacija servera s portom iz Preferences

  TF_InitStatic(&tfapp, TF_MASTER);
  rtcSyncTicker.attach(60.0, sendRtcToBus);

  tryConnectWiFi(); // 👈 Ovde se sada samo poziva čista funkcija

  if (!MDNS.begin(_mdns))
    Serial.println("⚠️ mDNS not started!");
  else
  {
    MDNS.addService("http", "tcp", _port);
    Serial.println(String("🌐 mDNS responder started. Available on http://") + _mdns + ".local:" + _port);
  }

  configTzTime(TIMEZONE, "pool.ntp.org");
  // Čekaj da se vreme sinhronizuje
  unsigned long start = millis();
  while (time(nullptr) < 100000 && millis() - start < 10000)
  {
    Serial.print(".");
    delay(500);
  }
  if (time(nullptr) < 100000)
  {
    timeValid = false;
    Serial.println("Time sync failed!");
  }
  else
  {
    timeValid = true;
    Serial.println("Time synchronized!");
  }

  loadTimerPreferences();
  setupLightControl();
  updateLightState();

  server->on("/sysctrl.cgi", HTTP_GET, handleSysctrlRequest);  // Handler za sysctrl.cgi
  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) // Osnovni endpoint root
             { request->send(200, "text/plain", "Online"); });
  server->on("/update", HTTP_GET, [](AsyncWebServerRequest *request) // Prikazivanje forme za update
             { request->send(200, "text/html", update_form_html); });
  server->on( // Obrada OTA upload POST zahteva
      "/update", HTTP_POST, [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, String filename, size_t index,
         uint8_t *data, size_t len, bool final)
      {
        if (!index)
        {
          Serial.printf("Update begin: %s\n", filename.c_str());
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
            Serial.printf("Update finished, size: %u\n", index + len);
          }
          else
          {
            Update.printError(Serial);
          }
        }
      });
  server->begin();
  Serial.println("Update server on http://" + WiFi.localIP().toString() + ":" + _port + "/update");
}
/**
 * GLAVNA PETLJA
 */
void loop()
{
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
    TF_AcceptChar(&tfapp, Serial2.read());
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
        Serial.println("BOOT taster hold for 5s, starting portal...");
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
      Serial.println("WiFi not connected, Restart...");
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
        Serial.println("[PingWdg] Google Response OK...");
      }
      else
      {
        pingFailures++;
        Serial.printf("[PingWdg] Ping failed (%d/%d)\n", pingFailures, MAX_PING_FAILURES);
        if (pingFailures >= MAX_PING_FAILURES)
        {
          Serial.println("[PingWdg] Max failures reached. Restarting...");
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
        Serial.println("Temperature sensor disconnected!");
      }
      else
      {
        // senzor validan → obradi podatak
        emaTemperature = (emaAlpha * temp) + ((1 - emaAlpha) * emaTemperature);
        setFansAndValve(emaTemperature);
        Serial.printf("Room Temperature: %.2f C (EMA: %.2f)\n", temp, emaTemperature);
      }
    }
    else
    {
      // pokušaj ponovo otkriti senzor
      if (sensors.getDeviceCount() > 0)
      {
        Serial.println("Room Temperature Sensor reconnected!");
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
          Serial.printf("Room Temperature: %.2f C | Settings restored | Mode: %d\n", temp, (int)th_mode);
        }
        else
        {
          Serial.println("Sensor reconnected, but not responding!");
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
      Serial.print("Fluid Temperature: " + String(fluid) + "*C\n");
    }
  }
}
