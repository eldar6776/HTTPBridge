# Password Management API - ESP32 HTTP Bridge

## Pregled

ESP32 HTTP Bridge sada podržava **potpuno upravljanje lozinkama** na IC kontrolerima preko RS485 komunikacije prema `common.h` protokolu.

---

## 1. SET_PASSWORD - Postavljanje Lozinki

### Endpoint
```
GET /sysctrl.cgi?CMD=SET_PASSWORD&ID=<id>&TYPE=<type>&PASSWORD=<pass>[&GUEST_ID=<1-8>][&EXPIRY=<HHMMDDMMYY>]
```

### Tipovi Korisnika

#### A. Guest Lozinka (ID 1-8)

**Parametri:**
- `ID` = Device ID na RS485 (1-254)
- `TYPE` = "GUEST"
- `GUEST_ID` = Broj gosta (1-8)
- `PASSWORD` = Numerička šifra
- `EXPIRY` = Vrijeme isteka u formatu **HHMMDDMMYY**

**Format EXPIRY:**
- `HH` = Sati (00-23)
- `MM` = Minuti (00-59)
- `DD` = Dan (01-31)
- `MM` = Mjesec (01-12)
- `YY` = Godina (00-99, npr. 26 = 2026)

**Primjer:**
```bash
# Postavi Guest 1, šifra 123456, ističe 15.01.2026 u 16:30
curl "http://192.168.1.100/sysctrl.cgi?CMD=SET_PASSWORD&ID=10&TYPE=GUEST&GUEST_ID=1&PASSWORD=123456&EXPIRY=1630150126"
```

---

#### B. Maid Lozinka (Sobarica)

**Parametri:**
- `ID` = Device ID (1-254)
- `TYPE` = "MAID"
- `PASSWORD` = Numerička šifra

**Primjer:**
```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=SET_PASSWORD&ID=10&TYPE=MAID&PASSWORD=555555"
```

---

#### C. Manager Lozinka

**Parametri:**
- `ID` = Device ID (1-254)
- `TYPE` = "MANAGER"
- `PASSWORD` = Numerička šifra

**Primjer:**
```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=SET_PASSWORD&ID=10&TYPE=MANAGER&PASSWORD=999999"
```

---

#### D. Service Lozinka

**Parametri:**
- `ID` = Device ID (1-254)
- `TYPE` = "SERVICE"
- `PASSWORD` = Numerička šifra

**Primjer:**
```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=SET_PASSWORD&ID=10&TYPE=SERVICE&PASSWORD=777777"
```

---

#### E. Brisanje Guest Lozinke

**Parametri:**
- `ID` = Device ID (1-254)
- `TYPE` = "DELETE_GUEST"
- `GUEST_ID` = Broj gosta za brisanje (1-8)

**Primjer:**
```bash
# Obriši Guest 1 lozinku
curl "http://192.168.1.100/sysctrl.cgi?CMD=SET_PASSWORD&ID=10&TYPE=DELETE_GUEST&GUEST_ID=1&PASSWORD=0"
```

---

## 2. GET_PASSWORD - Čitanje Lozinki

### Endpoint
```
GET /sysctrl.cgi?CMD=GET_PASSWORD&ID=<id>&TYPE=<type>[&GUEST_ID=<1-8>]
```

### Čitanje prema tipu

#### A. Guest Lozinka

**Parametri:**
- `ID` = Device ID (1-254)
- `TYPE` = "GUEST"
- `GUEST_ID` = Broj gosta (1-8)

**Primjer:**
```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=GET_PASSWORD&ID=10&TYPE=GUEST&GUEST_ID=1"
```

**Odgovor:**
```
Guest ID: 1
Password: 123456
Expiry: 2026-01-15 16:30
```

---

#### B. Maid Lozinka

**Primjer:**
```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=GET_PASSWORD&ID=10&TYPE=MAID"
```

**Odgovor:**
```
Password: 555555
```

---

#### C. Manager Lozinka

**Primjer:**
```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=GET_PASSWORD&ID=10&TYPE=MANAGER"
```

---

#### D. Service Lozinka

**Primjer:**
```bash
curl "http://192.168.1.100/sysctrl.cgi?CMD=GET_PASSWORD&ID=10&TYPE=SERVICE"
```

---

## Validacija

### SET_PASSWORD
- ✅ `ID` mora biti 1-254
- ✅ `PASSWORD` mora sadržati samo cifre (0-9)
- ✅ `GUEST_ID` mora biti 1-8 (samo za Guest)
- ✅ `EXPIRY` mora biti tačno 10 karaktera (HHMMDDMMYY)
- ✅ `TYPE` mora biti: GUEST, MAID, MANAGER, SERVICE ili DELETE_GUEST

### GET_PASSWORD
- ✅ `ID` mora biti 1-254
- ✅ `GUEST_ID` mora biti 1-8 (samo za Guest)
- ✅ `TYPE` mora biti: GUEST, MAID, MANAGER ili SERVICE

