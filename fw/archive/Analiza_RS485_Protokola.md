# Analiza RS485 Protokola (IC Uređaj)

Ovaj dokument sadrži analizu komunikacionog protokola na osnovu izvornog koda `rs485.c` i pripadajućih zaglavlja. Protokol je zasnovan na `TinyFrame` biblioteci.

## Osnovne Informacije
- **Komunikacija:** RS485
- **Biblioteka:** TinyFrame
- **Baud Rate:** Definisano u konfiguraciji (Default: 115200)

## Lista Podržanih Komandi

Sledeće komande su identifikovane u `rs485.c` (funkcija `GEN_Listener`) i `common.h`.

| Hex Kod | Simbol (Define) | Opis |
| :--- | :--- | :--- |
| **0xB1** | `PINS` | Komanda za upravljanje grupama pinova (zahteva potkomandu). |
| **0xFE** (254) | `SET_PIN` | Postavljanje stanja pojedinačnog pina (Direct). |
| **0xDB** | `HOTEL_SET_PIN_V2` | Napredno postavljanje pina (npr. otvaranje vrata sa logovanjem). |
| **0xDC** | `HOTEL_GET_PIN` | Čitanje stanja pojedinačnog pina. |
| **0xCE** | `HOTEL_READ_LOG` | Čitanje sistemskog dnevnika (Log). |
| **0xCF** | `HOTEL_DELETE_LOG` | Brisanje sistemskog dnevnika. |
| **0xDF** | `SET_PASSWORD` | Postavljanje korisničkih lozinki (Guest, Maid, Manager). |
| **0xE1** | `GET_PASSWORD` | Čitanje korisničkih lozinki. |
| **0xC0** | `RESTART_CTRL` | Restartovanje kontrolera. |
| **0xCB** | `THERMOSTAT_CHANGE_ALL` | Ažuriranje parametara termostata. |

---

## Detaljan Opis: Dodavanje i Čitanje Pinova (GPIO)

Postoje dva načina za upravljanje pinovima: preko grupne komande `PINS` ili direktnim komandama.

### 1. Dodavanje / Postavljanje Pinova (`SET_PIN`)

Ova komanda se koristi za postavljanje stanja izlaza (npr. releja, LED-a).

#### Metoda A: Direktna komanda
*Koristi se za brzo postavljanje stanja.*

- **Komanda (CMD):** `0xFE` (`SET_PIN`)
- **Struktura podataka (Payload):**
  - Byte 0: `SET_PIN` (0xFE)
  - Byte 1: `DeviceID_MSB`
  - Byte 2: `DeviceID_LSB`
  - Byte 3: **Pin ID** (Indeks pina)
  - Byte 4: **Pin Value** (Vrednost/Stanje: 0 ili 1)

#### Metoda B: Preko `PINS` komande
*Alternativni način, verovatno za grupne operacije.*

- **Komanda (CMD):** `0xB1` (`PINS`)
- **Struktura podataka (Payload):**
  - Byte 0: `PINS` (0xB1)
  - Byte 1: `DeviceID_MSB`
  - Byte 2: `DeviceID_LSB`
  - Byte 3: `SET_PIN` (Potkomanda: 0xFE)
  - Byte 4: **Pin ID**
  - Byte 5: **Pin Value**

#### Metoda C: `HOTEL_SET_PIN_V2` (Napredno)
*Ova komanda ima specijalnu logiku za otvaranje vrata (Door Open).*

- **Komanda (CMD):** `0xDB` (`HOTEL_SET_PIN_V2`)
- **Logika:**
  - Ako je `Pin ID == 'C'` (ASCII 67), `Value == 8` i `Extra != 0`, a osoba nije već ušla:
    - Triggeruje `PersonEnter(GUEST_ENTER_PASSWORD)`
    - Postavlja `DISPDoorOpenSet()`
    - Aktivira `BUZZ_DOOR_BELL`
  - U suprotnom, poziva standardnu `SetPinv2`.

---

### 2. Čitanje Pinova (`READ_PINS` / `GET_PIN`)

#### Metoda A: Čitanje svih pinova (`READ_PINS`)
*Vraća bajt koji reprezentuje stanje grupe pinova.*

- **Slanje:**
  - Komanda: `0xB1` (`PINS`)
  - Potkomanda: `0xB2` (`READ_PINS`)
- **Odgovor:**
  - `[PINS, READ_PINS, Vrednost_Bajt]`

