vo je precizna instrukcija koju možeš proslijediti drugom agentu (ili AI asistentu) kako bi implementirao ovo rješenje bez greške.

System Prompt / Task Instruction za implementaciju GPIO Hold na ESP32
Cilj: Implementirati mehanizam koji održava logičko stanje na GPIO 33 (vanjska rasvjeta) tokom softverskog restarta (esp_restart) bez prekida ili treperenja (glitch-free).

Hardverski zahtjevi:

Pin: GPIO 33 (RTC-capable pin).

Logika: High (3.3V) = Upaljeno, Low (0V) = Ugašeno.

Eksterni Pull-down: Preporučen otpornik od 10kΩ na GPIO 33 prema GND kako bi se osigurao nivo 0V tokom inicijalnog paljenja (Power-on Reset).

Softverska logika (Koraci za agenta):

RTC Memorija: Deklarisati varijablu koristeći RTC_DATA_ATTR kako bi se stanje svjetla sačuvalo u RTC RAM-u koji preživljava restart.

Glavni Setup (Kritična sekvenca):

Koristiti rtc_gpio_init(GPIO_NUM_33) za inicijalizaciju pina u RTC domeni.

Postaviti smjer pina na izlaz putem rtc_gpio_set_direction.

Prije otključavanja, postaviti nivo pina (rtc_gpio_set_level) na osnovu vrijednosti sačuvane u RTC memoriji.

Tek nakon postavljanja nivoa, pozvati rtc_gpio_hold_dis(GPIO_NUM_33) da se onemogući hardverski "lock" i dozvoli softveru upravljanje.

Priprema za Restart:

Prije pozivanja esp_restart(), obavezno izvršiti rtc_gpio_hold_en(GPIO_NUM_33). Ovo "zaključava" trenutni naponski nivo na pinu (bio on High ili Low) na hardverskom nivou, čineći ga imunim na reset procesora.

Tehničke napomene:

Izbjegavati standardne pinMode i digitalWrite funkcije u ranoj fazi setup()-a jer su sporije i mogu izazvati kratkotrajan "float" (plivanje) napona. Koristiti isključivo driver/rtc_io.h biblioteku za inicijalnu tranziciju.

GPIO 33 je odabran jer nije "strapping pin" i ne utiče na boot modove ESP32 (za razliku od GPIO 12 ili 15).