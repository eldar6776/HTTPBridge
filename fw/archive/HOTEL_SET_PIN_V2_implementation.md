# HOTEL_SET_PIN_V2 - Implementacija u ESP32 HTTP Bridge

## Opis komande

`HOTEL_SET_PIN_V2` (0xDB) je napredna komanda za setovanje GPIO pinova na IC kontroleru sa dodatnom funkcionalnostju za otvaranje vrata.

## Razlike između SET_PIN i HOTEL_SET_PIN_V2

| Karakteristika | SET_PIN (0xB1) | HOTEL_SET_PIN_V2 (0xDB) |
|---|---|---|
| **Hex kod** | 0xB1 | 0xDB |
| **PORT argument** | ❌ Nema | ✅ Karakter A-K |
| **PIN argument** | 1-6 | 0-15 (pun opseg) |
| **Specijalna funkcija** | ❌ Nema | ✅ Otvaranje vrata (C, 8, 1) |
| **Maksimalna fleksibilnost** | Ograničena | Puna kontrola |

## Struktura RS485 poruke


Bajt[0] = 0xDB              // Komanda HOTEL_SET_PIN_V2
Bajt[1] = ID                // Adresa uređaja (1-254)
Bajt[2] = 254               // Reserved/Broadcast marker
Bajt[3] = PORT              // ASCII karakter 'A'-'K' (GPIO port)
Bajt[4] = PIN               // Broj pina 0-15
Bajt[5] = VALUE             // 0 = LOW, 1 = HIGH


## HTTP API korišćenje

### Endpoint

```
GET /sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=<id>&PORT=<port>&PIN=<pin>&VALUE=<value>
```

### Parametri

| Parametar | Tip | Opseg | Opis |
|---|---|---|---|
| `CMD` | String | "HOTEL_SET_PIN_V2" | Ime komande |
| `ID` | Integer | 1-254 | Adresa IC kontrolera na RS485 busu |
| `PORT` | Char | A-K | GPIO port (A=GPIOA, B=GPIOB, ..., K=GPIOK) |
| `PIN` | Integer | 0-15 | Broj pina unutar porta |
| `VALUE` | Integer | 0 ili 1 | 0 = LOW, 1 = HIGH |

## Primjeri

### 1. Otvaranje vrata (Specijalni slučaj)

Ovo je najvažnija funkcionalnost - otvaranje vrata sa PC8 pinom:

```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=1"
```

**Šta se dešava:**

1. ESP32 šalje RS485 poruku IC kontroleru
2. IC kontroler detektuje PORT='C', PIN=8, VALUE=1
3. Provjerava da li je osoba već ušla (`PersonHasEntered()`)
4. Ako nije, aktivira `PersonEnter(GUEST_ENTER_PASSWORD)`:
   - Pali PC8 pin (otključava vrata)
   - Startuje tajmer za automatsko zaključavanje
   - Šalje mrežni signal `GUEST_ENTER_PASSWORD_RS485`
5. Timer automatski zaključava vrata nakon isteka

### 2. Setovanje drugog GPIO pina na HIGH

```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=5&PORT=A&PIN=3&VALUE=1"
```

Postavlja GPIOA Pin 3 na HIGH na uređaju sa ID=5.

### 3. Resetovanje GPIO pina na LOW

```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=5&PORT=A&PIN=3&VALUE=0"
```

Postavlja GPIOA Pin 3 na LOW na uređaju sa ID=5.

### 4. Kontrola releja na GPIOB Port 5

```bash
# Pali relej
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=2&PORT=B&PIN=5&VALUE=1"

# Gasi relej
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=2&PORT=B&PIN=5&VALUE=0"
```

### 5. Višestruki uređaji na RS485 busu

```bash
# Soba 1 (ID=1) - otvori vrata
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=1"

# Soba 2 (ID=2) - pali svjetlo
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=2&PORT=D&PIN=2&VALUE=1"

# Soba 3 (ID=3) - kontroliši Fan
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=3&PORT=E&PIN=7&VALUE=1"
```

## Odgovor API-ja

### Uspješan odgovor

```
HTTP/1.1 200 OK
Content-Type: text/plain

Pin Set V2 OK
```

### Greške

#### Nedostaju parametri

```
HTTP/1.1 400 Bad Request
Missing ID, PORT, PIN or VALUE
```

#### Nevažeći ID

```
HTTP/1.1 400 Bad Request
Invalid ID (must be 1-254)
```

