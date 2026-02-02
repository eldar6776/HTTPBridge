# HTTP Bridge JSON API Dokumentacija

## 📋 Pregled

HTTP Bridge ESP32 uređaj sada koristi **unified JSON format** za sve API odgovore (100% konverzija). Ovo omogućava:

- ✅ Lakše parsiranje u Python/JavaScript aplikacijama
- ✅ Tipizaciju podataka (boolean, number, string)
- ✅ Strukturirane nested objekte
- ✅ Uniformni error handling
- ✅ Lakšu ekstenziju bez breaking changes

### 🆕 Nedavno Dodane Komande

**ESP32 Lokalne:**
- `GET_TIMER` / `SET_TIMER` - Timer kontrola vanjske rasvjete
- `GET_TIME` / `SET_TIME` - Vrijeme i datum
- `ESP_GET_PINS` - Status dostupnih pinova
- `TH_STATUS` - Detaljni status termostata

**RS485 IC Kontroler:**
- `RESTART_CTRL` - Restart kontrolera
- `GET_ROOM_STATUS` - Status kartice u sobi
- `SET_LANG` - Postavljanje jezika (SRB/ENG/GER)
- `QR_CODE_SET` - WiFi QR kod
- `SET_SYSID` / `GET_SYSID` - System ID

---

## 🔗 Base URL

```
http://<IP_ADRESA>/sysctrl.cgi?CMD=<KOMANDA>&<PARAMETRI>
```

**Primjer:**
```
http://192.168.1.100/sysctrl.cgi?CMD=GET_STATUS
```

---

## 📦 Struktura Odgovora

### ✅ Success Response

```json
{
  "status": "success",
  "message": "Opis akcije",
  "data": {
    // Opcionalni podaci specifični za komandu
  }
}
```

**HTTP Status Code:** `200 OK`

### ❌ Error Response

```json
{
  "status": "error",
  "code": 400,
  "message": "Opis greške"
}
```

**HTTP Status Codes:**
- `400` - Bad Request (neispravni parametri)
- `408` - Timeout (nema odgovora sa RS485 uređaja)
- `500` - Internal Server Error

---

## 📡 Komande

### 🌐 **Network & WiFi**

#### `GET_SSID_PSWRD`
Dohvata WiFi credentials.

**Request:**
```
GET /sysctrl.cgi?CMD=GET_SSID_PSWRD
```

**Response:**
```json
{
  "status": "success",
  "message": "WiFi credentials retrieved",
  "data": {
    "ssid": "MyNetwork",
    "password": "password123",
    "open_network": false
  }
}
```

---

#### `SET_SSID_PSWRD`
Postavlja WiFi credentials.

**Request:**
```
GET /sysctrl.cgi?CMD=SET_SSID_PSWRD&SSID=MyNetwork&PSWRD=password123
```

**Response:**
```json
{
  "status": "success",
  "message": "WiFi credentials saved",
  "data": {
    "ssid": "MyNetwork"
  }
}
```

---

#### `GET_IP_ADDRESS`
Dohvata IP informacije.

**Response:**
```json
{
  "status": "success",
  "message": "IP address retrieved",
  "data": {
    "ip": "192.168.1.100",
    "subnet": "255.255.255.0",
    "gateway": "192.168.1.1"
  }
}
```

---

#### `GET_MDNS_NAME` / `SET_MDNS_NAME`

**GET Request:**
```
GET /sysctrl.cgi?CMD=GET_MDNS_NAME
```

**Response:**
```json
{
  "status": "success",
  "message": "mDNS name retrieved",
  "data": {
    "mdns": "soba"
  }
}
```

**SET Request:**
```
GET /sysctrl.cgi?CMD=SET_MDNS_NAME&MDNS=soba-101
```

---

#### `GET_TCPIP_PORT` / `SET_TCPIP_PORT`

**Response:**
```json
{
  "status": "success",
  "message": "TCP/IP port retrieved",
  "data": {
    "port": 80
  }
}
```

