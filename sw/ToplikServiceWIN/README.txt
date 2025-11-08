## 🚀 Toplik Service - Uputstvo za Instalaciju i Podešavanje

Ovo je vodič za postavljanje, testiranje i pokretanje "Toplik Service" web aplikacije na novoj Windows mašini.

---

### 1. Sadržaj Foldera

Prije početka, provjerite da li vaš `ToplikService` folder sadrži sve potrebne datoteke:

* `server.py` (Glavna aplikacija)
* `config.json` (Sve postavke)
* `requirements.txt` (Popis potrebnih biblioteka)
* `instaliraj_biblioteke.bat` (Skripta za instalaciju)
* `pokreni_server.bat` (Skripta za testno pokretanje)
* `nssm-2.24.zip` (ili noviji, alat za Windows servis)
* `templates/` (Folder koji sadrži):
    * `login.html`
    * `soba.html`
    * `admin_login.html`
    * `admin.html`

---

### 2. Podešavanje (Prvi i najvažniji korak)

Prije bilo kakve instalacije, morate podesiti `config.json` datoteku.

1.  Otvorite `config.json` u text editoru (kao VS Code ili Notepad).
2.  Promijenite `"admin_password": "vasa-tajna-admin-lozinka-123"` u neku vašu stvarnu, tajnu lozinku.
3.  U sekciji `"sobe"`, prođite kroz sve PIN-ove (npr. "301", "302"...) i ažurirajte podatke za svaku sobu:
    * `"ime"`: Ime koje vidite u admin panelu.
    * `"mdns"`: Stvarna mDNS adresa vašeg ESP32 uređaja (npr. `soba301.local`).
    * `"port"`: Port uređaja (npr. `8020`).
    * `"termostat_id"`: ID kontrolera termostata.
    * `"pin_ctrl_id"`: ID kontrolera za svjetla.
4.  Spremite i zatvorite datoteku.

---

### 3. Instalacija na Novoj Mašini

Ovo se radi samo jednom po mašini.

#### Korak 1: Instalacija Pythona
1.  Idite na `https://www.python.org/downloads/`.
2.  Preuzmite najnoviju stabilnu verziju Pythona (npr. Python 3.11 ili 3.12).
3.  Pokrenite instalaciju.
4.  **NAJVAŽNIJI KORAK:** Na prvom ekranu instalacije, **obavezno štiklirajte (označite) kućicu `Add Python to PATH`**.
5.  Dovršite instalaciju.

#### Korak 2: Instalacija Biblioteka
1.  Sada kada je Python instaliran, vratite se u `ToplikService` folder.
2.  Pronađite datoteku `instaliraj_biblioteke.bat`.
3.  **Dupli-klik** na nju.
4.  Otvorit će se crni prozor i automatski instalirati sve potrebne biblioteke (Flask, Waitress, Requests, PyJWT).
5.  Kada ispiše `Sve biblioteke su uspjesno instalirane`, pritisnite bilo koju tipku da zatvorite prozor.

---

### 4. Testiranje (Ručno Pokretanje)

Prije nego što server postavimo kao stalni servis, provjerite da li radi.

1.  Pronađite datoteku `pokreni_server.bat`.
2.  **Dupli-klik** na nju.
3.  Otvorit će se crni prozor i server će se pokrenuti. Trebali biste vidjeti poruku:
    `--- Pokrećem Toplik Service (PRODUKCIJA) ---`
    `--- Server radi na http://0.0.0.0:5000 ---`
4.  Otvorite Chrome (ili drugi preglednik) i idite na `http://localhost:5000`.
5.  Pokušajte se ulogirati s jednim od PIN-ova iz `config.json`.
6.  Ako sve radi, vratite se u crni prozor i **pritisnite CTRL+C** da ugasite server.

Ako ovo ne radi, provjerite `config.json` ili da li je Port 5000 zauzet.

---

### 5. Postavljanje kao Stalni Windows Servis (NSSM)

