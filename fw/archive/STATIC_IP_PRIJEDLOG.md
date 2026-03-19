# Prijedlog: HTTP komanda za statičku IP adresu

**Fajl:** `fw/src/main.cpp`  
**Datum analize:** 10. mart 2026.

---

## 1. Analiza postojećeg koda

### 1.1 WiFi konekcija – `tryConnectWiFi()`

Funkcija radi ovako:

1. Čita `ssid` i `password` iz NVS namespace-a `"wifi"`.
2. Ako postoji SSID, poziva `WiFi.begin(_ssid, _pass)` i čeka 10 sekundi.
3. Ako konekcija ne uspije → pokreće **WiFiManager** portal (blokira do `configPortalTimeout(60)`).
4. Kada WiFiManager uspješno spoji → sačuva novi SSID/password u `"wifi"` namespace.

**Problem:** Nigdje se ne poziva `WiFi.config()`, dakle uvijek se koristi DHCP. Nema načina da se fikisra IP adresa.

### 1.2 Komande (enum `CommandType`)

ESP32 lokalne komande su u opsegu `0x50–0x72`. Zadnja dodana je:
```
CMD_SET_IR = 0x72
```
Sljedeći slobodni opcode-ovi su `0x73` i `0x74`.

### 1.3 Pohrana postavki (NVS Preferences)

Sve postavke se čuvaju u NVS (Non-Volatile Storage) putem `Preferences` objekata. Primjeri namespace-ova:

| Namespace     | Sadržaj                          |
|---------------|----------------------------------|
| `"wifi"`      | ssid, password                   |
| `"_mdns"`     | mdns (naziv)                     |
| `"_port"`     | port                             |
| `"thermo"`    | setpoint, mode, treshold, ...    |
| `"_pingwdg"`  | pingwdg (bool)                   |
| `"sos_event"` | active, timestamp                |
| `"ir_settings"` | protocol                       |

**Plan:** dodat ćemo novi namespace `"static_ip"` s ključevima `ip`, `subnet`, `gateway`.

### 1.4 Komanda `CMD_GET_IP_ADDRESS` (0x56)

Trenutna implementacija vraća samo DHCP-dobivenu IP adresu:
```cpp
case CMD_GET_IP_ADDRESS:
{
  JsonDocument data;
  data["ip"] = WiFi.localIP().toString();
  data["subnet"] = WiFi.subnetMask().toString();
  data["gateway"] = WiFi.gatewayIP().toString();
  sendJsonSuccess(request, "IP address retrieved", &data);
  return;
}
```
Ovu komandu ćemo proširiti da vraća i konfigurisanu statičku adresu (ako postoji).

### 1.5 WiFiManager portal

U slučaju neuspjele konekcije, kod pokreće WiFiManager:
```cpp
connected = wm.autoConnect("WiFiManager");
if (connected) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", WiFi.SSID());
  preferences.putString("password", WiFi.psk());
  preferences.end();
}
```
Nakon što WiFiManager uspješno spoji, treba **obrisati statičku IP adresu**.

---

## 2. Zahtjevi implementacije

| # | Zahtjev |
|---|---------|
| R1 | Nova HTTP komanda `SET_STATIC_IP` prima IP adresu, subnet i gateway, i čuva ih u NVS |
| R2 | Ako je parametar izostavljen (ili `null`), `SET_STATIC_IP` **briše** statičku adresu i aktivira DHCP |
| R3 | Nova HTTP komanda `GET_STATIC_IP` vraća konfiguriranu statičku adresu (ili info da je DHCP) |
| R4 | Statička IP adresa se primjenjuje **uvijek** pri konekciji (`WiFi.config()` prije `WiFi.begin()`), bez obzira na stanje mreže |
| R5 | Pokretanje WiFiManager portala **briše** statičku IP adresu iz NVS-a |
| R6 | Komanda `GET_IP_ADDRESS` se proširuje da vraća i statičku adresu |
| R7 | Držanje **BOOT dugmeta 5s** (ručni WiFi reset u `loop()`) **također briše** statičku IP iz NVS-a |
| R8 | `SET_STATIC_IP` prihvata `IP=null` i `IP=0.0.0.0` kao ekvivalent izostavljenog parametra (brisanje) |

---

## 3. Plan promjena u kodu

### 3.1 Novi opcode-ovi