---

### 📊 **GET_STATUS** (Kompletan status uređaja)

**Request:**
```
GET /sysctrl.cgi?CMD=GET_STATUS
```

**Response:**
```json
{
  "status": "success",
  "message": "Status retrieved",
  "data": {
    "wifi": {
      "ssid": "MyNetwork",
      "password": "password123",
      "mdns": "soba",
      "ip": "192.168.1.100",
      "port": 80
    },
    "time": {
      "utc": "2026-01-24 15:30:00",
      "timezone_offset_minutes": 60,
      "dst_active": false
    },
    "sun": {
      "sunrise": "06:45",
      "sunset": "17:30"
    },
    "light": {
      "relay_state": "ON",
      "control_mode": "AUTO",
      "timer_on_type": "SUNSET",
      "timer_off_type": "TIME",
      "on_time": "18:00",
      "off_time": "23:00"
    },
    "ping_watchdog": true,
    "thermostat": {
      "temperature": 22.5,
      "setpoint": 25.0,
      "threshold": 0.5,
      "mode": "HEATING",
      "valve": "ON",
      "fan": "MEDIUM",
      "ema_alpha": 0.2,
      "fluid_temp": 45.3,
      "fluid_available": true
    },
    "sos": {
      "active": true,
      "timestamp": "2026-01-24 14:20:00"
    },
    "ir": {
      "received": true,
      "age_seconds": 120,
      "ctrl_mode": "HEATING",
      "state": "ON",
      "measured_temp": 22.50,
      "setpoint": 25,
      "fan_ctrl": 2,
      "fan_speed": 3
    }
  }
}
```

**Opis polja:**

- **wifi** - WiFi i network informacije
- **time** - Vremenske informacije (UTC + timezone)
- **sun** - Izlazak/zalazak sunca
- **light** - Status vanjske rasvjete i timer kontrola
- **ping_watchdog** - Da li je ping watchdog aktivan
- **thermostat** - Kompletne informacije o termostatu
  - `temperature` - Trenutna temperatura (°C)
  - `setpoint` - Željena temperatura (°C)
  - `threshold` - Histereza (°C)
  - `mode` - `"HEATING"`, `"COOLING"`, ili `"OFF"`
  - `valve` - Status pumpe/ventila (`"ON"` / `"OFF"`)
  - `fan` - Status ventilatora (`"OFF"`, `"LOW"`, `"MEDIUM"`, `"HIGH"`)
  - `fluid_temp` - Temperatura medija (ako je dostupan senzor), `-999` ako nije
  - `fluid_available` - Da li je fluid senzor prisutan
- **sos** - Status SOS signala iz toaleta
  - `active` - Da li je SOS aktivan
  - `timestamp` - Vrijeme kada je SOS pritisnut (perzistentno)
- **ir** - Podaci sa IR remote kontrole (za klima uređaj)
  - `received` - Da li su podaci primljeni
  - `age_seconds` - Koliko sekundi je prošlo od prijema
  - `ctrl_mode` - Mod kontrole (`"HEATING"`, `"COOLING"`, `"OFF"`)
  - `measured_temp` - Izmjerena temperatura sa toalet termostata

---

### 🌡️ **Termostat Komande**

#### `TH_SETPOINT`
Postavlja željenu temperaturu.

**Request:**
```
GET /sysctrl.cgi?CMD=TH_SETPOINT&VALUE=25
```

**Response:**
```json
{
  "status": "success",
  "message": "Thermostat setpoint updated",
  "data": {
    "setpoint": 25.0
  }
}
```

**Validacija:** VALUE mora biti 5-40 (°C)

---

#### `TH_DIFF`
Postavlja histerezi (threshold).

**Request:**
```
GET /sysctrl.cgi?CMD=TH_DIFF&VALUE=5
```

**Response:**
```json
{
  "status": "success",
  "message": "Thermostat threshold updated",
  "data": {
    "threshold": 0.5
  }
}
```

