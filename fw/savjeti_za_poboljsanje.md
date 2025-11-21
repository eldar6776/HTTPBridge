# Analiza Koda i Prijedlozi za Poboljšanje

Ovaj dokument pruža konstruktivne povratne informacije i prijedloge za poboljšanje firmvera `HTTPBridge/fw`. Analiza je fokusirana na sigurnost, robusnost, kvalitet koda i logičke greške.

## 1. Sigurnost (Security)

Najveći prostor za napredak leži u području sigurnosti. Trenutni dizajn izlaže uređaj značajnim rizicima.

*   **Nedostatak Autentifikacije:** HTTP server ne zahtijeva nikakvu vrstu autentifikacije. Bilo koji korisnik na lokalnoj mreži može slati komande, mijenjati postavke (uključujući WiFi kredencijale), restartovati uređaj i kontrolisati sve povezane releje i druge kontrolere na RS485 sabirnici.
    *   **Prijedlog:** Implementirati barem osnovnu HTTP autentifikaciju (Basic Auth). Biblioteka `ESPAsyncWebServer` to podržava. Može se dodati jednostavna provjera korisničkog imena i lozinke za sve ili samo za kritične endpoint-e (npr. one koji mijenjaju postavke).

*   **Nezaštićen OTA Update:** Endpoint `/update` za OTA (Over-the-Air) firmware update je potpuno otvoren. Zlonamjerni akter može bez ikakvih prepreka uploadati vlastiti, potencijalno štetan, firmware na uređaj.
    *   **Prijedlog:** Zaštititi OTA endpoint. Najjednostavnije rješenje je dodati autentifikaciju i na ovaj endpoint. Za dodatnu sigurnost, može se koristiti i `Update.onProgress` callback da se prikaže proces ažuriranja, a na kraju provjeriti hash firmvera.

## 2. Robusnost i Obrada Grešaka

*   **Blokirajuće Operacije:** Čekanje na odgovor od `TinyFrame` komande u funkciji `handleSysctrlRequest` je implementirano pomoću `do-while` petlje sa `delay(1)`.
    ```cpp
    do {
      --rdy;
      delay(1);
    } while (rdy > 0);
    ```
    Ovo je blokirajuća operacija koja zaustavlja izvršavanje ostatka koda i može trajati nekoliko stotina milisekundi. U asinhronom okruženju kakvo `ESPAsyncWebServer` podstiče, ovo je loša praksa. To također može uzrokovati probleme sa watchdog timerom ako je bio omogućen.
    *   **Prijedlog:** Refaktorisati logiku čekanja odgovora da bude neblokirajuća. Umjesto petlje, mogla bi se koristiti varijabla stanja (state variable) koja se provjerava u glavnoj `loop()` funkciji. Kada odgovor stigne (što se detektuje u `ID_Listener`), stanje se mijenja i HTTP odgovor se šalje klijentu.

*   **Nedovoljna Validacija Ulaza:** Iako postoji određena validacija za parametre HTTP zahtjeva, ona nije potpuna. Funkcije poput `request->getParam("...").toInt()` će vratiti 0 ako parametar ne postoji ili nije validan broj, što može dovesti do neočekivanog ponašanja (npr. postavljanje ID-a na 0).
    *   **Prijedlog:** Prije poziva `.toInt()`, uvijek provjeriti da li parametar postoji pomoću `request->hasParam(...)`. Dodatno, proširiti validaciju na sve ulazne vrijednosti.

## 3. Kvalitet Koda i Održivost

*   **Refaktorisanje `handleSysctrlRequest`:** Ova funkcija je izuzetno duga i sadrži ogroman `switch` blok, što je čini teškom za čitanje i održavanje.
    *   **Prijedlog:** Refaktorisati funkciju. Svaki `case` iz `switch` bloka može postati zasebna, manja funkcija (npr. `handleRestart()`, `handleSetSsid()`, `handleSetPin()`). Glavna funkcija bi onda samo pozivala odgovarajuću pod-funkciju, što bi znatno poboljšalo čitljivost.

*   **Upravljanje Memorijom (`String` vs `char[]`):** Kod koristi mješavinu `String` objekata i C-style stringova (`char` nizova). Pretjerano korištenje `String` objekata na ESP32 može dovesti do fragmentacije memorije i potencijalne nestabilnosti sistema tokom dužeg rada.
    *   **Prijedlog:** Gdje god je moguće, favorizovati korištenje `char` nizova i funkcija poput `snprintf` i `strlcpy` umjesto `String` objekata, posebno za operacije sastavljanja odgovora.

*   **Redundantna `isDST` Funkcija:** Funkcija `isDST` ručno implementira logiku za provjeru ljetnog računanja vremena. Međutim, `configTzTime` je već pozvan sa ispravnim TZ stringom (`"CET-1CEST,M3.5.0/2,M10.5.0/3"`), što znači da standardne C time funkcije (`localtime()`) automatski obračunavaju DST.
    *   **Prijedlog:** Ukloniti `isDST` funkciju i osloniti se isključivo na sistemsko upravljanje vremenom. Umjesto `gmtime()` i ručnog dodavanja ofseta u funkciji `updateLightState`, treba koristiti `localtime()` koji direktno daje lokalno vrijeme sa uračunatim DST-om. Ovo pojednostavljuje kod i čini ga otpornijim na buduće promjene DST pravila.

*   **Globalne Varijable:** Kod intenzivno koristi globalne varijable, što otežava praćenje stanja aplikacije.
    *   **Prijedlog:** Grupisati srodne varijable u `struct` ili `class` (npr. `struct AppState` ili `struct ThermostatState`). To bi učinilo kod organizovanijim i lakšim za praćenje.

## 4. Logičke Greške

*   **Greška u `CMD_PINGWDG_OFF`:** Prilikom isključivanja "Ping Watchdoga", u `Preferences` se upisuje pogrešna vrijednost.
    ```cpp
    case CMD_PINGWDG_OFF:
    {
      pingWatchdogEnabled = false;
      preferences.begin("_pingwdg", false);
      preferences.putBool("pingwdg", true); // <-- OVDJE JE GREŠKA
      preferences.end();
      // ...
      break;
    }
    ```
    *   **Ispravak:** Vrijednost koja se upisuje treba biti `false` ili direktno `pingWatchdogEnabled`.
      ```cpp
      preferences.putBool("pingwdg", false);
      ```

*   **Greška u `sendRtcToBus`:** Provjera validnosti vremena sadrži grešku u dodjeli.
    ```cpp
    void sendRtcToBus()
    {
      if (timeValid = false) // <-- OVDJE JE GREŠKA
        return;
      //...
    }
    ```
    Umjesto poređenja (`==`), koristi se dodjela (`=`), što znači da će `timeValid` uvijek postati `false`, a uslov će biti ispunjen, te će funkcija uvijek odmah izaći.
    *   **Ispravak:**
      ```cpp
      if (timeValid == false) // Koristiti '==' za poređenje
      ```
      ili jednostavnije:
      ```cpp
      if (!timeValid)
      ```