```cpp
// U enum CommandType, iza CMD_SET_IR = 0x72:
CMD_SET_STATIC_IP = 0x73,  // Postavi ili obriši statičku IP adresu
CMD_GET_STATIC_IP = 0x74   // Dohvati konfiguriranu statičku IP adresu
```

### 3.2 Registracija u `stringToCommand()`

```cpp
if (cmd == "SET_STATIC_IP")
  return CMD_SET_STATIC_IP;
if (cmd == "GET_STATIC_IP")
  return CMD_GET_STATIC_IP;
```

### 3.3 Pomoćna funkcija `applyStaticIPIfConfigured()`

```cpp
/**
 * Učitava statičku IP konfiguraciju iz NVS-a i primjenjuje je na WiFi.
 * Ako statička IP nije konfigurirana, ne radi ništa (DHCP ostaje).
 * Mora se pozivati NEPOSREDNO PRIJE WiFi.begin().
 * Vraća true ako je statička IP primijenjena.
 */
bool applyStaticIPIfConfigured() {
  preferences.begin("static_ip", true); // read-only
  String ip_str      = preferences.getString("ip",      "");
  String subnet_str  = preferences.getString("subnet",  "");
  String gateway_str = preferences.getString("gateway", "");
  preferences.end();

  if (ip_str.length() == 0) {
    return false; // Nije konfigurirana
  }

  IPAddress ip, subnet, gateway;
  if (!ip.fromString(ip_str) || !subnet.fromString(subnet_str) || !gateway.fromString(gateway_str)) {
    LOG_ERROR_LN("[Static IP] Greška: nevalidna pohranjena IP konfiguracija!");
    return false;
  }

  WiFi.config(ip, gateway, subnet);
  LOG_INFO("[Static IP] Primijenjena: %s / %s via %s\n",
           ip_str.c_str(), subnet_str.c_str(), gateway_str.c_str());
  return true;
}
```

> **Zašto `WiFi.config(ip, gateway, subnet)` a ne `WiFi.config(ip, subnet, gateway)` ?**
> U Arduino ESP32 biblioteci redosljed je: `WiFi.config(local_ip, gateway, subnet [, dns1, dns2])`.
> Pazi na redosljed parametara!

### 3.4 Izmjena `tryConnectWiFi()`

Ključna promjena: **dodati poziv `applyStaticIPIfConfigured()` neposredno prije svakog `WiFi.begin()`**:

```cpp
void tryConnectWiFi()
{
  preferences.begin("wifi", false);
  preferences.getString("ssid", _ssid, sizeof(_ssid));
  preferences.getString("password", _pass, sizeof(_pass));
  preferences.end();

  WiFiManager wm;
  wm.setConfigPortalTimeout(60);

  bool connected = false;

  if (strlen(_ssid) > 0)
  {
    // ★ NOVO: Primijeni statičku IP adresu AKO je konfigurirana,
    //         prije svakog pokušaja konekcije na mrežu.
    //         WiFi.config() postavi statičku konfiguraciju koja ostaje aktivna
    //         do restarta — ne može je zaobići DHCP, čak ni ako AP ne odgovori.
    applyStaticIPIfConfigured();

    if (strlen(_pass) > 0)
      WiFi.begin(_ssid, _pass);
    else
      WiFi.begin(_ssid);

    // ... (ostatak ostaje isti — timeout petlja, connected flag)

    if (WiFi.status() != WL_CONNECTED) {
      // Resetuj statičku konfiguraciju prije fallback-a na WiFiManager,
      // jer WiFiManager mora raditi normalno (ne smije koristiti staru statičku IP)
      WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }
  }

  if (!connected)
  {
    LOG_INFO_LN("📶 Starting WiFiManager portal...");
    esp_task_wdt_delete(NULL);
    connected = wm.autoConnect("WiFiManager");
    esp_task_wdt_add(NULL);

    if (connected)
    {
      LOG_INFO_LN("✅ Connected with portal!");

      // Sačuvaj SSID/password
      preferences.begin("wifi", false);
      preferences.putString("ssid", WiFi.SSID());
      preferences.putString("password", WiFi.psk());
      preferences.end();

      // ★ NOVO: WiFiManager portal = briši statičku IP adresu iz NVS-a
      preferences.begin("static_ip", false);
      preferences.clear();
      preferences.end();
      LOG_INFO_LN("[Static IP] Obrisana — WiFiManager portal je koristен.");
    }
    else
    {
      LOG_ERROR_LN("❌ Not connected. Restart...");
      delay(3000);
      ESP.restart();
    }
  }
}
```