**Napomena:** VALUE je u 0.1°C jedinicama (5 = 0.5°C, 10 = 1.0°C)  
**Validacija:** VALUE mora biti 1-50 (0.1°C - 5.0°C)

---

#### `TH_STATUS`
Dohvata trenutni detaljni status termostata sa svim senzorima.

**Request:**
```
GET /sysctrl.cgi?CMD=TH_STATUS
```

**Response:**
```json
{
  "status": "success",
  "message": "Thermostat status retrieved",
  "data": {
    "status_text": "Temperature=22.5*C\nSetpoint=25.0*C\nMode=HEATING\nValve=ON\nFan=MEDIUM",
    "ema_alpha": 0.2,
    "fluid_temp": 45.3,
    "fluid_available": true
  }
}
```

**Napomena:** `fluid_temp` je temperatura fluida sa dodatnog DS18B20 senzora (ako je dostupan).

---

#### `TH_HEATING` / `TH_COOLING` / `TH_OFF` / `TH_ON`

**Request:**
```
GET /sysctrl.cgi?CMD=TH_HEATING
```

**Response:**
```json
{
  "status": "success",
  "message": "Thermostat mode set to HEATING",
  "data": {
    "mode": "HEATING"
  }
}
```

- `TH_HEATING` - Prebacuje u HEATING mod
- `TH_COOLING` - Prebacuje u COOLING mod
- `TH_OFF` - Isključuje termostat (čuva prethodni mod)
- `TH_ON` - Nastavlja sa prethodno sačuvanim modom

---

#### `TH_EMA`
Postavlja EMA filter alpha parametar.

**Request:**
```
GET /sysctrl.cgi?CMD=TH_EMA&VALUE=2
```

**Response:**
```json
{
  "status": "success",
  "message": "EMA filter updated",
  "data": {
    "ema_alpha": 0.2
  }
}
```

**Napomena:** VALUE je u 0.1 jedinicama (1-10 = 0.1-1.0)

---

### 📌 **ESP32 PIN Kontrola**

#### `ESP_GET_PINS`
Dohvata status svih dostupnih ESP32 pinova.

**Request:**
```
GET /sysctrl.cgi?CMD=ESP_GET_PINS
```

**Response:**
```json
{
  "status": "success",
  "message": "ESP32 pins status retrieved",
  "data": {
    "pins_status": "Available: 0,2,4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33,34,35,36,39"
  }
}
```

---

#### `ESP_SET_PIN`
Postavlja pin na HIGH.

**Request:**
```
GET /sysctrl.cgi?CMD=ESP_SET_PIN&PIN=25
```

**Response:**
```json
{
  "status": "success",
  "message": "PIN set HIGH",
  "data": {
    "pin": 25,
    "state": "HIGH"
  }
}
```

---

#### `ESP_RESET_PIN`
Postavlja pin na LOW.

**Request:**
```
GET /sysctrl.cgi?CMD=ESP_RESET_PIN&PIN=25
```

**Response:**
```json
{
  "status": "success",
  "message": "PIN set LOW",
  "data": {
    "pin": 25,
    "state": "LOW"
  }
}
```

---

#### `ESP_PULSE_PIN`
Pulsira pin (HIGH pa LOW nakon određenog vremena).

**Request:**
```
GET /sysctrl.cgi?CMD=ESP_PULSE_PIN&PIN=32&PAUSE=5
```

**Response:**
```json
{
  "status": "success",
  "message": "PIN pulsed",
  "data": {
    "pin": 32,
    "duration_seconds": 5
  }
}
```

**Default:** Ako PAUSE nije navedeno, koristi 2 sekunde.

---

### 💡 **Vanjska Rasvjeta**

#### `OUTDOOR_LIGHT_ON` / `OUTDOOR_LIGHT_OFF`

**Request:**
```
GET /sysctrl.cgi?CMD=OUTDOOR_LIGHT_ON
```

