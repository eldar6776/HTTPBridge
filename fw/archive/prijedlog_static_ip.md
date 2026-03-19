# Prijedlog implementacije fiksne IP adrese

Na osnovu detaljne analize `fw/src/main.cpp`, evo koraka i izmjena koje su potrebne kako bi se implementiralo podešavanje i pamćenje statičke IP adrese:

## 1. Nove HTTP komande (Enumi i String parsiranje)
Potrebno je dodati novu komandu za podešavanje statičke IP adrese, npr. `SET_STATIC_IP`. 
- **`CommandType` enum:** Dodati `CMD_SET_STATIC_IP = 0x6C` (ili neki drugi slobodan hex kod unutar sigurnog `0x50-0x68` / `0x69+` ESP32 lokalnog ranga).
- **`stringToCommand` funkcija:** Dodati `if (cmd == "SET_STATIC_IP") return CMD_SET_STATIC_IP;`

## 2. Snimanje adrese u NVM (Preferences)
U `handleSysctrlRequest` u `switch` bloku za `CMD_SET_STATIC_IP`:
- Očekivaćemo HTTP parametre: `IP`, `GW` (Gateway), `SN` (Subnet).
- **Logika null parametra:** Ako je `IP` parametar poslan kao `"null"`, prazan string, ili `"0.0.0.0"`, to će biti signal za brisanje statičke adrese. Tada ćemo izbrisati ključeve iz memorije i time vratiti DHCP.
- Vrijednosti će se sačuvati u `Preferences` unutar istog namespace-a `"wifi"` ili novog `"static_ip"`.

## 3. Primjena IP adrese pri pokretanju (`tryConnectWiFi()`)
Funkcija `tryConnectWiFi()` trenutno iz memorije čita samo `_ssid` i `_pass` i odmah poziva `WiFi.begin()`. 
Da bi statička adresa radila i ignorisala DHCP (čak i ako je mreža spora/kvari se), moramo primijeniti IP prije pokretanja WiFi-a:
- Učitati `IP`, `GW`, `SN` iz `Preferences`.
- Provjeriti da li su stringovi validni. Pretvoriti stringove pomoću `IPAddress().fromString()`.
- Ako statička adresa postoji, prije linije `WiFi.begin(_ssid, _pass)` pozvati:
  ```cpp
  WiFi.config(ip, gateway, subnet);
  ```
- Ukoliko nema spašenog statičkog IP-a (izbrisan komandom `"null"`), ESP32 po defaultu ostaje na DHCP-u, tako da tu ništa ne moramo dodavati osim ignorisanja `WiFi.config` poziva.
- Isto tako, ako uređaj padne u WiFiManager (`wm.autoConnect`), moramo prosljediti konfiguraciju u WiFiManager:
  ```cpp
  wm.setSTAStaticIPConfig(ip, gateway, subnet);
  ```

## 4. Brisanje prilikom reseta preko `BOOT_PIN`-a
Zahtjev kaže: *"ako pokrenem wifi manager onda treba obrisati i ovu ip adresu"*. 
U `loop()` funkciji, kada se `BOOT_PIN` drži 5 sekundi (linija koda ~1793):
```cpp
wm.resetSettings();    // obrisi stare podatke
WiFi.disconnect(true); // prekini vezu i zaboravi sve
// NOVO:
preferences.begin("wifi", false);
preferences.remove("static_ip");
preferences.remove("static_gw");
preferences.remove("static_sn");
preferences.end();
```
Ovo osigurava da dugme potpuno obriše mrežne podatke i vrati uređaj na čisti početak (DHCP).

## 5. Povratna informacija (Opcijonalno)
U komandi `CMD_GET_STATUS` bi bilo preporučljivo, ispod `doc["wifi"]["ip"]`, dodati parametar `doc["wifi"]["static"] = true/false` zavisno od toga da li je u memoriji konfigurisan fiksni IP.

---

Ovo je siguran pristup jer se minimalno dotiče postojećeg watchdog i reset koda, a statička IP adresa će biti robusna (forsira se na razini C++ API-ja prije samog uspostavljanja konekcije).