> **Napomena o `WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE)`:**
> Ovaj poziv "poništava" prethodnu `WiFi.config()` konfiguraciju i vraća ESP32 na DHCP mode.
> Koristi se samo u slučaju NEUSPJELE direktne konekcije, da WiFiManager može normalno funkcionisati.
> **Ako konekcija uspije s statičkom IP-om, nikad se ne poziva** — ESP32 zadržava zabilježenu IP adresu.

### 3.5 Handler za `CMD_SET_STATIC_IP`

```cpp
case CMD_SET_STATIC_IP:
{
  isLocalCommand = true;

  // Provjeri ima li IP parametar — ako nema, BRIŠI statičku IP (vrati DHCP)
  // Prihvatamo i eksplicitne brisanje-signale: IP=null, IP=0.0.0.0, IP= (prazan)
  bool deleteRequest = !request->hasParam("IP");
  if (!deleteRequest) {
    String ipVal = request->getParam("IP")->value();
    if (ipVal == "null" || ipVal == "0.0.0.0" || ipVal.length() == 0) {
      deleteRequest = true;
    }
  }

  if (deleteRequest) {
    preferences.begin("static_ip", false);
    preferences.clear();
    preferences.end();

    JsonDocument data;
    data["static_ip"] = nullptr;
    data["mode"] = "DHCP";
    sendJsonSuccess(request, "Static IP removed. DHCP will be used after restart.", &data);
    return;
  }

  String ip_str      = request->getParam("IP")->value();
  String subnet_str  = request->hasParam("SUBNET")  ? request->getParam("SUBNET")->value()  : "255.255.255.0";
  String gateway_str = request->hasParam("GATEWAY") ? request->getParam("GATEWAY")->value() : "";

  // Validacija IP adresa
  IPAddress ip, subnet, gateway;
  if (!ip.fromString(ip_str)) {
    sendJsonError(request, 400, "Invalid IP address format");
    return;
  }
  if (!subnet.fromString(subnet_str)) {
    sendJsonError(request, 400, "Invalid subnet mask format");
    return;
  }

  // Gateway je opcionalan — ako nije zadan, koristimo .1 iz iste subnet-e
  if (gateway_str.length() == 0) {
    gateway = IPAddress(ip[0], ip[1], ip[2], 1);
    gateway_str = gateway.toString();
  } else if (!gateway.fromString(gateway_str)) {
    sendJsonError(request, 400, "Invalid gateway address format");
    return;
  }

  // Sačuvaj u NVS
  preferences.begin("static_ip", false);
  preferences.putString("ip",      ip_str);
  preferences.putString("subnet",  subnet_str);
  preferences.putString("gateway", gateway_str);
  preferences.end();

  JsonDocument data;
  data["ip"]      = ip_str;
  data["subnet"]  = subnet_str;
  data["gateway"] = gateway_str;
  data["mode"]    = "STATIC";
  data["note"]    = "Restart required to apply.";
  sendJsonSuccess(request, "Static IP saved. Restart device to apply.", &data);
  return;
}
```

### 3.6 Handler za `CMD_GET_STATIC_IP`

```cpp
case CMD_GET_STATIC_IP:
{
  isLocalCommand = true;

  preferences.begin("static_ip", true);
  String ip_str      = preferences.getString("ip",      "");
  String subnet_str  = preferences.getString("subnet",  "");
  String gateway_str = preferences.getString("gateway", "");
  preferences.end();

  JsonDocument data;
  if (ip_str.length() > 0) {
    data["mode"]    = "STATIC";
    data["ip"]      = ip_str;
    data["subnet"]  = subnet_str;
    data["gateway"] = gateway_str;
  } else {
    data["mode"] = "DHCP";
    data["ip"]   = nullptr;
  }
  // Uvijek dodaj i trenutnu (active) IP adresu
  data["current_ip"]      = WiFi.localIP().toString();
  data["current_subnet"]  = WiFi.subnetMask().toString();
  data["current_gateway"] = WiFi.gatewayIP().toString();

  sendJsonSuccess(request, "Static IP configuration retrieved", &data);
  return;
}
```