**Response:**
```json
{
  "status": "success",
  "message": "Outdoor light state changed",
  "data": {
    "state": "ON"
  }
}
```

**Napomena:** Ove komande aktiviraju **MANUAL** mod kontrole rasvjete.

---

### 🔔 **SOS Signal (Emergency)**

#### `SOS_RESET`
Resetuje SOS događaj nakon što je hitnost riješena.

**Request:**
```
GET /sysctrl.cgi?CMD=SOS_RESET
```

**Response:**
```json
{
  "status": "success",
  "message": "SOS event cleared from memory"
}
```

**Napomena:** SOS događaji su **perzistentni** (ostaju nakon restarta uređaja).

---

### 🕐 **Vrijeme i Timer**

#### `GET_TIME`

**Request:**
```
GET /sysctrl.cgi?CMD=GET_TIME
```

**Response:**
```json
{
  "status": "success",
  "message": "Current time retrieved",
  "data": {
    "date": "24.01.2026",
    "time": "15:30:45",
    "day": "Friday",
    "epoch": 1737731445
  }
}
```

---

#### `SET_TIME`

**Request:**
```
GET /sysctrl.cgi?CMD=SET_TIME&DATE=240126&TIME=153000
```

**Format:**
- `DATE` - DDMMYY (24.01.2026)
- `TIME` - HHMMSS (15:30:00)

**Response:**
```json
{
  "status": "success",
  "message": "RTC date and time set successfully",
  "data": {
    "date": "240126",
    "time": "153000"
  }
}
```

---

#### `GET_TIMER`
Dohvata timer postavke za automatsku kontrolu vanjske rasvjete.

**Request:**
```
GET /sysctrl.cgi?CMD=GET_TIMER
```

**Response:**
```json
{
  "status": "success",
  "message": "Timer settings retrieved",
  "data": {
    "timer_on": "SUNSET",
    "timer_off": "TIME",
    "on_time": "0000",
    "off_time": "0630",
    "sunrise": "07:23",
    "sunset": "16:45",
    "relay_state": "ON",
    "dst_active": true
  }
}
```

**Timer Types:**
- `OFF` - Timer isključen
- `TIME` - Fiksno vrijeme
- `SUNRISE` - Izlazak sunca
- `SUNSET` - Zalazak sunca

---

#### `SET_TIMER`
Postavlja timer za automatsku kontrolu rasvjete.

**Request:**
```
GET /sysctrl.cgi?CMD=SET_TIMER&TIMERON=SUNSET&TIMEROFF=TIME&ONTIME=0000&OFFTIME=0630
```

**Parametri:**
- `TIMERON` - OFF, TIME, SUNRISE, SUNSET
- `TIMEROFF` - OFF, TIME, SUNRISE, SUNSET
- `ONTIME` - HHMM (ako TIMERON=TIME)
- `OFFTIME` - HHMM (ako TIMEROFF=TIME)

**Response:**
```json
{
  "status": "success",
  "message": "Timer set successfully",
  "data": {
    "timer_on": "SUNSET",
    "timer_off": "TIME",
    "on_time": "0000",
    "off_time": "0630"
  }
}
```

---

### 🐕 **Ping Watchdog**

#### `GET_PINGWDG`

**Response:**
```json
{
  "status": "success",
  "message": "Ping watchdog state retrieved",
  "data": {
    "enabled": true
  }
}
```

---

#### `PINGWDG_ON` / `PINGWDG_OFF`

**Response:**
```json
{
  "status": "success",
  "message": "Ping watchdog enabled",
  "data": {
    "enabled": true
  }
}
```

---

### 🔄 **System**

#### `RESTART`
Restartuje ESP32 uređaj.

**Response:**
```json
{
  "status": "success",
  "message": "Restart in 3s"
}
```

---

### 🚪 **RS485 Komande (IC Kontroler)**

Ove komande komuniciraju sa RS485 uređajima (IC kontrolerima) i vraćaju njihove odgovore.

