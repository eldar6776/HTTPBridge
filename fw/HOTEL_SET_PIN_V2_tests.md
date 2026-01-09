# Test Primjeri za HOTEL_SET_PIN_V2

## Brzo testiranje

### 1. Test otvaranja vrata (najvažniji test)

```bash
# Zamijenite 192.168.1.100 sa IP adresom vašeg ESP32 bridgea
# Zamijenite ID=1 sa ID-em vašeg IC kontrolera

curl -v "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=1"
```

**Očekivani rezultat:**

< HTTP/1.1 200 OK
< Content-Type: text/plain
Pin Set V2 OK

**Šta treba posmatrati na IC kontroleru:**

- PC8 pin treba da ode na HIGH
- LED ili relej za vrata treba da se aktivira
- Nakon 5-10 sekundi (ili konfigurisano vrijeme), pin automatski ide na LOW

---

### 2. Test sa različitim portovima i pinovima

```bash
# Test GPIOA Pin 0
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=A&PIN=0&VALUE=1"

# Test GPIOB Pin 5
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=B&PIN=5&VALUE=1"

# Test GPIOD Pin 15 (maksimalni pin)
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=D&PIN=15&VALUE=0"
```

---

### 3. Negativni testovi (trebaju vratiti greške)

```bash
# Nedostaje parametar PORT
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PIN=8&VALUE=1"
# Očekivano: "Missing ID, PORT, PIN or VALUE"

# Nevažeći PORT (L nije validan)
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=L&PIN=8&VALUE=1"
# Očekivano: "Invalid PORT (must be A-K)"

# Nevažeći PIN (16 je van opsega)
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=16&VALUE=1"
# Očekivano: "Invalid PIN (must be 0-15)"

# Nevažeći VALUE (2 nije 0 ili 1)
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=2"
# Očekivano: "Invalid VALUE (must be 0 or 1)"

# Nevažeći ID (0 je van opsega)
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=0&PORT=C&PIN=8&VALUE=1"
# Očekivano: "Invalid ID (must be 1-254)"

# Nevažeći ID (255 je van opsega)
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=255&PORT=C&PIN=8&VALUE=1"
# Očekivano: "Invalid ID (must be 1-254)"
```

---

### 4. Test višestrukih uređaja na RS485 busu

Ako imate više IC kontrolera na RS485 busu:

```bash
# Otvori vrata sobe 1
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=1"

# Otvori vrata sobe 2
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=2&PORT=C&PIN=8&VALUE=1"

# Otvori vrata sobe 3
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=3&PORT=C&PIN=8&VALUE=1"
```

---

### 5. Test zaštite od spamovanja (otvaranje vrata)

Pokušajte poslati komandu dva puta zaredom vrlo brzo:

```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=1"
sleep 1
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=1"
```

**Očekivano ponašanje:**

1. Prva komanda otvara vrata i aktivira timer
2. Druga komanda bi trebala biti ignorisana (ili tretirana kao običan SetPinv2) jer je `PersonHasEntered()` već true

---

## Python test skripta

Spremite kao `test_hotel_pin.py`:

```python
#!/usr/bin/env python3
import requests
import time
import sys

# Konfiguracija
BRIDGE_IP = "192.168.1.100"  # IP adresa ESP32 bridgea
ROOM_ID = 1                   # ID IC kontrolera

def test_command(port, pin, value, description):
    """Test pojedinačne HOTEL_SET_PIN_V2 komande"""
    url = f"http://{BRIDGE_IP}/sysctrl.cgi"
    params = {
        'CMD': 'HOTEL_SET_PIN_V2',
        'ID': ROOM_ID,
        'PORT': port,
        'PIN': pin,
        'VALUE': value
    }
    
    print(f"\n[TEST] {description}")
    print(f"  PORT={port}, PIN={pin}, VALUE={value}")
    
    try:
        response = requests.get(url, params=params, timeout=5)
        print(f"  ✓ Status: {response.status_code}")
        print(f"  ✓ Response: {response.text}")
        return response.status_code == 200
    except requests.exceptions.Timeout:
        print(f"  ✗ TIMEOUT!")
        return False
    except Exception as e:
        print(f"  ✗ ERROR: {e}")
        return False

def main():
    print("=" * 60)
    print("HOTEL_SET_PIN_V2 Test Suite")
    print("=" * 60)
    print(f"Bridge IP: {BRIDGE_IP}")
    print(f"Room ID: {ROOM_ID}")
    
    tests = [
        # (PORT, PIN, VALUE, Description)
        ('C', 8, 1, "Otvaranje vrata (specijalni slučaj)"),
        ('A', 0, 1, "Test GPIOA Pin 0 = HIGH"),
        ('A', 0, 0, "Test GPIOA Pin 0 = LOW"),
        ('B', 5, 1, "Test GPIOB Pin 5 = HIGH"),
        ('D', 15, 1, "Test GPIOD Pin 15 = HIGH (maksimalni pin)"),
        ('K', 0, 1, "Test GPIOK Pin 0 = HIGH (maksimalni port)"),
    ]
    
    passed = 0
    failed = 0
    
    for port, pin, value, desc in tests:
        if test_command(port, pin, value, desc):
            passed += 1
        else:
            failed += 1
        time.sleep(0.5)  # Pauza između testova
    
    print("\n" + "=" * 60)
    print(f"REZULTATI: {passed} passed, {failed} failed")
    print("=" * 60)
    
    return 0 if failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
```

