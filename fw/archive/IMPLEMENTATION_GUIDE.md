# Vodič za Implementaciju i Korištenje Firmware Update Sistema

Ovaj dokument detaljno opisuje HTTP API za kontrolu ažuriranja firmvera na udaljenim uređajima putem RS485 mreže.

## 1. Pregled Sistema Slotova i Adresiranja

Sistem koristi 8 slotova (0-7) na eksternoj memoriji ESP32. Svaki slot ima specifična pravila ponašanja prilikom slanja na udaljeni uređaj.

| Slot ID | Namjena | Default Adresa (STM32) | Override Adrese? | Opis |
| :---: | :--- | :--- | :---: | :--- |
| **0, 1, 2** | **Firmware** | `0x90F00000` | ❌ **NE** | Uvijek se piše na `RT_NEW_FILE_ADDR`. Parametar `staging_addr` se ignoriše. |
| **3** | **Firmware (Flex)** | `0x90F00000` | ✅ **DA** | Default je `RT_NEW_FILE_ADDR`, ali se može promijeniti slanjem `staging_addr`. |
| **4, 5, 6** | **Resursi** | `0x90000000` | ❌ **NE** | Uvijek se piše na `EXT_FLASH_ADDR`. Parametar `staging_addr` se ignoriše. |
| **7** | **Resursi (Flex)** | `0x90000000` | ✅ **DA** | Default je `EXT_FLASH_ADDR`, ali se može promijeniti slanjem `staging_addr`. |

---

## 2. API Komande (Detaljno)

Sve komande se šalju na port **8020**.

### A. Upload Fajla na ESP32 (`/upload`)

Prije ažuriranja, fajl mora biti postavljen u odgovarajući slot na ESP32.

**Komanda:**
```bash
# Upload fajla 'IC.BIN' u Slot 0
curl -X POST "http://soba504.local:8020/upload?slot=0" -F "file=@IC.BIN"
```

**Očekivani Odgovor (JSON):**
```json
{
  "slots": [
    {
      "id": 0,
      "type": "firmware",
      "size": 54320,
      "version": 33685505,
      "crc32": 2864434397,
      "valid": true
    },
    ...
  ]
}
```

---

### 2. Upload Fajla na ESP32
Učitava binarni fajl (`.bin`) sa vašeg računara na ESP32.

- **Metoda:** `POST`
- **Endpoint:** `/upload`
- **Parametri:**
  - `slot` (Query Parametar): Broj slota (0-7). Obavezno navesti u URL-u.
  - `file` (Body): Sadržaj fajla (Multipart Form Data).

**Primjer upotrebe (cURL):**
```bash
# Upload fajla 'firmware.bin' na Slot 0
curl -X POST "http://soba504.local:8020/upload?slot=0" -F "file=@firmware.bin"
```

**Odgovor:**
```json
{
    "status": "success",
    "message": "Upload Complete"
}
```

---

### 3. Pokretanje Ažuriranja (Update Uređaja)
Šalje fajl iz lokalnog ESP32 slota prema ciljanom uređaju na RS485 mreži.

- **Metoda:** `POST`
- **Endpoint:** `/start_update`
- **Parametri:**
  - `slot`: Broj izvornog slota na ESP32 (npr. `0`).
  - `addr`: RS485 adresa ciljanog uređaja (npr. `5`).
  - `staging_addr`: (Opcionalno) Adresa u memoriji STM32 (Default: `0x90000000`).

**Primjer upotrebe (cURL) - PREPORUČENO:**
Najpouzdaniji način je slanje parametara direktno u URL-u kako bi se izbjegli problemi sa enkodiranjem body-a na Windows-u:
```bash
# Pokreni update za uređaj na adresi 5 koristeći fajl iz Slota 0
curl -X POST "http://soba504.local:8020/start_update?slot=0&addr=5"
```

**Alternativa (x-www-form-urlencoded):**
```bash
curl -X POST http://soba504.local:8020/start_update -d "slot=0&addr=5"
```

**Odgovor:**
```json
{
    "status": "success",
    "message": "Update Started"
}
```

---

### 4. Status Procesa
Provjerava status trenutnog update procesa.

- **Metoda:** `GET`
- **Endpoint:** `/update_status`

**Primjer upotrebe:**
```bash
curl http://soba504.local:8020/update_status
```