#### `RESTART_CTRL`
Restartuje IC kontroler.

**Request:**
```
GET /sysctrl.cgi?CMD=RESTART_CTRL&ID=153
```

**Response:**
```json
{
  "status": "success",
  "message": "Controller restart OK",
  "data": {
    "result": "Controller restart OK"
  }
}
```

---

#### `GET_ROOM_STATUS`
Dohvata status sobe (da li je kartica uložena).

**Request:**
```
GET /sysctrl.cgi?CMD=GET_ROOM_STATUS&ID=153
```

**Response:**
```json
{
  "status": "success",
  "message": "Room status retrieved",
  "data": {
    "room_status": "GUEST_IN",
    "card_inserted": true
  }
}
```

**Moguće vrijednosti:**
- `room_status`: "GUEST_IN" ili "EMPTY"
- `card_inserted`: true/false

---

#### `SET_LANG`
Postavlja jezik na IC kontroleru.

**Request:**
```
GET /sysctrl.cgi?CMD=SET_LANG&ID=152&VALUE=1
```

**Parametri:**
- `VALUE`: 0=SRB, 1=ENG, 2=GER

**Response:**
```json
{
  "status": "success",
  "message": "Language set OK",
  "data": {
    "result": "Language set OK"
  }
}
```

---

#### `QR_CODE_SET`
Postavlja QR kod na IC kontroler.

**Request:**
```
GET /sysctrl.cgi?CMD=QR_CODE_SET&ID=153&QR_CODE=WIFI:S:MyNet;T:WPA;P:1234;;
```

**Response:**
```json
{
  "status": "success",
  "message": "QR code set OK",
  "data": {
    "result": "QR code set OK"
  }
}
```

**Error Response:**
```json
{
  "status": "success",
  "message": "QR code set FAILED",
  "data": {
    "result": "QR code set FAILED"
  }
}
```

---

#### `SET_SYSID`
Postavlja System ID na IC kontroleru.

**Request:**
```
GET /sysctrl.cgi?CMD=SET_SYSID&ID=154&VALUE=42444
```

**Response:**
```json
{
  "status": "success",
  "message": "System ID set OK",
  "data": {
    "result": "System ID set OK"
  }
}
```

---

#### `GET_SYSID`
Dohvata System ID sa IC kontrolera.

**Request:**
```
GET /sysctrl.cgi?CMD=GET_SYSID&ID=154
```

**Response:**
```json
{
  "status": "success",
  "message": "System ID retrieved",
  "data": {
    "system_id": 42444,
    "system_id_hex": "0xA5DC"
  }
}
```

---

#### `GET_ROOM_TEMP`

**Request:**
```
GET /sysctrl.cgi?CMD=GET_ROOM_TEMP&ID=10
```

**Response:**
```json
{
  "room_temperature": 22,
  "setpoint_temperature": 25
}
```

---

#### `GET_PASSWORD`

**Request:**
```
GET /sysctrl.cgi?CMD=GET_PASSWORD&ID=154&TYPE=GUEST&GUEST_ID=1
```

**Response (Guest):**
```json
{
  "user_type": "guest",
  "guest_id": 5,
  "password": 123456,
  "expiry": "2026-02-01 12:00"
}
```

**Response (Staff):**
```json
{
  "user_type": "staff",
  "password": 789012
}
```

---

#### `OPEN_DOOR`

**Request:**
```
GET /sysctrl.cgi?CMD=OPEN_DOOR&ID=153
```

**Response:**
```json
{
  "result": "Door opened"
}
```

---

## 🐍 Python Primjeri

### Instalacija zavisnosti

```bash
pip install requests
```

### Primjer 1: Dohvati status sobe