**Pokretanje:**

```bash
chmod +x test_hotel_pin.py
./test_hotel_pin.py
```

---

## Postman Collection

Import ovaj JSON u Postman za lako testiranje:

```json
{
  "info": {
    "name": "HOTEL_SET_PIN_V2 Tests",
    "schema": "https://schema.getpostman.com/json/collection/v2.1.0/collection.json"
  },
  "variable": [
    {
      "key": "bridge_ip",
      "value": "192.168.1.100"
    },
    {
      "key": "room_id",
      "value": "1"
    }
  ],
  "item": [
    {
      "name": "Open Door (C,8,1)",
      "request": {
        "method": "GET",
        "header": [],
        "url": {
          "raw": "http://{{bridge_ip}}/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID={{room_id}}&PORT=C&PIN=8&VALUE=1",
          "protocol": "http",
          "host": ["{{bridge_ip}}"],
          "path": ["sysctrl.cgi"],
          "query": [
            {"key": "CMD", "value": "HOTEL_SET_PIN_V2"},
            {"key": "ID", "value": "{{room_id}}"},
            {"key": "PORT", "value": "C"},
            {"key": "PIN", "value": "8"},
            {"key": "VALUE", "value": "1"}
          ]
        }
      }
    },
    {
      "name": "Set GPIOA Pin 0 HIGH",
      "request": {
        "method": "GET",
        "header": [],
        "url": {
          "raw": "http://{{bridge_ip}}/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID={{room_id}}&PORT=A&PIN=0&VALUE=1",
          "protocol": "http",
          "host": ["{{bridge_ip}}"],
          "path": ["sysctrl.cgi"],
          "query": [
            {"key": "CMD", "value": "HOTEL_SET_PIN_V2"},
            {"key": "ID", "value": "{{room_id}}"},
            {"key": "PORT", "value": "A"},
            {"key": "PIN", "value": "0"},
            {"key": "VALUE", "value": "1"}
          ]
        }
      }
    },
    {
      "name": "Set GPIOA Pin 0 LOW",
      "request": {
        "method": "GET",
        "header": [],
        "url": {
          "raw": "http://{{bridge_ip}}/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID={{room_id}}&PORT=A&PIN=0&VALUE=0",
          "protocol": "http",
          "host": ["{{bridge_ip}}"],
          "path": ["sysctrl.cgi"],
          "query": [
            {"key": "CMD", "value": "HOTEL_SET_PIN_V2"},
            {"key": "ID", "value": "{{room_id}}"},
            {"key": "PORT", "value": "A"},
            {"key": "PIN", "value": "0"},
            {"key": "VALUE", "value": "0"}
          ]
        }
      }
    }
  ]
}
```

---

## Serial Monitor Debug

Povežite se na ESP32 preko Serial monitora (115200 baud) i posmatrajte:

```
Sending command: DB 01 FE 43 08 01
```

Gdje:

- `DB` = HOTEL_SET_PIN_V2
- `01` = ID=1
- `FE` = Reserved
- `43` = ASCII 'C'
- `08` = PIN 8
- `01` = VALUE HIGH

---

## Troubleshooting

### Problem: "TIMEOUT" odgovor

**Razlog:** IC kontroler ne odgovara

**Provjera:**

1. Da li je IC kontroler uključen?
2. Da li je RS485 kabel ispravno povezan?
3. Da li je ID parametar tačan?
4. Provjerite RS485 A/B polaritet

### Problem: "Missing ID, PORT, PIN or VALUE"

**Razlog:** Nedostaje neki parametar u HTTP zahtjevu

**Rješenje:** Provjerite da ste uključili sve 4 parametra:

```
CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=1
```

### Problem: Vrata se ne otvaraju

**Provjera:**

1. Da li je PORT='C', PIN=8, VALUE=1?
2. Da li je `PersonHasEntered()` već aktivan (vrata već otvorena)?
3. Provjerite hardware pin PC8 na IC kontroleru
4. Provjerite da li relej/brave funkcionišu

---

## Napredni test - Stres test

Test 10 otvaranja vrata sa pauzom:

```bash
for i in {1..10}; do
  echo "Test #$i"
  curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=1"
  echo ""
  sleep 15  # Čekaj da timer istekne prije sljedećeg testa
done
```

**Rezultat:** Svaki zahtjev bi trebao uspješno otvoriti vrata nakon što timer istekne.
