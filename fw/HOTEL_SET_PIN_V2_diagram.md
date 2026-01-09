# HOTEL_SET_PIN_V2 - Dijagram toka

## Arhitektura sistema

```
┌─────────────────┐
│   Korisnik      │
│  (HTTP Client)  │
└────────┬────────┘
         │ HTTP GET /sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&...
         ▼
┌─────────────────────────────────────────┐
│         ESP32 HTTP Bridge               │
│                                         │
│  ┌────────────────────────────────┐    │
│  │  handleSysctrlRequest()        │    │
│  │  - Primi HTTP parametere       │    │
│  │  - Validiraj (ID, PORT, PIN)   │    │
│  │  - Kreiraj RS485 buffer        │    │
│  └──────────┬─────────────────────┘    │
│             │                           │
│  ┌──────────▼─────────────────────┐    │
│  │  TF_QuerySimple()              │    │
│  │  - Šalje preko TinyFrame       │    │
│  └──────────┬─────────────────────┘    │
│             │                           │
│  ┌──────────▼─────────────────────┐    │
│  │  TF_WriteImpl() → Serial2      │    │
│  │  - RS485_DE_PIN = HIGH         │    │
│  │  - Serial2.write(buf)          │    │
│  │  - RS485_DE_PIN = LOW          │    │
│  └──────────┬─────────────────────┘    │
└─────────────┼─────────────────────────┘
              │ RS485 Bus
              ▼
┌─────────────────────────────────────────┐
│      IC Kontroler (STM32)               │
│                                         │
│  ┌────────────────────────────────┐    │
│  │  GEN_Listener()                │    │
│  │  - Primi RS485 poruku          │    │
│  │  - Detektuj CMD = 0xDB         │    │
│  └──────────┬─────────────────────┘    │
│             │                           │
│  ┌──────────▼─────────────────────┐    │
│  │  RS_SetPinv2(msg)              │    │
│  │  - PORT, PIN, VALUE            │    │
│  └──────────┬─────────────────────┘    │
│             │                           │
│      ┌──────┴──────┐                   │
│      │ PORT='C' && │                   │
│      │ PIN=8 &&    │                   │
│      │ VALUE=1 ?   │                   │
│      └──────┬──────┘                   │
│             │                           │
│      ┌──────▼──────┐──────────┐        │
│      │     DA      │    NE    │        │
│      │             │          │        │
│  ┌───▼──────────┐  │   ┌──────▼─────┐ │
│  │ PersonEnter()│  │   │SetPinv2()  │ │
│  │ - PC8=HIGH   │  │   │- Setuj pin │ │
│  │ - Timer ON   │  │   └────────────┘ │
│  │ - Send signal│  │                   │
│  └──────────────┘  │                   │
│                    │                   │
│  ┌─────────────────▼──────────────┐   │
│  │  TF_Respond() → RS485 Bus       │   │
│  └─────────────────┬───────────────┘   │
└────────────────────┼────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────┐
│         ESP32 HTTP Bridge               │
│                                         │
│  ┌────────────────────────────────┐    │
│  │  ID_Listener()                 │    │
│  │  - Primi odgovor               │    │
│  │  - Kopiraj u replyData[]       │    │
│  └──────────┬─────────────────────┘    │
│             │                           │
│  ┌──────────▼─────────────────────┐    │
│  │  Parse response                │    │
│  │  case CMD_HOTEL_SET_PIN_V2:    │    │
│  │    body = "Pin Set V2 OK"      │    │
│  └──────────┬─────────────────────┘    │
└─────────────┼─────────────────────────┘
              │ HTTP Response
              ▼
┌─────────────────┐
│   Korisnik      │
│  "Pin Set V2 OK"│
└─────────────────┘
```

## Detaljni tok za otvaranje vrata

```
┌─────────────────────────────────────────────────────┐
│  SPECIJALNI SLUČAJ: PORT='C', PIN=8, VALUE=1        │
└─────────────────────────────────────────────────────┘

HTTP Request:
┌─────────────────────────────────────────────────────┐
│ GET /sysctrl.cgi?CMD=HOTEL_SET_PIN_V2&ID=1&        │
│                  PORT=C&PIN=8&VALUE=1               │
└───────────────────────┬─────────────────────────────┘
                        │
                        ▼
RS485 Poruka:
┌─────────────────────────────────────────────────────┐
│ [0xDB] [0x01] [0xFE] [0x43] [0x08] [0x01]          │
│   ^      ^      ^      ^      ^      ^             │
│   │      │      │      │      │      └─ VALUE=1    │
│   │      │      │      │      └──────── PIN=8      │
│   │      │      │      └─────────────── PORT='C'   │
│   │      │      └────────────────────── Reserved   │
│   │      └───────────────────────────── ID=1       │
│   └──────────────────────────────────── CMD=0xDB   │
└───────────────────────┬─────────────────────────────┘
                        │
                        ▼
IC Kontroler - RS_SetPinv2():
┌─────────────────────────────────────────────────────┐
│  if((data[3] == 'C') &&                             │
│     (data[4] == 8) &&                               │
│     data[5] &&                                      │
│     (!PersonHasEntered()))                          │
│  {                                                  │
│      PersonEnter(GUEST_ENTER_PASSWORD);            │
│  }                                                  │
│  else                                               │
│  {                                                  │
│      SetPinv2(data[3], data[4], data[5]);          │
│  }                                                  │
└───────────────────────┬─────────────────────────────┘
                        │
                        ▼
PersonEnter(GUEST_ENTER_PASSWORD):
┌─────────────────────────────────────────────────────┐
│  1. persontEnter_AccessType = GUEST_ENTER_PASSWORD  │
│  2. GuestEnteredDoorLockPinOn() → PC8 = HIGH       │
│  3. personEnter_TimerDoorLock_Start()              │
│  4. personSendEntranceSignal = 1                   │
└───────────────────────┬─────────────────────────────┘
                        │
           ┌────────────┴────────────┐
           │                         │
           ▼                         ▼
    ┌─────────────┐          ┌─────────────────┐
    │  PC8 = HIGH │          │  Timer Started  │
    │ (Vrata open)│          │  (Auto-close)   │
    └─────────────┘          └─────────────────┘
           │                         │
           │                         │
           └────────────┬────────────┘
                        │
                        ▼
                 ┌─────────────┐
                 │ Mrežni      │
                 │ signal:     │
                 │ GUEST_ENTER │
                 │ _PASSWORD   │
                 │ _RS485      │
                 └─────────────┘
```