```python
import requests

BASE_URL = "http://192.168.1.100/sysctrl.cgi"

def get_room_status(device_id):
    response = requests.get(f"{BASE_URL}?CMD=GET_ROOM_STATUS&ID={device_id}")
    data = response.json()
    
    if data['status'] == 'success':
        room = data['data']
        print(f"Room Status: {room['room_status']}")
        print(f"Card Inserted: {room['card_inserted']}")
    else:
        print(f"Error: {data['message']}")

get_room_status(153)
```

### Primjer 2: Kontrola timera rasvjete

```python
def set_light_timer():
    params = {
        "CMD": "SET_TIMER",
        "TIMERON": "SUNSET",
        "TIMEROFF": "TIME",
        "ONTIME": "0000",
        "OFFTIME": "0630"
    }
    
    response = requests.get(BASE_URL, params=params)
    data = response.json()
    
    if data['status'] == 'success':
        timer = data['data']
        print(f"Timer set: ON={timer['timer_on']}, OFF={timer['timer_off']}")
    else:
        print(f"Error: {data['message']}")

set_light_timer()
```

### Primjer 3: Restart IC kontrolera

```python
def restart_controller(device_id):
    response = requests.get(f"{BASE_URL}?CMD=RESTART_CTRL&ID={device_id}")
    data = response.json()
    
    if data['status'] == 'success':
        print(f"✅ {data['data']['result']}")
    else:
        print(f"❌ Error: {data['message']}")

restart_controller(153)
```

### Primjer 4: Dohvati System ID

```python
def get_system_id(device_id):
    response = requests.get(f"{BASE_URL}?CMD=GET_SYSID&ID={device_id}")
    data = response.json()
    
    if data['status'] == 'success':
        sysid = data['data']
        print(f"System ID: {sysid['system_id']} ({sysid['system_id_hex']})")
    else:
        print(f"Error: {data['message']}")

get_system_id(154)
```

### Primjer 5: Dohvati status

```python
import requests

BASE_URL = "http://192.168.1.100/sysctrl.cgi"

def get_status():
    response = requests.get(f"{BASE_URL}?CMD=GET_STATUS")
    data = response.json()
    
    if data['status'] == 'success':
        thermo = data['data']['thermostat']
        print(f"🌡️ Temperatura: {thermo['temperature']}°C")
        print(f"🎯 Setpoint: {thermo['setpoint']}°C")
        print(f"🔥 Mod: {thermo['mode']}")
        print(f"💨 Fan: {thermo['fan']}")
        
        # Provjeri SOS
        sos = data['data']['sos']
        if sos['active']:
            print(f"⚠️ SOS AKTIVAN: {sos['timestamp']}")
    else:
        print(f"❌ Error: {data['message']}")

get_status()
```

### Primjer 2: Postavi termostat

```python
def set_thermostat(setpoint, mode="HEATING"):
    # Postavi setpoint
    response = requests.get(f"{BASE_URL}?CMD=TH_SETPOINT&VALUE={setpoint}")
    if response.json()['status'] == 'success':
        print(f"✅ Setpoint postavljen na {setpoint}°C")
    
    # Postavi mod
    response = requests.get(f"{BASE_URL}?CMD=TH_{mode}")
    if response.json()['status'] == 'success':
        print(f"✅ Mod postavljen na {mode}")

set_thermostat(25, "HEATING")
```

### Primjer 3: Monitoring SOS događaja

```python
import time

def monitor_sos():
    print("🔍 Monitoring SOS događaja...")
    
    while True:
        response = requests.get(f"{BASE_URL}?CMD=GET_STATUS")
        data = response.json()
        
        if data['status'] == 'success':
            sos = data['data']['sos']
            
            if sos['active']:
                print(f"\n⚠️ SOS ALARM!")
                print(f"   Vrijeme: {sos['timestamp']}")
                
                # Ovdje možeš dodati logiku za slanje notifikacije
                # send_notification(sos['timestamp'])
                
                # Čekaj da admin resetuje
                input("Pritisni ENTER nakon što riješiš SOS...")
                
                # Resetuj SOS
                reset_resp = requests.get(f"{BASE_URL}?CMD=SOS_RESET")
                if reset_resp.json()['status'] == 'success':
                    print("✅ SOS resetovan")
        
        time.sleep(5)  # Provjeri svakih 5 sekundi

# monitor_sos()  # Odkomentiraj za pokretanje
```