---

## Greške

| HTTP Status | Greška | Razlog |
|-------------|--------|--------|
| 400 | Missing ID, TYPE or PASSWORD | Nedostaju obavezni parametri |
| 400 | Invalid ID (must be 1-254) | ID van dozvoljenog opsega |
| 400 | Password must contain only digits | Lozinka sadrži ne-numeričke karaktere |
| 400 | Invalid GUEST_ID (must be 1-8) | Guest ID van opsega |
| 400 | Invalid EXPIRY format | EXPIRY format nije HHMMDDMMYY |
| 400 | Invalid TYPE | Nepoznat tip korisnika |
| 200 | TIMEOUT | IC kontroler nije odgovorio |
| 200 | Password read FAILED (NAK) | IC kontroler odbio zahtjev |

---

## RS485 Protokol Detalji

### SET_PASSWORD Format
```
Bajt[0] = 0xDF          // SET_PASSWORD komanda
Bajt[1] = ID            // Device ID (1-254)
Bajt[2+] = DATA_STRING  // ASCII string sa parametrima
```

**DATA_STRING primjeri:**
- Guest: `G1,123456,1630150126`
- Maid: `H555555`
- Manager: `M999999`
- Service: `S777777`
- Delete Guest: `G1X`

---

### GET_PASSWORD Format
```
Bajt[0] = 0xE1              // GET_PASSWORD komanda
Bajt[1] = ID                // Device ID (1-254)
Bajt[2] = USER_GROUP        // 'G', 'H', 'M' ili 'S'
Bajt[3] = GUEST_ID          // Samo za 'G' (1-8)
```

**Odgovor:**
```
Bajt[0] = 0xE1              // GET_PASSWORD komanda
Bajt[1] = ACK/NAK           // 0x06 = ACK, 0x15 = NAK
Bajt[2+] = PASSWORD_DATA    // 8 bajtova za Guest, 3 bajta za ostale
```

---

## Sigurnosne Napomene

⚠️ **UPOZORENJE:** Šifre se prenose kao plain text preko HTTP-a!

**Za produkcijsko okruženje preporučuje se:**
1. ✅ Korištenje **HTTPS-a** umjesto HTTP-a
2. ✅ Dodatna **enkripcija payload-a**
3. ✅ **Autentifikacija** API zahtjeva (API key, JWT token)
4. ✅ **IP whitelisting** za pristup API-ju
5. ✅ **Rate limiting** za sprječavanje brute force napada

---

## Testiranje

### Test Scenario 1: Postaviti Guest lozinku
```bash
# 1. Postavi Guest 1, šifra 111111, ističe sutra u 12:00
curl "http://192.168.1.100/sysctrl.cgi?CMD=SET_PASSWORD&ID=1&TYPE=GUEST&GUEST_ID=1&PASSWORD=111111&EXPIRY=1200160126"

# 2. Pročitaj Guest 1 lozinku
curl "http://192.168.1.100/sysctrl.cgi?CMD=GET_PASSWORD&ID=1&TYPE=GUEST&GUEST_ID=1"

# 3. Obriši Guest 1 lozinku
curl "http://192.168.1.100/sysctrl.cgi?CMD=SET_PASSWORD&ID=1&TYPE=DELETE_GUEST&GUEST_ID=1&PASSWORD=0"
```

### Test Scenario 2: Postaviti Staff lozinke
```bash
# Maid
curl "http://192.168.1.100/sysctrl.cgi?CMD=SET_PASSWORD&ID=1&TYPE=MAID&PASSWORD=222222"
curl "http://192.168.1.100/sysctrl.cgi?CMD=GET_PASSWORD&ID=1&TYPE=MAID"

# Manager
curl "http://192.168.1.100/sysctrl.cgi?CMD=SET_PASSWORD&ID=1&TYPE=MANAGER&PASSWORD=333333"
curl "http://192.168.1.100/sysctrl.cgi?CMD=GET_PASSWORD&ID=1&TYPE=MANAGER"

# Service
curl "http://192.168.1.100/sysctrl.cgi?CMD=SET_PASSWORD&ID=1&TYPE=SERVICE&PASSWORD=444444"
curl "http://192.168.1.100/sysctrl.cgi?CMD=GET_PASSWORD&ID=1&TYPE=SERVICE"
```

---

## Changelog

### 2026-01-15
- ✅ Refaktorizovane ESP32 lokalne komande na opseg 0x50-0x68
- ✅ Riješena kolizija hex koda 0xE1 (GET_PASSWORD vs SET_SSID_PSWRD)
- ✅ Implementirana potpuna SET_PASSWORD funkcionalnost
- ✅ Implementirana potpuna GET_PASSWORD funkcionalnost
- ✅ Dodana validacija svih parametara
- ✅ Dodano parsiranje odgovora sa IC kontrolera
- ✅ Podrška za sve tipove korisnika: Guest, Maid, Manager, Service

---

**Kraj dokumentacije**