#### Metoda B: Čitanje pojedinačnog pina (`HOTEL_GET_PIN`)
*Vraća stanje specifičnog pina.*

- **Slanje:**
  - Komanda: `0xDC` (`HOTEL_GET_PIN`)
  - Payload: `[Pin ID, Parametar]`
- **Odgovor:**
  - `[HOTEL_GET_PIN, Stanje_Pina]`

---

## Upravljanje Lozinkama (Access PINs)

Ova sekcija opisuje tačan format za dodavanje, čitanje i brisanje korisničkih šifri.
**Napomena:** Sve komande zahtevaju validan `Paired Device ID` u zaglavlju (Bajt 1 i 2) kako bi ih kontroler prihvatio.

### 1. Dodavanje i Ažuriranje Lozinki (`SET_PASSWORD`)

Komanda se koristi za postavljanje nove lozinke ili ažuriranje postojeće. Payload je formatiran kao ASCII tekstualni string.

- **Komanda (CMD):** `0xDF` (223)
- **Format Paketa:**
  `[CMD] [DevID_H] [DevID_L] [DATA_STRING...]`

#### A. Guest (Gost) Lozinke
Gosti imaju ID (1-8), lozinku i vreme isteka.

*   **Format Stringa:** `G{ID},{PASSWORD},{EXPIRY}`
    *   `G`: Oznaka za gosta.
    *   `{ID}`: Broj od 1 do 8.
    *   `,`: Separator.
    *   `{PASSWORD}`: Numerička lozinka (npr. 123456).
    *   `,`: Separator.
    *   `{EXPIRY}`: Vreme isteka u formatu `HHMMDDMMYY` (10 hex karaktera).
        *   HH: Sati, MM: Minuti, DD: Dan, MM: Mesec, YY: Godina.

*   **Primer (Postavi Guest 1, Pass 123456, Ističe 16:30 15.01.26):**
    *   String: `G1,123456,1030150126`
    *   Bajtovi (Payload počevši od bajta 3): `0x47 0x31 0x2C 0x31 0x32 0x33 0x34 0x35 0x36 0x2C 0x31 0x30 0x33 0x30 0x31 0x35 0x30 0x31 0x32 0x36`

#### B. Ostali Korisnici (Maid, Manager, Service)
Ovi korisnici imaju jednu fiksnu lozinku po grupi.

*   **Format Stringa:** `{TAG}{PASSWORD}`
    *   `{TAG}`:
        *   `H`: Maid (Sobarica)
        *   `M`: Manager
        *   `S`: Service
    *   `{PASSWORD}`: Numerička lozinka.

*   **Primer (Postavi Manager Pass 999999):**
    *   String: `M999999`

---

### 2. Brisanje Lozinki

Brisanje se vrši slanjem specifičnog stringa unutar `SET_PASSWORD` komande.

#### A. Brisanje Guest Lozinke
*   **Format Stringa:** `G{ID}X`
*   **Primer (Obriši Guest 1):** `G1X`

#### B. Brisanje Ostalih
Nije eksplicitno podržano brisanje komandom 'X', ali se mogu prebrisati novom vrednošću (npr. "000000").

---

### 3. Čitanje Lozinki (`GET_PASSWORD`)

**UPOZORENJE (Analiza Koda):** Uočen je potencijalni konflikt u firmware-u.
Funkcija za čitanje očekuje parametre na pozicijama `Data[1]` i `Data[2]`, dok glavni filter (`GEN_Listener`) zahteva da te iste pozicije sadrže `DeviceID`.
Ovo znači da bi čitanje radilo ispravno, `Paired Device ID` kontrolera bi morao privremeno odgovarati traženim parametrima (npr. 0x47 za 'G'), što je nestandardno ponašanje.

Pretpostavljeni (standardni) format, ako se ignoriše konflikt sa DeviceID filterom:

- **Komanda (CMD):** `0xAC` (172)
- **Format:**
  `[CMD] [USER_GROUP] [ID]`

*   `[USER_GROUP]`: 'G', 'H', 'M', ili 'S' (ASCII vrednost).
*   `[ID]`: Samo za Guest ('G'), broj 1-8. Za ostale se ignoriše ili šalje 0.

**Odgovor Kontrolera:**
`[CMD] [ACK/NAK] [PASSWORD_DATA...]`
*   Za Guest: 8 bajtova podataka.
*   Za ostale: 3 bajta podataka.