### 3.7 Proširenje `CMD_GET_IP_ADDRESS`

```cpp
case CMD_GET_IP_ADDRESS:
{
  preferences.begin("static_ip", true);
  String static_ip_str = preferences.getString("ip", "");
  preferences.end();

  JsonDocument data;
  data["ip"]      = WiFi.localIP().toString();
  data["subnet"]  = WiFi.subnetMask().toString();
  data["gateway"] = WiFi.gatewayIP().toString();
  data["mode"]    = static_ip_str.length() > 0 ? "STATIC" : "DHCP";
  if (static_ip_str.length() > 0) {
    data["static_ip"] = static_ip_str;
  }
  sendJsonSuccess(request, "IP address retrieved", &data);
  return;
}
```

### 3.8 Proširenje `CMD_GET_STATUS`

U JSON odgovoru `CMD_GET_STATUS`, unutar `doc["wifi"]` bloka, dodati:

```cpp
// -- u CMD_GET_STATUS handler-u, iza doc["wifi"]["rssi"] = WiFi.RSSI(); --

preferences.begin("static_ip", true);
String static_ip_cfg = preferences.getString("ip", "");
preferences.end();
doc["wifi"]["ip_mode"] = static_ip_cfg.length() > 0 ? "STATIC" : "DHCP";
if (static_ip_cfg.length() > 0) {
  doc["wifi"]["static_ip"] = static_ip_cfg;
}
```

### 3.9 Izmjena BOOT_PIN handlera u `loop()`

Postojeći kod u `loop()` (oko linije 4193) pri držanju BOOT dugmeta 5s radi ovo:
```cpp
wm.resetSettings();    // briše WiFiManager-ove NVS podatke
WiFi.disconnect(true); // prekida vezu i briše iz WiFi drivera
delay(1000);
wm.startConfigPortal("WiFiManager");
```

Treba dodati brisanje statičke IP **prije** pokretanja portala, i snimanje novih WiFi kredencijala **nakon** što portal uspješno spoji:

```cpp
esp_task_wdt_delete(NULL);
wm.resetSettings();
WiFi.disconnect(true);

// ★ NOVO: Obriši statičku IP adresu — korisnik resetuje mrežu,
//         novi start treba biti čisti DHCP.
preferences.begin("static_ip", false);
preferences.clear();
preferences.end();
LOG_INFO_LN("[Static IP] Obrisana — BOOT_PIN portal.");

delay(1000);
wm.startConfigPortal("WiFiManager");
esp_task_wdt_add(NULL);

// ★ BONUS FIX (preporučeno): Sačuvaj nove kredencijale u naš "wifi" namespace.
// Bez ovoga, na sljedećem restartu tryConnectWiFi() pokušava sa STARIM podacima,
// ne uspije, pada u autoConnect() koji uspije s WiFiManager-ovim internim NVS-om,
// i tek tada spasi u naš namespace — efekt je 2-restart problem.
if (WiFi.isConnected()) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", WiFi.SSID());
  preferences.putString("password", WiFi.psk());
  preferences.end();
  LOG_INFO_LN("[BOOT Portal] Novi WiFi kredencijali sačuvani.");
}
```

> **Napomena:** 2-restart problem postoji i u trenutnom kodu (bez statičke IP) ali nije bio kritičan. Ovaj fix ga rješava usput.

---

## 4. HTTP API tablice

### `SET_STATIC_IP` – Postavi ili obriši statičku IP adresu

```
GET /sysctrl.cgi?CMD=SET_STATIC_IP&IP=192.168.1.100&SUBNET=255.255.255.0&GATEWAY=192.168.1.1
```

| Parametar | Obavezno | Opis |
|-----------|----------|------|
| `IP`      | Ne* | IPv4 adresa. Ako **izostavljen** → briše statičku adresu i aktivira DHCP |
| `SUBNET`  | Ne | Subnet maska. Default: `255.255.255.0` |
| `GATEWAY` | Ne | Gateway adresa. Default: `<IP>.1` (automatski) |

**Postavi statičku IP:**
```
?CMD=SET_STATIC_IP&IP=192.168.1.150&SUBNET=255.255.255.0&GATEWAY=192.168.1.1
```
```json
{
  "status": "success",
  "message": "Static IP saved. Restart device to apply.",
  "data": {
    "ip": "192.168.1.150",
    "subnet": "255.255.255.0",
    "gateway": "192.168.1.1",
    "mode": "STATIC",
    "note": "Restart required to apply."
  }
}
```