**Očekivani Odgovor:**
```json
{
  "active": true,    // true ako je proces u toku
  "state": 3,        // Trenutno stanje (vidi tabelu ispod)
  "progress": 45     // Procenat završenosti (0-100%)
}
```

**Tabela Stanja (State Codes):**
- `0`: IDLE (Spreman)
- `1`: STARTING (Inicijalizacija)
- `2`: WAIT_START_ACK (Čeka potvrdu brisanja flash-a)
- `3`: SENDING_DATA (Slanje podataka)
- `4`: WAIT_DATA_ACK (Čeka potvrdu paketa)
- `5`: FINISHING (Verifikacija)
- `6`: WAIT_FINISH_ACK (Čeka kraj)
- `7`: SUCCESS (Uspješno)
- `8`: FAILED (Greška)

---

## ⚙️ Tehnički Detalji Protokola (TinyFrame)

Ovaj dio je relevantan za programere koji implementiraju klijentsku stranu (STM32).

**Type ID:** `0xC1` (TF_TYPE_FIRMWARE_UPDATE)

| Sub-Komanda | Bajt | Sadržaj Paketa | Opis |
| :--- | :--- | :--- | :--- |
| **START_REQUEST** | `0x01` | `[CMD][ADDR][FwInfo(20b)][StagingAddr(4b)]` | Šalje info o fajlu i adresu za upis. |
| **START_ACK** | `0x02` | `[CMD][ADDR]` | Potvrda da je klijent spreman. |
| **DATA_PACKET** | `0x10` | `[CMD][ADDR][Seq(4b)][Data(128b)]` | Paket podataka. |
| **DATA_ACK** | `0x11` | `[CMD][ADDR][Seq(4b)]` | Potvrda prijema paketa. |
| **FINISH_REQUEST** | `0x20` | `[CMD][ADDR][CRC32(4b)]` | Kraj slanja, zahtjev za provjeru. |
| **FINISH_ACK** | `0x21` | `[CMD][ADDR]` | Potvrda uspjeha. |

**Integracija na STM32:**
1. Inicijalizirati `firmware_update_agent.c`.
2. Registrovati listener za Type `0xC1`.
3. Osigurati pristup QSPI flash memoriji za `staging_addr`.

### Detaljan izvještaj o implementaciji

Evo kako sistem sada **TAČNO** radi sa novim izmjenama:

1.  **Prijem Zahtjeva (`/start_update`):**
    *   ESP32 prima `slot` i `addr` (i opcionalno `staging_addr`).
    *   **Validacija Slota:** Kod provjerava broj slota i na osnovu njega **forsira** adresu upisa:
        *   Ako je slot **0, 1, 2**: Varijabla `staging` se postavlja na **`0x90F00000`**. Bilo šta što pošaljete u `staging_addr` se ignoriše. Ovo garantuje da firmver ide na pravo mjesto.
        *   Ako je slot **4, 5, 6**: Varijabla `staging` se postavlja na **`0x90000000`**. Ovo garantuje da resursi idu na početak eksterne memorije.
        *   Ako je slot **3** ili **7**: Kod provjerava da li ste poslali `staging_addr`. Ako jeste, koristi tu vrijednost. Ako niste, koristi default (Firmware adresa za slot 3, ExtFlash adresa za slot 7).

2.  **Slanje `START_REQUEST` (TinyFrame):**
    *   ESP32 formira paket. U payload (podatke) paketa, na offsetu 18, upisuje tu izračunatu `staging` adresu (4 bajta).
    *   Paket se šalje STM32 uređaju.

3.  **Prijem na STM32 (`firmware_update_agent.c`):**
    *   STM32 prima paket.
    *   Čita adresu sa offseta 18 u varijablu `staging_qspi_addr`.
    *   Izvršava `QSPI_Erase` na toj adresi.
    *   Svi naredni `DATA_PACKET` podaci se upisuju počevši od te adrese.

Ovim je osigurano da ne možete slučajno pregaziti resurse firmverom (jer slotovi 0-2 ne daju promjenu adrese) niti firmver resursima (jer slotovi 4-6 forsiraju svoju adresu).

<!--
[PROMPT_SUGGESTION]Objasni mi kako da izračunam decimalnu vrijednost za heksadecimalnu adresu 0x90D80000 za LOGO update.[/PROMPT_SUGGESTION]
[PROMPT_SUGGESTION]Kako da provjerim da li je update uspješno završen na STM32 strani?[/PROMPT_SUGGESTION]
-->
