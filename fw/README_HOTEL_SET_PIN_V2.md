# Implementacija HOTEL_SET_PIN_V2 - Rezime

## Šta je urađeno

✅ **Dodato u ESP32 HTTP Bridge:**

1. **Enum konstanta** - `CMD_HOTEL_SET_PIN_V2 = 0xDB` u [src/main.cpp](src/main.cpp#L172)
2. **String konvertor** - `stringToCommand()` prepoznaje "HOTEL_SET_PIN_V2" u [src/main.cpp](src/main.cpp#L240-L242)
3. **HTTP handler** - Validacija parametara i kreiranje RS485 poruke u [src/main.cpp](src/main.cpp#L1084-L1143)
4. **Obrada odgovora** - Parsiranje odgovora sa IC kontrolera u [src/main.cpp](src/main.cpp#L1759-L1761)

## Dokumentacija

📄 [HOTEL_SET_PIN_V2_implementation.md](HOTEL_SET_PIN_V2_implementation.md) - Detaljna dokumentacija sa primerima  
🧪 [HOTEL_SET_PIN_V2_tests.md](HOTEL_SET_PIN_V2_tests.md) - Test scenariji i troubleshooting

## HTTP API

### Format zahtjeva

```
GET /sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=<id>&PORT=<port>&PIN=<pin>&VALUE=<value>
```

### Parametri

| Parametar | Tip | Opseg | Opis |
| --- | --- | --- | --- |
| `CMD` | String | "HOTEL_SET_PIN_V2" | Ime komande |
| `ID` | Integer | 1-254 | Adresa IC kontrolera |
| `PORT` | Char | A-K | GPIO port |
| `PIN` | Integer | 0-15 | Broj pina |
| `VALUE` | Integer | 0/1 | LOW/HIGH |

### Primjer otvaranja vrata

```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=1"
```

## Kako funkcioniše

### 1. HTTP zahtjev → ESP32

Korisnik šalje HTTP GET zahtjev sa parametrima

### 2. ESP32 → RS485 poruka

ESP32 kreira RS485 poruku:

```
Bajt[0] = 0xDB              // HOTEL_SET_PIN_V2
Bajt[1] = ID                // Adresa uređaja (1-254)
Bajt[2] = 254               // Reserved
Bajt[3] = PORT              // 'A'-'K'
Bajt[4] = PIN               // 0-15
Bajt[5] = VALUE             // 0/1
```

### 3. IC kontroler → Obrada

IC kontroler prima poruku i:

- Ako je `PORT='C', PIN=8, VALUE=1` → **Otvara vrata** (specijalni slučaj)
- Inače → `SetPinv2()` direktno setuje pin

### 4. IC kontroler → ESP32 → HTTP odgovor

ESP32 prima odgovor i šalje HTTP odgovor korisniku

## Razlike sa SET_PIN

| | SET_PIN (0xB1) | HOTEL_SET_PIN_V2 (0xDB) |
| --- | --- | --- |
| **PORT argument** | ❌ | ✅ A-K |
| **PIN opseg** | 1-6 | 0-15 |
| **Otvaranje vrata** | ❌ | ✅ C,8,1 |

## Specijalna funkcija - Otvaranje vrata

Kada se pošalje `PORT='C', PIN=8, VALUE=1`:

1. IC kontroler provjerava `PersonHasEntered()`
2. Ako nisu vrata već otvorena:
   - Aktivira PC8 pin (otključava vrata)
   - Startuje timer za automatsko zaključavanje
   - Šalje mrežni signal `GUEST_ENTER_PASSWORD_RS485`
3. Timer automatski zaključava vrata nakon isteka

### Zaštita od spamovanja

- Ako su vrata već otključana, ponovna komanda se ignoriše
- Timer se ne resetuje tokom otključavanja
- Nakon zaključavanja, komanda je ponovo spremna

## Testiranje

### Brzi test otvaranja vrata

```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&PORT=C&PIN=8&VALUE=1"
```

### Ostali testovi

Vidjeti detaljne test scenarije u [HOTEL_SET_PIN_V2_tests.md](HOTEL_SET_PIN_V2_tests.md)

## Struktura projekta

```
fw/
├── src/
│   └── main.cpp                              # ESP32 kod (✅ Izmijenjeno)
├── include/
│   └── TinyFrame.h                           # TinyFrame header
├── rs485.c                                   # IC kontroler referenca
├── RS485_commands_analysis.md                # Analiza komandi
├── HOTEL_SET_PIN_V2_implementation.md        # 📄 Detaljna dokumentacija
├── HOTEL_SET_PIN_V2_tests.md                 # 🧪 Test scenariji
└── README_HOTEL_SET_PIN_V2.md                # 📋 Ovaj fajl
```

## Sledeći koraci

1. **Kompajliraj i upload** ESP32 firmware
2. **Testiraj osnovno** - Pošalji test zahtjev
3. **Testiraj otvaranje vrata** - PORT=C, PIN=8, VALUE=1
4. **Proveri RS485 komunikaciju** - Serial monitor
5. **Integriši sa sistemom** - Dodaj u svoju automatizaciju

## Pomoć i troubleshooting

### Problem: "TIMEOUT"

- Proveri RS485 konekciju
- Proveri ID parametar
- Proveri da li je IC kontroler uključen

### Problem: Vrata se ne otvaraju

- Proveri da li je `PORT='C', PIN=8, VALUE=1`
- Proveri hardver (relej, brava)
- Proveri da li je `PersonHasEntered()` već aktivan

### Dodatne informacije

Vidjeti [HOTEL_SET_PIN_V2_tests.md](HOTEL_SET_PIN_V2_tests.md) za detaljne troubleshooting korake.

## Kontakt

Za dodatna pitanja ili probleme, kontaktiraj autora ili otvori issue.

---

**Verzija:** 1.0  
**Datum:** Januar 2026  
**Status:** ✅ Spremno za testiranje