**Ukloni statičku IP (vrati DHCP):**
```
?CMD=SET_STATIC_IP
```
```json
{
  "status": "success",
  "message": "Static IP removed. DHCP will be used after restart.",
  "data": {
    "static_ip": null,
    "mode": "DHCP"
  }
}
```

---

### `GET_STATIC_IP` – Dohvati konfiguriranu statičku IP adresu

```
GET /sysctrl.cgi?CMD=GET_STATIC_IP
```

**Odgovor (STATIC mode — sačuvano, restart još nije urađen):**

> Najkorisniji scenarij: `ip` ≠ `current_ip` jer je nova statička IP sačuvana u NVS-u ali WiFi još uvijek
> koristi staru DHCP adresu. Ovo je jedini način da znaš je li statička IP već aktivna ili čeka restart.

```json
{
  "status": "success",
  "data": {
    "mode": "STATIC",
    "ip": "192.168.1.150",
    "subnet": "255.255.255.0",
    "gateway": "192.168.1.1",
    "current_ip": "192.168.1.87",
    "current_subnet": "255.255.255.0",
    "current_gateway": "192.168.1.1"
  }
}
```

**Odgovor (STATIC mode — nakon restarta, adresa aktivna):**

> `ip` == `current_ip`: statička IP je primijenjena, restart je već urađen.

```json
{
  "status": "success",
  "data": {
    "mode": "STATIC",
    "ip": "192.168.1.150",
    "subnet": "255.255.255.0",
    "gateway": "192.168.1.1",
    "current_ip": "192.168.1.150",
    "current_subnet": "255.255.255.0",
    "current_gateway": "192.168.1.1"
  }
}
```

**Odgovor (DHCP mode):**
```json
{
  "status": "success",
  "data": {
    "mode": "DHCP",
    "ip": null,
    "current_ip": "192.168.1.200",
    "current_subnet": "255.255.255.0",
    "current_gateway": "192.168.1.1"
  }
}
```

---

## 5. Opcode tablica (ažurirana)

| Opcode | Komanda | Opis |
|--------|---------|------|
| `0x50` | `CMD_GET_SSID_PSWRD` | |
| `0x51` | `CMD_SET_SSID_PSWRD` | |
| `0x52` | `CMD_GET_MDNS_NAME` | |
| `0x53` | `CMD_SET_MDNS_NAME` | |
| `0x54` | `CMD_GET_TCPIP_PORT` | |
| `0x55` | `CMD_SET_TCPIP_PORT` | |
| `0x56` | `CMD_GET_IP_ADDRESS` | Prošireno: vraća i info o statičkoj IP |
| `...`  | `...` | |
| `0x72` | `CMD_SET_IR` | Zadnja postojeća |
| **`0x73`** | **`CMD_SET_STATIC_IP`** | **NOVO** |
| **`0x74`** | **`CMD_GET_STATIC_IP`** | **NOVO** |

---

## 6. Edge case-ovi i napomene

### 6.1 Restart je potreban

`WiFi.config()` se primjenjuje samo u `tryConnectWiFi()`, koja se poziva samo u `setup()`. Dakle, nakon `SET_STATIC_IP`, device se mora restartovati da bi nova adresa bila aktivna.  
Poruka u odgovoru to jasno naznačava (`"note": "Restart required to apply."`).

**Opcija:** Odmah pozvati `ESP.restart()` u handleru (korisnik bira ovo naknadno).

### 6.2 WiFi.config() i DHCP – detalji ponašanja

Na ESP32 Arduino platformi:
- `WiFi.config(ip, gateway, subnet)` → isključuje DHCP klijent, koristi zadane adrese
- `WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE)` → "resetuje" konfiguraciju i dozvoljava DHCP
- Jednom postavljena `WiFi.config()` ostaje aktivna za sve buduće `WiFi.begin()` pozive **u istoj sesiji**
- Nakon `ESP.restart()`, konfig se **ne pamti** automatski — zato se čuva u NVS i svaki put primjenjuje iz NVS-a u `setup()`

### 6.3 Zašto `WiFi.config()` garantuje "zaobilaženje mrežnih problema"