#### Nevažeći PORT

```
HTTP/1.1 400 Bad Request
Invalid PORT (must be A-K)
```

#### Nevažeći PIN

```
HTTP/1.1 400 Bad Request
Invalid PIN (must be 0-15)
```

#### Nevažeći VALUE

```
HTTP/1.1 400 Bad Request
Invalid VALUE (must be 0 or 1)
```

#### Timeout (nema odgovora sa IC kontrolera)

```
HTTP/1.1 200 OK
TIMEOUT
```

## Mapiranje GPIO Portova

| PORT karakter | GPIO Port na STM32 |
|---|---|
| A | GPIOA |
| B | GPIOB |
| C | GPIOC |
| D | GPIOD |
| E | GPIOE |
| F | GPIOF |
| G | GPIOG |
| H | GPIOH |
| I | GPIOI |
| J | GPIOJ |
| K | GPIOK |

## Tehnička napomena o otvaranju vrata

**Zaštita od višestrukog okidanja:**

Kada se pošalje komanda za otvaranje vrata (`PORT=C, PIN=8, VALUE=1`), IC kontroler ima ugrađenu zaštitu:

1. **Prvi put**: Vrata se otključavaju, PC8 ide na HIGH, timer kreće
2. **Tokom otključavanja**: Ako se komanda pošalje ponovo dok su vrata otključana, `PersonHasEntered()` vraća `true` i komanda se ignoriše
3. **Nakon isteka timera**: Timer automatski zaključava vrata, PC8 ide na LOW
4. **Ponovo spremno**: Komanda je ponovo spremna za novo otvaranje

Ovo sprječava:

- Spamovanje mreže sa mrežnim signalima
- Nepotrebno resetovanje timera
- Neočekivano ponašanje sistema

## Integracija sa automatizacijom

### Node-RED primjer

```javascript
// Otvaranje vrata preko RFID kartice
msg.url = "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=1";
return msg;
```

### Python skripta

```python
import requests

def open_door(bridge_ip, room_id):
    """Otvori vrata hotela"""
    url = f"http://{bridge_ip}/sysctrl.cgi"
    params = {
        'CMD': 'HOTEL_SET_PIN_V2',
        'ID': room_id,
        'PORT': 'C',
        'PIN': 8,
        'VALUE': 1
    }
    
    response = requests.get(url, params=params, timeout=5)
    if response.text == "Pin Set V2 OK":
        print(f"Vrata sobe {room_id} otvorena!")
    elif response.text == "TIMEOUT":
        print(f"Kontroler sobe {room_id} ne odgovara!")
    else:
        print(f"Greška: {response.text}")

# Korišćenje
open_door("192.168.1.100", 1)
```

### Home Assistant Automation

```yaml
- service: rest_command.hotel_open_door
  data:
    bridge_ip: "192.168.1.100"
    room_id: 1
```

## Debugging

Za debugging RS485 komunikacije, možete koristiti Serial monitor ESP32:

```cpp
Serial.print("Sending HOTEL_SET_PIN_V2: ");
for (int i = 0; i < length; i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX);
    Serial.print(' ');
}
Serial.println();
```

Primjer izlaza:

```
Sending HOTEL_SET_PIN_V2: DB 01 FE 43 08 01
```

Gdje:

- `DB` = HOTEL_SET_PIN_V2 komanda
- `01` = ID uređaja = 1
- `FE` = Reserved (254)
- `43` = ASCII 'C' (PORT C)
- `08` = PIN 8
- `01` = VALUE HIGH

## Sigurnosne preporuke

1. **Ograničen pristup**: HTTP API ne bi trebao biti dostupan sa javnog interneta
2. **Firewall**: Koristite firewall za ograničavanje pristupa samo sa lokalnih adresa
3. **Autentifikacija**: Razmislite o dodavanju Basic Auth ili API tokena
4. **Rate limiting**: Limitirajte broj zahtjeva po minuti za otvaranje vrata
5. **Logging**: Logujte sve pokušaje otvaranja vrata sa IP adresom i timestamp-om

## Dodatna dokumentacija

- Vidjeti [RS485_commands_analysis.md](RS485_commands_analysis.md) za detaljnu analizu svih komandi
- Vidjeti [rs485.c](rs485.c) za implementaciju na IC kontroleru
- Vidjeti [main.cpp](src/main.cpp) za implementaciju ESP32 bridge-a
