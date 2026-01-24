# Analiza RS485 Komandi za projekat IC (REVIDIRANO)

## Status Analize

**UPOZORENJE:** Detektovan je kritičan logički propust u `IC/Src/rs485.c` u funkciji `GEN_Listener`.
Veliki broj komandi je implementiran u `else if` blokovima, ali su njihovi uslovi za ulazak ("guard condition") **zakomentarisani**.

## 1. Komande koje se MOGU izvršiti (100% Executable)

Samo sledeće komande prolaze ulazni filter i mogu se aktivirati:

| Hex | Makro | Funkcija | Napomena |
| :---: | :--- | :--- | :--- |
| `0xDE` | **`SET_THST_OFF`** | `SetThstOff()` | Isključuje termostat. |
| `0xCE` | **`HOTEL_READ_LOG`** | `ReadLog(msg)` | Čita logove. |
| `0xCF` | **`HOTEL_DELETE_LOG`** | `DeleteLog(msg)` | Briše logove. |
| `0xCB` | **`THERMOSTAT_CHANGE_ALL`** | *(Prazno)* | Implementacija je prazna. |
| `0xDF` | **`SET_PASSWORD`** | `SetPassword(msg)` | Postavlja lozinku. |
| `0xB1` | **`PINS`** | `SendPins(msg)` | Čita ili setuje pinove. |
| `0xFE` | **`SET_PIN`** | `RS_SetPin(msg)` | Direktno setovanje pina. |
| `0xDB` | **`HOTEL_SET_PIN_V2`** | `RS_SetPinv2(msg)` | Setovanje pina v2 (Detaljna analiza ispod). |
| `0xDC` | **`HOTEL_GET_PIN`** | `RS_GetPin(msg)` | Čita pin. |
| `0xD5` | **`SET_RTC_DATE_TIME`** | `HAL_RTC_SetTime` | **Samo kao Broadcast/Group poruka.** |

## 2. Detaljna Analiza: `HOTEL_SET_PIN_V2` (0xDB)

Ova komanda ima dvojaku funkciju zavisno od prosleđenih argumenata.

**Logika izvršenja:**

```c
if((data[3] == 'C') && (data[4] == 8) && data[5] && (!PersonHasEntered()))
    PersonEnter(GUEST_ENTER_PASSWORD);
else
    SetPinv2(data[3], data[4], data[5]);
```

### Slučaj A: "Guest Entry" Mod (Specijalna funkcija)

Ovaj mod se aktivira **samo** ako su ispunjeni svi sledeći uslovi:

1. **Port (Arg1):** `'C'` (ASCII 67).
2. **Pin (Arg2):** `8`.
3. **Vrednost (Arg3):** Veća od 0 (True).
4. **Uslov Stanja:** `PersonHasEntered()` vraća `0` (False).

**Šta radi `PersonHasEntered()`?**

* Proverava stanje pina `PC8` (GPIOC, Pin 8).
* Ako je pin već visoko (`SET`), funkcija vraća `1`.
* **Zaključak:** Ako je "Guest Entry" već aktivan (PC8 je High), komanda **neće** ponovo okinuti logiku ulaska, već će preći na "Slučaj B". Ovo sprečava višestruko okidanje istog događaja dok je timer aktivan.

**Šta radi `PersonEnter(GUEST_ENTER_PASSWORD)`?**

1. Postavlja `persontEnter_AccessType` na `GUEST_ENTER_PASSWORD`.
2. **Pali pin PC8** (`GuestEnteredDoorLockPinOn`). Ovo automatski čini da naredni pozivi `PersonHasEntered()` vraćaju `True`.
3. Startuje tajmer za zaključavanje vrata (`personEnter_TimerDoorLock_Start`).
4. Postavlja zastavicu `personSendEntranceSignal = 1` koja će u sledećem ciklusu `RS485_Service` poslati poruku `GUEST_ENTER_PASSWORD_RS485` nazad na mrežu.

### Slučaj B: Standardno Setovanje Pina

Izvršava se u svim ostalim slučajevima (drugi port/pin, ili ako je `PersonHasEntered()` true).

* Poziva `SetPinv2(port, pin, value)`.
* Direktno menja stanje hardverskog pina mapiranjem `'A'-'K'` na GPIO portove i broja pina na `GPIO_PIN_x`.

### Zaključak o ponašanju (Trigger mehanizam)

Komanda funkcioniše kao **ciklični okidač**:

1. **Prvo slanje:** Ako su vrata zaključana, ona se otključavaju, tajmer kreće, i šalje se mrežni signal.
2. **Slanje tokom otključanog stanja:** Ako se komanda pošalje ponovo dok tajmer još traje (vrata su još uvek otvorena), sistem je ignoriše (ne resetuje tajmer i ne šalje ponovo signal) kako bi se sprečilo spamovanje mreže.
3. **Ponovni ciklus:** Čim tajmer istekne i vrata se automatski zaključaju, komanda je ponovo spremna da pokrene **potpuno isti ciklus** otključavanja.

Dakle, komanda će raditi "svaki put" kada su vrata u zatvorenom stanju, omogućavajući neograničen broj uzastopnih otvaranja vrata nakon svakog zaključavanja.

## 3. Komande koje se NE MOGU izvršiti (Dead Code)

Sledeće komande postoje u kodu, ali su **zakomentarisane u `if` uslovu**.
Kontroler će ove poruke **ignorisati** kao da nisu namenjene njemu.

* `SET_THST_ON` (`0xB4`)
* `GET_ROOM_TEMP` (`0xAC`)
* `SET_ROOM_TEMP` (`0xD6`)
* `SET_THST_HEATING` (`0xDC`) (Takođe konflikt sa `HOTEL_GET_PIN`)
* `SET_THST_COOLING` (`0xDD`)
* `GET_FAN_DIFFERENCE` (`0xB5`)
* `GET_FAN_BAND` (`0xB6`)
* `SELECT_RELAY` (`0xAD`)
* `RET` (`0xB7`)
* `RET_TO_PRIMARY` (`0xBD`)
* `GET_SP_MAX` (`0xB8`)
* `SET_SP_MAX` (`0xB9`)
* `GET_SP_MIN` (`0xBA`)
* `SET_SP_MIN` (`0xBB`)

## 4. Zaključak

Kod sadrži ozbiljan bug gde je funkcionalnost termostata (paljenje, setovanje temperature, modovi) **onemogućena** jer su provere komandi ostale zakomentarisane u glavnom filteru poruka. Jedina termostat funkcija koja radi je `SET_THST_OFF`. Komanda `HOTEL_SET_PIN_V2` ima zaštitu od višestrukog okidanja ("debounce" na nivou logike ulaska).
