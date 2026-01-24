# RS485 ADRESIRANJE - KOMPLETNA ANALIZA I PLAN REFAKTORISANJA

**Datum analize:** 15. Januar 2026  
**Projekt:** HTTP Bridge - ESP32 FW  
**Problem:** Prelazak sa 2-bajtnog na 1-bajtno adresiranje RS485 uređaja

---

## SAŽETAK PROBLEMA

U trenutnom kodu postoje **DVE** komande koje koriste **2-bajtno adresiranje** (ID može biti 1-65535), dok sve ostale komande koriste **1-bajtno adresiranje** (ID može biti 1-254).

Ovo stvara:
1. **Nekonzistentnost** u protokolu
2. **Probleme sa kompatibilnošću** kada IC kontroleri podržavaju samo 1-bajtnu adresu
3. **Zbunjujuću dokumentaciju i validaciju**

---

## DETEKTOVANE KOMANDE SA 2-BAJTNIM ADRESIRANJEM

### 1. CMD_SET_PASSWORD (0xDF)

**Lokacija:** [main.cpp#L988-L999](main.cpp#L988-L999)

**Trenutna struktura poruke:**
```
Bajt[0] = 0xDF              // Komanda
Bajt[1] = ID >> 8           // MSB - Visoki bajt adrese (0-255)
Bajt[2] = ID & 0xFF         // LSB - Niski bajt adrese (0-255)
Bajt[3..N] = PASSWORD       // ASCII string lozinke
```

**Validacija:**
- Trenutno nema validacije gornje granice
- ID može biti 1-65535 (2 bajta)

**HTTP zahtjev:**
```
GET /sysctrl.cgi?CMD=SET_PASSWORD&ID=<id>&PASSWORD=<password>
```

---

### 2. CMD_OPEN_DOOR (0xDB) - OPEN_DOOR alias za HOTEL_SET_PIN_V2

**Lokacija:** [main.cpp#L1098-L1123](main.cpp#L1098-L1123)

**Trenutna struktura poruke:**
```
Bajt[0] = 0xDB              // Komanda (HOTEL_SET_PIN_V2)
Bajt[1] = ID >> 8           // MSB - Visoki bajt adrese
Bajt[2] = ID & 0xFF         // LSB - Niski bajt adrese
Bajt[3] = 'C'               // PORT = C (fiksno)
Bajt[4] = 8                 // PIN = 8 (fiksno)
Bajt[5] = 1                 // VALUE = 1 (fiksno)
```

**Validacija:**
- ID može biti 1-65535

**HTTP zahtjev:**
```
GET /sysctrl.cgi?CMD=OPEN_DOOR&ID=<id>
```

---

## KOMANDE SA 1-BAJTNIM ADRESIRANJEM (ISPRAVNO)

Svi ostali RS485 zahtjevi koriste **1-bajtno adresiranje**:

### Grupa 1: Temperature Control Commands

| Komanda | Hex | Struktura | Validacija ID |
|---------|-----|-----------|---------------|
| `CMD_SET_ROOM_TEMP` | 0xD6 | [CMD][ID][VALUE] | 1-254 ✅ |
| `CMD_SET_GUEST_IN_TEMP` | 0xD0 | [CMD][ID][VALUE] | 1-254 ✅ |
| `CMD_SET_GUEST_OUT_TEMP` | 0xD1 | [CMD][ID][VALUE] | 1-254 ✅ |
| `CMD_GET_ROOM_TEMP` | 0xAC | [CMD][ID] | 1-254 ✅ |
| `CMD_GET_GUEST_IN_TEMP` | 0xD7 | [CMD][ID] | 1-254 ✅ |
| `CMD_GET_GUEST_OUT_TEMP` | 0xD8 | [CMD][ID] | 1-254 ✅ |

### Grupa 2: Thermostat Control

| Komanda | Hex | Struktura | Validacija ID |
|---------|-----|-----------|---------------|
| `CMD_SET_THST_OFF` | 0xDE | [CMD][ID] | Nema validacije ⚠️ |
| `CMD_SET_THST_ON` | 0xB4 | [CMD][ID] | Nema validacije ⚠️ |
| `CMD_SET_THST_HEATING` | 0xDC | [CMD][ID] | Nema validacije ⚠️ |
| `CMD_SET_THST_COOLING` | 0xDD | [CMD][ID] | Nema validacije ⚠️ |
| `CMD_GET_FAN_DIFFERENCE` | 0xB5 | [CMD][ID] | Nema validacije ⚠️ |
| `CMD_GET_FAN_BAND` | 0xB6 | [CMD][ID] | Nema validacije ⚠️ |

### Grupa 3: Pin Control

| Komanda | Hex | Struktura | Validacija ID |
|---------|-----|-----------|---------------|
| `CMD_SET_PIN` | 0xB1 | [CMD][ID][254][PIN][VALUE] | 1-254 ✅ |
| `CMD_GET_PINS` | 0xB1 | [CMD][ID][0xB2] | 1-254 ✅ |

### Grupa 4: System Control

| Komanda | Hex | Struktura | Validacija ID |
|---------|-----|-----------|---------------|
| `CMD_RESTART_CTRL` | 0xA3 | [CMD][ID] | 1-254 ✅ |

### Grupa 5: Broadcast Commands

| Komanda | Hex | Struktura | Adresa |
|---------|-----|-----------|--------|
| `SET_RTC_DATE_TIME` | 0xD5 | [CMD][255][...time data] | Broadcast (255) ✅ |

**Napomena:** RTC komanda koristi broadcast adresu `DEF_TFBRA = 255` i ne prima ID parametar.

---

## PLAN REFAKTORISANJA

### Faza 1: Dodaj validaciju za komande bez validacije

**Ciljne komande:**
- `CMD_SET_THST_OFF`
- `CMD_SET_THST_ON`
- `CMD_SET_THST_HEATING`
- `CMD_SET_THST_COOLING`
- `CMD_GET_FAN_DIFFERENCE`
- `CMD_GET_FAN_BAND`

**Izmjena:** Dodati validaciju `if (id < 1 || id > 254)` kao što imaju ostale komande.

---

### Faza 2: Konvertuj CMD_SET_PASSWORD na 1-bajtno adresiranje

**Prije:**
```cpp
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
        buf[1] = id >> 8;      // ❌ UKLONITI
        buf[2] = id & 0xFF;    // ❌ UKLONITI
        
        const char *pw = request->getParam("PASSWORD")->value().c_str();
        strlcpy((char *)buf + 3, pw, sizeof(buf) - 3);
        length = 3 + strlen(pw) + 1;
    }
    break;
}
```

**Poslije:**
```cpp
case CMD_SET_PASSWORD:
{
    if (!request->hasParam("ID") || !request->hasParam("PASSWORD"))
    {
        request->send(400, "text/plain", "Missing ID or PASSWORD");
        return;
    }
    
    int id = request->getParam("ID")->value().toInt();
    
    // ✅ Validacija 1-bajtne adrese
    if (id < 1 || id > 254)
    {
        request->send(400, "text/plain", "Invalid ID (must be 1-254)");
        return;
    }
    
    buf[0] = CMD_SET_PASSWORD;
    buf[1] = id;  // ✅ Direktna 1-bajtna adresa
    
    const char *pw = request->getParam("PASSWORD")->value().c_str();
    strlcpy((char *)buf + 2, pw, sizeof(buf) - 2);  // ✅ Offset promijenjen sa 3 na 2
    length = 2 + strlen(pw) + 1;  // ✅ Length promijenjen sa 3 na 2
    break;
}
```

**Nova struktura poruke:**
```
Bajt[0] = 0xDF          // Komanda
Bajt[1] = ID            // Adresa uređaja (1-254)
Bajt[2..N] = PASSWORD   // ASCII string lozinke + null terminator
```

---

### Faza 3: Konvertuj CMD_OPEN_DOOR na 1-bajtno adresiranje

**Prije:**
```cpp
case CMD_OPEN_DOOR:
{
    if (!request->hasParam("ID"))
    {
        request->send(400, "text/plain", "Missing ID");
        return;
    }

    int id = request->getParam("ID")->value().toInt();

    // ✅ Validacija ID-a (2-bajtna adresa podržava 1-65535)
    if (id < 1 || id > 65535)
    {
        request->send(400, "text/plain", "Invalid ID (must be 1-65535)");
        return;
    }

    buf[0] = cmd;          // 0xDB (OPEN_DOOR)
    buf[1] = id >> 8;      // MSB adrese (visoki bajt)  ❌ UKLONITI
    buf[2] = id & 0xFF;    // LSB adrese (niski bajt)   ❌ UKLONITI
    buf[3] = 'C';          // PORT C (fiksno)
    buf[4] = 8;            // PIN 8 (fiksno)
    buf[5] = 1;            // VALUE HIGH (fiksno)
    length = 6;
    break;
}
```

**Poslije:**
```cpp
case CMD_OPEN_DOOR:
{
    if (!request->hasParam("ID"))
    {
        request->send(400, "text/plain", "Missing ID");
        return;
    }

    int id = request->getParam("ID")->value().toInt();

    // ✅ Validacija 1-bajtne adrese
    if (id < 1 || id > 254)
    {
        request->send(400, "text/plain", "Invalid ID (must be 1-254)");
        return;
    }

    buf[0] = cmd;       // 0xDB (OPEN_DOOR/HOTEL_SET_PIN_V2)
    buf[1] = id;        // ✅ Direktna 1-bajtna adresa
    buf[2] = 'C';       // PORT C (fiksno)
    buf[3] = 8;         // PIN 8 (fiksno)
    buf[4] = 1;         // VALUE HIGH (fiksno)
    length = 5;         // ✅ Length smanjen sa 6 na 5
    break;
}
```

**Nova struktura poruke:**
```
Bajt[0] = 0xDB          // Komanda
Bajt[1] = ID            // Adresa uređaja (1-254)
Bajt[2] = 'C'           // PORT C
Bajt[3] = 8             // PIN 8
Bajt[4] = 1             // VALUE HIGH
```

---

## BACKWARD COMPATIBILITY

### ⚠️ BREAKING CHANGE

Ove izmjene su **breaking changes** i zahtijevaju:

1. **Ažuriranje IC firmware-a** da prihvati nove formate poruka
2. **Ažuriranje HTTP API dokumentacije**
3. **Testing** sa stvarnim IC kontrolerima

### Kompatibilnost sa starim kontrolerima

Ako treba zadržati privremenu kompatibilnost:
- Možeš dodati **NOVI parametar** u HTTP zahtjev (npr. `ADDR_MODE=1` ili `ADDR_MODE=2`)
- Na osnovu toga birati format poruke
- Ovo omogućava postepeni prelazak

---

## CHECKLIST ZA IMPLEMENTACIJU

- [ ] **Faza 1:** Dodaj validaciju za termostat komande bez validacije
- [ ] **Faza 2:** Refaktoriši `CMD_SET_PASSWORD` na 1-bajt
- [ ] **Faza 3:** Refaktoriši `CMD_OPEN_DOOR` na 1-bajt
- [ ] **Testiranje:** Testiraj svaku komandu sa IC kontrolerom
- [ ] **Dokumentacija:** Ažuriraj `HOTEL_SET_PIN_V2_implementation.md`
- [ ] **Dokumentacija:** Ažuriraj `README_HOTEL_SET_PIN_V2.md`
- [ ] **API dokumentacija:** Ažuriraj HTTP API primjere

---

## REZIME IZMJENA PO KOMANDAMA

| Komanda | Trenutni format | Novi format | Status |
|---------|----------------|-------------|--------|
| `CMD_SET_PASSWORD` | [0xDF][ID_H][ID_L][PW...] | [0xDF][ID][PW...] | ❌ Treba izmjena |
| `CMD_OPEN_DOOR` | [0xDB][ID_H][ID_L][C][8][1] | [0xDB][ID][C][8][1] | ❌ Treba izmjena |
| Sve ostale komande | 1-bajtno adresiranje | 1-bajtno adresiranje | ✅ OK |

---

## FINALNA NAPOMENA

Nakon ovih izmjena, **SVE** komande će koristiti **1-bajtno adresiranje (ID 1-254)**, što omogućava:
- ✅ Konzistentnost protokola
- ✅ Jednostavniju validaciju
- ✅ Lakše održavanje koda
- ✅ Kompatibilnost sa standardnim RS485 adresiranjem

---

**Kraj analize**