Ovo osigurava da se server automatski pokreće s Windowsom i da se sam restartira ako se sruši.

#### Korak 1: Priprema NSSM-a
1.  Raspakirajte `nssm-2.24.zip` (desni klik -> Extract All...).
2.  Uđite u folder koji se stvorio, pa u folder `win64`.
3.  Pronađite datoteku `nssm.exe`.
4.  Iskopirajte `nssm.exe` u vaš glavni `ToplikService` folder (tamo gdje je i `server.py`), radi jednostavnosti.

#### Korak 2: Pokretanje Admin CMD-a
1.  Kliknite na Start (Windows ikona).
2.  Ukucajte `cmd`.
3.  Na "Command Prompt" koji se pojavi, kliknite **desni klik** i odaberite **"Run as administrator"**. Ovo je obavezno.

#### Korak 3: Navigacija do Foldera
1.  U crnom (Admin) prozoru, morate doći do vašeg foldera. Ukucajte `cd` (razmak) i zalijepite punu putanju do vašeg foldera.
    *Primjer:* `cd C:\Users\VašeIme\Desktop\ToplikService`
2.  Pritisnite Enter.

#### Korak 4: Instalacija Servisa
1.  Sada kada ste u pravom folderu, ukucajte sljedeću naredbu i pritisnite Enter:
    ```bash
    nssm install ToplikService
    ```
2.  Otvorit će vam se grafički (GUI) prozor za podešavanje servisa.

#### Korak 5: Podešavanje Servisa u GUI-u

Morate popuniti tri taba:

**A. Tab `Application` (Najvažniji):**
* **Path:** Kliknite na `...` gumb. Morate pronaći gdje je instaliran `python.exe`.
    * *Uobičajena putanja je:* `C:\Users\VašeIme\AppData\Local\Programs\Python\Python312\python.exe`
    * *(Ako ne znate gdje je, u drugi cmd prozor ukucajte `where python` i pokazat će vam putanju)*
* **Startup directory:** Kliknite na `...` gumb i odaberite vaš `ToplikService` folder (npr. `C:\Users\VašeIme\Desktop\ToplikService`).
    * *Ovo je ključno da bi server pronašao `server.py` i `config.json`!*
* **Arguments:** Ukucajte točno: `server.py`

**B. Tab `Details`:**
* **Display name:** `Toplik Service` (Ovo je ime koje ćete vidjeti u listi servisa).

**C. Tab `Restart`:**
* **Application exit:** U padajućem izborniku, odaberite `Restart application`.
* **Delay:** Ukucajte `5000` (ovo je 5000 ms, ili 5 sekundi. To je vrijeme koje će servis pričekati prije restarta ako se sruši).

#### Korak 6: Instalacija
1.  Kliknite na gumb **`Install service`**.
2.  Ako je sve u redu, vidjet ćete poruku "Service 'ToplikService' installed successfully!".

#### Korak 7: Pokretanje Servisa
1.  Servis je instaliran, ali još ne radi. U istom crnom (Admin) prozoru ukucajte:
    ```bash
    nssm start ToplikService
    ```
2.  Trebali biste dobiti poruku "ToplikService START".

**Gotovo!** Vaš server sada radi u pozadini. Možete zatvoriti crni prozor. On će se automatski paliti s Windowsom i sam restartati ako padne.

---

### 6. Kako Upravljati Servisom (Kasnije)

Uvijek koristite **Admin Command Prompt** i navigirajte do vašeg `ToplikService` foldera (gdje je `nssm.exe`).

* Za **zaustavljanje** servisa (npr. ako želite mijenjati `server.py`):
    `nssm stop ToplikService`

* Za **ponovno pokretanje** servisa (nakon što ste spremili izmjene):
    `nssm start ToplikService`

* Za **restart** servisa:
    `nssm restart ToplikService`

* Za **provjeru statusa**:
    `nssm status ToplikService`

* Za **brisanje** servisa (ako ga više ne trebate):
    `nssm remove ToplikService`