## State Dijagram - Vrata

```
                    ┌──────────────────┐
                    │  VRATA ZAKLJUČANA│
                    │  PersonHasEntered│
                    │  = False         │
                    │  PC8 = LOW       │
                    └────────┬─────────┘
                             │
              Komanda:       │
              PORT=C, PIN=8, │
              VALUE=1        │
                             │
                             ▼
                    ┌──────────────────┐
                    │ VRATA OTKLJUČANA │
                    │ PersonHasEntered │
                    │ = True           │
                    │ PC8 = HIGH       │
                    │ Timer aktivan    │
                    └────────┬─────────┘
                             │
              Ponovna        │  Timer
              komanda:       │  istek
              IGNORISANA  ◄──┤
                             │
                             ▼
                    ┌──────────────────┐
                    │  VRATA ZAKLJUČANA│
                    │  PersonHasEntered│
                    │  = False         │
                    │  PC8 = LOW       │
                    └──────────────────┘
                             │
                             │
                    (Ciklus se ponavlja)
```

## RS485 bajtovi - Primjeri

### Primjer 1: Otvaranje vrata (ID=1)

```
Hex: DB 01 FE 43 08 01
     │  │  │  │  │  └─ VALUE = 1 (HIGH)
     │  │  │  │  └──── PIN = 8
     │  │  │  └─────── PORT = 'C' (ASCII 67 = 0x43)
     │  │  └────────── Reserved (254 = 0xFE)
     │  └───────────── ID = 1
     └──────────────── CMD = HOTEL_SET_PIN_V2 (0xDB)
```

### Primjer 2: Setovanje GPIOA Pin 5 = HIGH (ID=10)

```
Hex: DB 0A FE 41 05 01
     │  │  │  │  │  └─ VALUE = 1 (HIGH)
     │  │  │  │  └──── PIN = 5
     │  │  │  └─────── PORT = 'A' (ASCII 65 = 0x41)
     │  │  └────────── Reserved (254 = 0xFE)
     │  └───────────── ID = 10 (0x0A)
     └──────────────── CMD = HOTEL_SET_PIN_V2 (0xDB)
```

### Primjer 3: Resetovanje GPIOB Pin 15 = LOW (ID=5)

```
Hex: DB 05 FE 42 0F 00
     │  │  │  │  │  └─ VALUE = 0 (LOW)
     │  │  │  │  └──── PIN = 15 (0x0F)
     │  │  │  └─────── PORT = 'B' (ASCII 66 = 0x42)
     │  │  └────────── Reserved (254 = 0xFE)
     │  └───────────── ID = 5
     └──────────────── CMD = HOTEL_SET_PIN_V2 (0xDB)
```

## Validacioni dijagram

```
HTTP Request
     │
     ▼
┌─────────────────────┐
│ Validiraj ID        │
│ (1-254)?            │
└──────┬──────────────┘
       │ DA
       ▼
┌─────────────────────┐
│ Validiraj PORT      │
│ (A-K)?              │
└──────┬──────────────┘
       │ DA
       ▼
┌─────────────────────┐
│ Validiraj PIN       │
│ (0-15)?             │
└──────┬──────────────┘
       │ DA
       ▼
┌─────────────────────┐
│ Validiraj VALUE     │
│ (0 ili 1)?          │
└──────┬──────────────┘
       │ DA
       ▼
┌─────────────────────┐
│ Kreiraj RS485 buf   │
│ [DB][ID][FE]...     │
└──────┬──────────────┘
       │
       ▼
┌─────────────────────┐
│ TF_QuerySimple()    │
│ Pošalji na RS485    │
└──────┬──────────────┘
       │
       ▼
┌─────────────────────┐
│ Čekaj odgovor       │
│ (Timeout 4 ticks)   │
└──────┬──────────────┘
       │
       ▼
┌─────────────────────┐
│ Parsiraj i vrati    │
│ HTTP Response       │
└─────────────────────┘

  (Ako bilo koja validacija
   ne prođe, vrati 400 error)
```

## Sekvencijalni dijagram

```
Korisnik     ESP32        IC Kontroler
   │            │               │
   │──HTTP GET─→│               │
   │            │               │
   │            │──RS485 MSG───→│
   │            │   [0xDB...]   │
   │            │               │
   │            │               │──Check PORT,PIN,VALUE
   │            │               │
   │            │               │──PersonEnter() or SetPinv2()
   │            │               │
   │            │               │──PC8 = HIGH (ako C,8,1)
   │            │               │
   │            │←──RS485 ACK───│
   │            │               │
   │←HTTP 200──│               │
   │"Pin Set OK"│               │
   │            │               │
   │            │               │──Timer tick...
   │            │               │
   │            │               │──PC8 = LOW (Auto)
```

---

## Legenda

- `┌─┐` - Komponente/Moduli
- `│ │` - Tok podataka
- `→ ←` - Pravac komunikacije
- `▼ ▲` - Tok izvršavanja
- `[0xDB]` - Heksadecimalni bajtovi