- Standardno, DHCP klijent šalje broadcast i čeka DHCP server da odgovori
- Ako DHCP server ne odgovori (mrežni problem, restart AP, itd.) → konekcija pada
- Sa `WiFi.config()`, ESP32 **ne koristi DHCP** — odmah konfigurira zadanu adresu bez ikakve mrežne komunikacije
- Konekcija na WiFi AP može i dalje uspjeti (ili ne), ali IP adresa se ne mijenja

### 6.4 WiFiManager fallback

Redosljed u `tryConnectWiFi()`:
1. Primijeni statičku IP (`applyStaticIPIfConfigured()`)
2. Pokušaj direktnu konekciju
3. **AKO direktna konekcija ne uspije** → resetuj `WiFi.config()` na DHCP (`INADDR_NONE`) → pokreni WiFiManager
4. Ako WiFiManager uspije → **briši** statičku IP iz NVS-a
5. Ako WiFiManager ne uspije → restart

Ovaj redosljed osigurava da:
- Statička IP uvijek vrijedi za direktne konekcije
- WiFiManager uvijek radi s DHCP (bez statičke IP), jer mora primiti IP da bi portal bio dostupan
- Nakon uspješnog WiFiManager-a, statička IP se briše (novo okruženje = nove postavke)

**Zašto ne koristimo `wm.setSTAStaticIPConfig()`?**

Drugi prijedlog (`prijedlog_static_ip.md`) predlaže poziv `wm.setSTAStaticIPConfig(ip, gateway, subnet)` pri fallback-u na portal. To bi zadržalo statičku IP i unutar samog portala. Međutim, ovo je **pogrešno** za naš use-case jer:
- Korisnik ulazi u portal jer direktna konekcija nije uspjela — znači ili se mreža promijenila ili ima problema
- Zadržavanje statičke IP u novoj mreži moglo bi uzrokovati konflikt ili nemogućnost konekcije
- Naš zahtjev (R5) eksplicitno kaže: portal = brisanje statičke IP
- `wm.setSTAStaticIPConfig()` bi negiralo R5

Zato koristimo `WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE)` i brišemo iz NVS umjesto `setSTAStaticIPConfig()`.

### 6.5 Validacija IP adrese

Arduino `IPAddress::fromString()` radi korektno za standardne IPv4 adrese u formatu `x.x.x.x`. Nema potrebe za dodatnom validacijom — ako format nije ispravan, `fromString()` vraća `false`.

### 6.6 Prihvatanje `null` i `0.0.0.0` kao brisanje

HTTP klijenti (Python, JS) ponekad šalju `null` kao string ili `0.0.0.0` kada žele poništiti postavku. Da ne bi pravilo grešku (`Invalid IP address format`), handler eksplicitno provjerava ove vrijednosti i tretira ih kao signal za brisanje statičke IP — ekvivalentno izostavljenom parametru `IP`.

---

## 7. Sažetak promjena – popis fajlova

Sve promjene su samo u `fw/src/main.cpp`:

| Dio koda | Vrsta promjene |
|----------|----------------|
| `enum CommandType` | Dodati `CMD_SET_STATIC_IP = 0x73`, `CMD_GET_STATIC_IP = 0x74` |
| `stringToCommand()` | Dodati 2 nova if-bloka |
| (nova funkcija) `applyStaticIPIfConfigured()` | Kompletno nova funkcija |
| `tryConnectWiFi()` | Dodati pozive `applyStaticIPIfConfigured()`, reset na `INADDR_NONE` pri fallback-u, brisanje NVS-a na portal success |
| `loop()` – BOOT_PIN handler | Dodati brisanje `"static_ip"` NVS-a + snimanje novih WiFi kredencijala |
| `handleSysctrlRequest()` – `CMD_GET_IP_ADDRESS` | Proširiti odgovor s info o statičkoj IP |
| `handleSysctrlRequest()` – `CMD_SET_STATIC_IP` | Novi `case` blok (prihvata i `null`/`0.0.0.0`) |
| `handleSysctrlRequest()` – `CMD_GET_STATIC_IP` | Novi `case` blok |
| `handleSysctrlRequest()` – `CMD_GET_STATUS` | Dodati `ip_mode` i `static_ip` u `doc["wifi"]` |

---

*Ovaj dokument opisuje plan implementacije. Implementacija traje ~15 minuta i ne zahtijeva promjene u platformio.ini ni u header datotekama. Odobri plan i implementacija kreće.*
