# HTTPBridge ESP32

HTTPBridge je firmware za ESP32 koji omogućava upravljanje sobnim kontrolerima (termostatima, rasvjetom, pinovima, relejima itd.) putem HTTP API-ja. Primarno se koristi za automatizaciju u hotelskim ili apartmanskim sistemima, gdje je potrebno centralizovano i daljinsko upravljanje sobama.

## Funkcionalnosti

- Preuzimanje i podešavanje WiFi parametara (SSID, Password)
- Mijenjanje mDNS imena i TCP/IP porta uređaja
- Upravljanje termostatima: očitavanje i podešavanje temperature, režima rada (grijanje/hladjenje), setpointa, diference, statusa
- Upravljanje sobnim pinovima i izlazima (svjetla, fancoil motor, buzzer, scene)
- Upravljanje vanjskom rasvjetom putem releja i timera
- Upravljanje pristupnim PIN-ovima za goste
- Restartovanje uređaja i sobnih kontrolera
- Preuzimanje trenutnog RTC vremena i podešavanje istog
- Ping watchdog servis za automatski restart u slučaju gubitka konekcije
- Detekcija i upravljanje slobodnim ESP32 pinovima (GPIO)
- Sve funkcije dostupne kroz HTTP GET zahteve

## Primjeri HTTP komandi

Preporučeni format komande:
```
http://<mdns_ime>.local:<port>/sysctrl.cgi?CMD=<komanda>[&parametri]
```

### Pregled najčešćih komandi

| Komanda              | Opis                                                  | Primjer |
|----------------------|-------------------------------------------------------|---------|
| GET_STATUS           | Preuzmi sve postavke uređaja                          | `/sysctrl.cgi?CMD=GET_STATUS` |
| GET_ROOM_TEMP        | Preuzmi trenutnu i zadanu temperaturu sobe            | `/sysctrl.cgi?CMD=GET_ROOM_TEMP&ID=92` |
| SET_ROOM_TEMP        | Postavi temperaturu sobe                              | `/sysctrl.cgi?CMD=SET_ROOM_TEMP&ID=92&VALUE=28` |
| SET_SSID_PSWRD       | Postavi WiFi parametre                                | `/sysctrl.cgi?CMD=SET_SSID_PSWRD&SSID=WiFi0&PSWRD=12345678` |
| GET_PINS             | Preuzmi stanje pinova kontrolera                      | `/sysctrl.cgi?CMD=GET_PINS&ID=200` |
| SET_PIN              | Postavi stanje pina                                   | `/sysctrl.cgi?CMD=SET_PIN&ID=200&PIN=1&VALUE=1` |
| GET_TIMER / SET_TIMER| Preuzmi/podesi timer rasvjete                         | `/sysctrl.cgi?CMD=SET_TIMER&TIMERON=SUNSET&TIMEROFF=SUNRISE` |
| OUTDOOR_LIGHT_ON/OFF | Ručno uključi/isključi vanjsku rasvjetu               | `/sysctrl.cgi?CMD=OUTDOOR_LIGHT_ON` |
| PINGWDG_ON/OFF       | Omogući/onemogući ping watchdog servis                | `/sysctrl.cgi?CMD=PINGWDG_ON` |
| RESTART              | Restartuj HTTPBridge ESP32 uređaj                     | `/sysctrl.cgi?CMD=RESTART` |

### Termostat komande

- TH_STATUS – status termostata (trenutna temp., mod rada, fan, pumpa)
- TH_SETPOINT – setpoint temperature
- TH_DIFF – diferenca za fan brzine
- TH_HEATING / TH_COOLING – režim grijanja/hladjenja
- TH_ON / TH_OFF – aktivacija/deaktivacija termostata
- TH_EMA – podešavanje EMA filtera za temperaturu

### GPIO komande

- ESP_GET_PINS – prikaz slobodnih pinova
- ESP_SET_PIN / ESP_RESET_PIN – postavi/resetuj stanje pina
- ESP_PULSE_PIN – pulsiraj pin sa pauzom

## Napomena

- Pristup uređaju je moguć preko mDNS imena (npr. soba501.local) ili direktno preko IP adrese.
- Sve promjene parametara (SSID, mDNS, port, pinovi itd.) se trajno upisuju u flash i važe nakon restarta uređaja.
- Detaljan opis svih komandi i odgovora nalazi se u fajlu `toplik HTTP ESP32 komande.txt`.

## Instalacija

1. Flashujte firmware na ESP32 uređaj.
2. Priključite uređaj na mrežu i konfigurirajte WiFi koristeći WiFiManager portal (pritisak na BOOT taster >5s).
3. Pristupite uređaju preko mDNS ili IP adrese putem HTTP API-ja.

## Prava upotreba

Ovaj firmware je namijenjen za hotelske, apartmanske i slične objekte gdje je potrebno centralizovano upravljanje sobama, rasvjetom, pristupom i komforom gostiju.

## Kontakt

Za podršku i pitanja, kontaktirajte autora kroz GitHub Issues.