### Primjer 4: IR Remote Data

```python
def get_ir_data():
    response = requests.get(f"{BASE_URL}?CMD=GET_STATUS")
    data = response.json()
    
    if data['status'] == 'success':
        ir = data['data']['ir']
        
        if ir['received']:
            print(f"📡 IR Podaci primljeni prije {ir['age_seconds']}s")
            print(f"   Mod: {ir['ctrl_mode']}")
            print(f"   State: {ir['state']}")
            print(f"   Temp: {ir['measured_temp']}°C")
            print(f"   Setpoint: {ir['setpoint']}°C")
        else:
            print("❌ IR podaci nisu primljeni")

get_ir_data()
```

### Primjer 5: Error handling

```python
def safe_request(cmd, params=None):
    try:
        url = f"{BASE_URL}?CMD={cmd}"
        if params:
            url += "&" + "&".join([f"{k}={v}" for k, v in params.items()])
        
        response = requests.get(url, timeout=5)
        data = response.json()
        
        if data['status'] == 'success':
            return data.get('data', {})
        else:
            print(f"❌ Error [{data.get('code', 'N/A')}]: {data['message']}")
            return None
            
    except requests.Timeout:
        print("❌ Timeout: Uređaj ne odgovara")
        return None
    except Exception as e:
        print(f"❌ Exception: {e}")
        return None

# Primjer upotrebe
result = safe_request("TH_SETPOINT", {"VALUE": 25})
if result:
    print(f"Setpoint: {result['setpoint']}")
```

---

## 🔄 Migracija sa Starog Formata

### Stari format (Text):
```
Temperature=22.5*C
Setpoint=25.0*C
Mode=HEATING
```

### Novi format (JSON):
```json
{
  "status": "success",
  "message": "Thermostat status retrieved",
  "data": {
    "temperature": 22.5,
    "setpoint": 25.0,
    "mode": "HEATING"
  }
}
```

### Kod adaptacija:

**Stari Python kod:**
```python
response = requests.get(url)
text = response.text
temp = float(text.split("Temperature=")[1].split("*C")[0])
```

**Novi Python kod:**
```python
response = requests.get(url)
data = response.json()
if data['status'] == 'success':
    temp = data['data']['temperature']
```

---

## 📝 Napomene

1. **Content-Type:** Svi odgovori su `application/json`
2. **Encoding:** UTF-8
3. **HTTP Method:** Sve komande koriste GET (radi kompatibilnosti sa legacy sistemima)
4. **Timeout:** RS485 komande imaju timeout od ~2 sekunde
5. **Perzistentnost:** SOS događaji ostaju u memoriji nakon restarta
6. **Rate limiting:** Nema ograničenja, ali se preporučuje max 10 req/sec

---

## 🆘 Troubleshooting

### Problem: `{"status": "error", "code": 408, "message": "Timeout..."}`
**Rješenje:** RS485 uređaj ne odgovara. Provjeri:
- Da li je uređaj uključen
- Da li je RS485 bus ispravan
- Da li je ID ispravan

### Problem: `{"status": "error", "code": 400, "message": "Missing ... parameter"}`
**Rješenje:** Nedostaje obavezni parametar u zahтјеvu.

### Problem: JSON parse error
**Rješenje:** Provjeri da li stari kod pokušava parsirati novi JSON response kao text.

---

## 📚 Reference

- **ArduinoJson:** https://arduinojson.org/
- **ESP32 Async WebServer:** https://github.com/me-no-dev/ESPAsyncWebServer
- **TinyFrame Protocol:** Interna RS485 komunikacija

---

**Verzija:** 2.0  
**Datum:** 24.01.2026  
**Autor:** HTTP Bridge ESP32 Team
