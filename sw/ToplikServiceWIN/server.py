# ----------------------------------------------------------------------
#  TOPLIK SERVICE - PYTHON BACKEND SERVER (FAZA 6.3: Admin PIN Change)
# ----------------------------------------------------------------------
import os
import json
import requests  
import jwt 
from datetime import datetime, timedelta
from flask import Flask, request, jsonify, render_template, redirect, make_response, url_for
from waitress import serve
import re
import threading
import time
import logging
from collections import OrderedDict

# --- INICIJALIZACIJA APLIKACIJE ---
app = Flask(__name__)
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

# --- GLOBALNE VARIJABLE ---
CONFIG = {}
config_lock = threading.Lock()

# ----------------------------------------------------------------------
# POSTAVKE
# ----------------------------------------------------------------------
app.config['SECRET_KEY'] = 'OVDJE-STAVITE-NEKI-VAS-DUGACAK-TAJNI-KLJUC-12345'

# ----------------------------------------------------------------------
# FUNKCIJE ZA RUKOVANJE KONFIGURACIJOM
# ----------------------------------------------------------------------
def load_config():
    global CONFIG
    try:
        with open('config.json', 'r', encoding='utf-8') as f:
            original_config = json.load(f, object_pairs_hook=OrderedDict)
            for soba_id, soba_data in original_config.get('sobe', {}).items():
                soba_data['cached_ip'] = None
            
            with config_lock:
                CONFIG = original_config
            logging.info("Uspješno učitan i inicijaliziran 'config.json'.")
    except Exception as e:
        logging.critical(f"!!! GRESKA pri čitanju 'config.json': {e}")
        exit(1)

def save_config():
    """Sprema trenutni CONFIG u config.json (thread-safe)."""
    with config_lock:
        try:
            config_to_save = CONFIG.copy()
            with open('config.json', 'w', encoding='utf-8') as f:
                json.dump(config_to_save, f, indent=2, ensure_ascii=False)
            logging.info("CONFIG: Uspješno spremljen config.json.")
            return True
        except Exception as e:
            logging.error(f"CONFIG: Nije moguće upisati u config.json: {e}")
            return False

# ----------------------------------------------------------------------
#  PARSER ZA GET_STATUS (Verzija 5.4)
# ----------------------------------------------------------------------
def parse_get_status(raw_text):
    main_data = {}
    thermostat_data = {}
    pin_states = {} 
    current_section = 'main'
    KEY_MAP = {
        'TCP/IP Address': 'ip_address', 'TCP/IP Port': 'port',
        'WIFI SSID': 'wifi_ssid', 'WIFI PASSWORD': 'wifi_password',
        'Ping Watchdog': 'ping_watchdog', 'Pump/Valve': 'pump_valve',
        'EMA Filter': 'ema_filter', 'TIMERON': 'timer_on', 'TIMEROFF': 'timer_off'
    }
    try:
        lines = raw_text.strip().split('\n')
        for line in lines:
            line = line.strip()
            if not line or line.startswith('----'): continue
            if line.startswith('ESP Thermostat State:'):
                current_section = 'thermostat'
                continue
            elif line.startswith('ESP Pin States:'):
                current_section = 'pins'
                continue
            if '=' not in line: continue
            key, value = line.split('=', 1)
            key = key.strip(); value = value.strip()
            if '*C' in value: value = value.split('*C')[0].strip()
            if 'min' in value: value = value.split('min')[0].strip()
            if key in KEY_MAP:
                js_key = KEY_MAP[key]
            else:
                js_key = key.lower().replace(' ', '_').replace('/', '_')
            if current_section == 'thermostat':
                thermostat_data[js_key] = value
            elif current_section == 'pins':
                if js_key.startswith('gpio'):
                    pin_broj = js_key.replace('gpio', '')
                    pin_states[pin_broj] = value
            else:
                main_data[js_key] = value
        main_data.update(thermostat_data) 
        main_data['pins'] = pin_states 
        return main_data 
    except Exception as e:
        logging.error(f"GRESKA PARSERA: {e} na liniji: '{line}'")
        return {'error': str(e)}

# ----------------------------------------------------------------------
#  HELPER FUNKCIJE ZA IP RESOLVING I SLANJE
# ----------------------------------------------------------------------
def resolve_and_cache_ip(soba_id):
    """
    (POZADINSKA FUNKCIJA) Pokušava riješiti IP i spremiti ga u CONFIG.
    """
    with config_lock:
        if soba_id not in CONFIG['sobe']:
            return None
        soba_config = CONFIG['sobe'][soba_id]
        mdns_name = soba_config['mdns']
        port = soba_config['port']
    mdns_url = f"http://{mdns_name}:{port}/sysctrl.cgi"
    params = {'CMD': 'GET_IP_ADDRESS'}
    try:
        logging.info(f"RESOLVING: Tražim IP za {soba_id} ({mdns_name})...")
        r = requests.get(mdns_url, params=params, timeout=5)
        r.raise_for_status()
        match = re.search(r'TCP/IP Address = ([\d\.]+)', r.text)
        if match:
            ip_address = match.group(1).strip()
            logging.info(f"RESOLVED: Soba {soba_id} ({mdns_name}) je na IP {ip_address}")
            with config_lock:
                CONFIG['sobe'][soba_id]['cached_ip'] = ip_address
            return ip_address
        else:
            logging.warning(f"Neuspjelo parsiranje IP adrese za {soba_id}. Odgovor: {r.text}")
            return None
    except requests.exceptions.RequestException as e:
        logging.error(f"RESOLVE FAILED: Neuspješno kontaktiranje {mdns_name}. Greška: {e}")
        with config_lock:
            if soba_id in CONFIG['sobe']:
                CONFIG['sobe'][soba_id]['cached_ip'] = None
        return None

def get_cached_url_only(soba_id):
    """
    (BRZA FUNKCIJA) Vraća URL samo ako je IP keširan. NIKADA ne pokreće resolve.
    """
    with config_lock:
        soba_config = CONFIG['sobe'].get(soba_id)
        if not soba_config:
            return None, "soba_not_found"
        
        cached_ip = soba_config.get('cached_ip')
        port = soba_config['port']

    if cached_ip:
        return f"http://{cached_ip}:{port}/sysctrl.cgi", "ok"
    else:
        return None, "resolving"

def send_esp_command(soba_id, params, timeout=3):
    """
    Šalje komandu. NIKADA ne blokira na mDNS resolve.
    Ako IP ne radi, pokreće asinhroni re-resolve i vraća grešku.
    """
    base_url, status = get_cached_url_only(soba_id)
    
    if status == "soba_not_found":
        logging.error(f"FATAL: Pokušaj slanja komande na nepostojeću sobu {soba_id}")
        return None, "Greška: Soba nije pronađena u konfiguraciji."
    
    if status == "resolving":
        logging.warning(f"INFO: Komanda za {soba_id} odbijena (cached_ip=None). Pozadinski task traži adresu.")
        threading.Thread(target=resolve_and_cache_ip, args=(soba_id,), daemon=True).start()
        return None, "Uređaj se još traži (mDNS). Pokušajte ponovo za 10 sekundi."

    try:
        logging.info(f"Šaljem komandu: {base_url}  Params: {params}")
        r = requests.get(base_url, params=params, timeout=timeout)
        r.raise_for_status()
        
        cmd = params.get('CMD')
        if cmd == 'SET_PASSWORD':
            if "Password set OK" not in r.text:
                logging.warning(f"GREŠKA: Očekivan 'Password set OK', dobiveno: {r.text.strip()}")
                return None, "Uređaj je odbio komandu za PIN."
        elif cmd == 'ESP_SET_PIN':
            if "PIN set HIGH" not in r.text:
                logging.warning(f"GREŠKA: Očekivan 'PIN set HIGH', dobiveno: {r.text.strip()}")
                return None, "Uređaj je odbio komandu za SET_PIN."
        elif cmd == 'ESP_RESET_PIN':
             if "PIN set LOW" not in r.text:
                logging.warning(f"GREŠKA: Očekivan 'PIN set LOW', dobiveno: {r.text.strip()}")
                return None, "Uređaj je odbio komandu za RESET_PIN."
        
        logging.info(f"ODGOVOR OK: {r.text.strip()}")
        return r, "ok" # USPJEH
        
    except requests.exceptions.RequestException as e:
        logging.warning(f"GREŠKA KONEKCIJE za {soba_id} na {base_url}: {e}. Pokrećem reaktivni re-resolve.")
        
        with config_lock:
            if soba_id in CONFIG['sobe']:
                CONFIG['sobe'][soba_id]['cached_ip'] = None
        
        threading.Thread(target=resolve_and_cache_ip, args=(soba_id,), daemon=True).start()
        
        return None, "Greška: Konekcija na uređaj nije uspjela. Adresa se osvježava u pozadini. Pokušajte ponovo."

def find_soba_id_by_mdns(mdns_name):
    """Helper funkcija za pronalazak soba_id (npr. '505') iz mDNS imena."""
    with config_lock:
        for soba_id, data in CONFIG.get('sobe', {}).items():
            if data.get('mdns') == mdns_name:
                return soba_id
    return None

# ----------------------------------------------------------------------
#  KORISNIČKE (GOST) RUTE
# ----------------------------------------------------------------------
@app.route('/')
def login_page():
    return render_template('login.html')

@app.route('/soba')
def soba_page():
    return render_template('soba.html')

@app.route('/api/login', methods=['POST'])
def api_login():
    data = request.json
    pin = data.get('pin') 
    if not pin:
        return jsonify({'success': False, 'message': 'PIN nije poslan'}), 400
    
    try:
        with config_lock:
            for soba_id, soba_data in CONFIG.get('sobe', {}).items():
                if soba_data.get('guest_pin') == pin:
                    token_payload = {
                        'soba_id': soba_id, 
                        'guest_pin': pin, # Spremi PIN u token
                        'mdns': soba_data['mdns'],
                        'port': soba_data['port'],
                        'tip': 'gost',
                        'exp': datetime.utcnow() + timedelta(hours=24)
                    }
                    token = jwt.encode(token_payload, app.config['SECRET_KEY'], algorithm='HS256')
                    response = make_response(jsonify({'success': True}))
                    response.set_cookie('token', token, httponly=True, samesite='Strict', max_age=86400)
                    logging.info(f"USPJEŠAN LOGIN: Gost se prijavio za sobu {soba_id} (Ime: {soba_data['ime']})")
                    return response

        logging.warning(f"NEUSPJEŠAN LOGIN: Pogrešan PIN unesen: {pin}")
        return jsonify({'success': False, 'message': 'Pogrešan PIN'}), 401
            
    except Exception as e:
        logging.error(f"Greška u /api/login: {e}")
        return jsonify({'success': False, 'message': 'Greška servera'}), 500

@app.route('/api/control', methods=['POST'])
def api_control():
    token = request.cookies.get('token')
    if not token:
        return jsonify({'success': False, 'message': 'Niste prijavljeni'}), 401
    try:
        soba_data = jwt.decode(token, app.config['SECRET_KEY'], algorithms=['HS256'])
        if soba_data.get('tip') != 'gost':
             return jsonify({'success': False, 'message': 'Neispravan token'}), 401
        
        # --- SIGURNOSNI BLOK (Faza 6.2) ---
        soba_id = soba_data.get('soba_id')
        pin_iz_tokena = soba_data.get('guest_pin')
        if not pin_iz_tokena:
            return jsonify({'success': False, 'message': 'Neispravan token (nedostaje pin). Prijavite se ponovo.'}), 401

        with config_lock:
            if not soba_id or soba_id not in CONFIG['sobe']:
                return jsonify({'success': False, 'message': 'Konfiguracija sobe nije pronađena.'}), 500
            trenutni_guest_pin = CONFIG['sobe'][soba_id].get('guest_pin')
            if trenutni_guest_pin != pin_iz_tokena:
                logging.warning(f"ODBIJENO: Token za sobu {soba_id} je nevažeći (PIN promijenjen). Korisnik izbačen.")
                return jsonify({'success': False, 'message': 'PIN za sobu je promijenjen. Molimo prijavite se ponovo.'}), 401
        # --- KRAJ SIGURNOSNOG BLOKA ---

        data = request.json
        uredjaj = data.get('uredjaj'); vrijednost = data.get('vrijednost')

        with config_lock:
            soba_config = CONFIG['sobe'][soba_id]
            if not uredjaj or uredjaj not in soba_config.get('uredjaji', {}):
                return jsonify({'success': False, 'message': f'Uređaj "{uredjaj}" nije definiran'}), 400
            device_config = soba_config['uredjaji'][uredjaj]
        
        params = device_config.copy()
        komanda = params.get('CMD')
        
        if komanda == 'SET_PIN': params['VALUE'] = '1' if vrijednost else '0'
        elif komanda == 'SET_ROOM_TEMP': params['VALUE'] = str(int(vrijednost))
        elif komanda in ['SET_THST_ON', 'SET_THST_OFF', 'SET_THST_HEATING', 'SET_THST_COOLING']: pass
        else: return jsonify({'success': False, 'message': f'Komanda {komanda} nije podržana'}), 500

        logging.info(f"GOST KONTROLA ({soba_id}): Uređaj '{uredjaj}' -> {params}")
        response, message = send_esp_command(soba_id, params)
        
        if response and response.ok:
            return jsonify({'success': True, 'message': 'Komanda poslana'})
        else:
            return jsonify({'success': False, 'message': message}), 500
            
    except jwt.ExpiredSignatureError:
        logging.info("GOST KONTROLA: Sesija istekla (ExpiredSignatureError)")
        return jsonify({'success': False, 'message': 'Sesija istekla, prijavite se ponovo'}), 401
    except Exception as e:
        logging.error(f"Greška u /api/control: {e}")
        return jsonify({'success': False, 'message': 'Greška servera'}), 500

# ----------------------------------------------------------------------
#  ADMIN RUTE
# ----------------------------------------------------------------------
def provjeri_admin_token(token):
    if not token: return False
    try:
        data = jwt.decode(token, app.config['SECRET_KEY'], algorithms=['HS256'])
        return data.get('tip') == 'admin'
    except: return False

@app.route('/admin')
def admin_login_page():
    return render_template('admin_login.html')

# AŽURIRANO: /admin/dashboard (Ispravno šalje 'guest_pin')
@app.route('/admin/dashboard')
def admin_dashboard():
    if not provjeri_admin_token(request.cookies.get('admin_token')):
        return redirect(url_for('admin_login_page'))
    
    with config_lock:
        sobe_za_admina = {}
        # AŽURIRANO: Ključ je 'soba_id' (fiksni) a ne 'pin'
        for soba_id, data in CONFIG.get('sobe', {}).items(): 
            uredjaji = data.get('uredjaji', {})
            sobe_za_admina[soba_id] = { # Koristi fiksni soba_id kao ključ
                "ime": data.get('ime'),
                "mdns": data.get('mdns'),
                "port": data.get('port'),
                "guest_pin": data.get('guest_pin', 'N/A'), # Šaljemo stvarni PIN
                "termostat_id": uredjaji.get('termostat_set', {}).get('ID', 'N/A'),
                "pin_controller_id": uredjaji.get('pin_controller', {}).get('ID', 'N/A')
            }
    return render_template('admin.html', sobe=sobe_za_admina)

@app.route('/api/admin/login', methods=['POST'])
def api_admin_login():
    data = request.json
    password = data.get('password')
    if not password:
        return jsonify({'success': False, 'message': 'Lozinka nije poslana'}), 400
    if password == CONFIG.get('admin_password'):
        token_payload = { 'tip': 'admin', 'exp': datetime.utcnow() + timedelta(hours=8) }
        token = jwt.encode(token_payload, app.config['SECRET_KEY'], algorithm='HS256')
        response = make_response(jsonify({'success': True}))
        response.set_cookie('admin_token', token, httponly=True, samesite='Strict', max_age=28800)
        logging.info("USPJEŠAN LOGIN: Administrator se prijavio.")
        return response
    else:
        logging.warning("NEUSPJEŠAN LOGIN: Pogrešna admin lozinka.")
        return jsonify({'success': False, 'message': 'Pogrešna lozinka'}), 401

@app.route('/api/admin/get_status', methods=['POST'])
def api_admin_get_status():
    if not provjeri_admin_token(request.cookies.get('admin_token')):
        return jsonify({'success': False, 'message': 'Niste prijavljeni'}), 401
    
    data = request.json
    mdns_name = data.get('mdns')
    if not mdns_name:
        logging.error("ADMIN GRESKA: /api/admin/get_status pozvan bez 'mdns' parametra.")
        return jsonify({'success': False, 'message': 'Greška: Nedostaje mdns u zahtjevu.'}), 400

    soba_id = find_soba_id_by_mdns(mdns_name)
    
    if not soba_id:
        logging.warning(f"ADMIN: Primljen zahtjev za nepoznat mDNS: {mdns_name}")
        return jsonify({'success': False, 'message': f'Greška: mDNS ime {mdns_name} nije pronađeno u config.json.'}), 400
    
    params = {'CMD': 'GET_STATUS'}
    logging.info(f"ADMIN: Tražim GET_STATUS za {soba_id} (mDNS: {mdns_name})") 
    
    response, message = send_esp_command(soba_id, params, timeout=5) 
    
    if response and response.ok:
        parsed_data = parse_get_status(response.text)
        logging.info(f"ADMIN: Uspješno parsiran status za {soba_id}")
        return jsonify({'success': True, 'status': parsed_data})
    else:
        logging.error(f"ADMIN GRESKA: Dohvaćanje statusa za {soba_id} nije uspjelo.")
        return jsonify({'success': False, 'message': message}), 500

@app.route('/api/admin/set_settings', methods=['POST'])
def api_admin_set_settings():
    if not provjeri_admin_token(request.cookies.get('admin_token')):
        return jsonify({'success': False, 'message': 'Niste prijavljeni'}), 401
    
    data = request.json
    mdns_name = data.get('mdns')
    termostat_id = data.get('termostat_id')
    settings = data.get('settings')
    
    if not mdns_name:
         return jsonify({'success': False, 'message': 'Greška: Nedostaje mdns.'}), 400
         
    soba_id = find_soba_id_by_mdns(mdns_name)
    
    if not all([soba_id, settings]):
        logging.error(f"ADMIN SET GRESKA: Poziv bez 'settings' ili 'soba_id' nije pronađen (mdns: {mdns_name})")
        return jsonify({'success': False, 'message': 'Nedostaju ključni podaci (settings) ili mDNS nije pronađen.'}), 400

    commands_to_send = []
    
    if 'mode' in settings:
        mode = settings['mode'].upper()
        if mode == 'HEATING': commands_to_send.append({'CMD': 'TH_HEATING'})
        elif mode == 'COOLING': commands_to_send.append({'CMD': 'TH_COOLING'})
        elif mode == 'ON': commands_to_send.append({'CMD': 'TH_ON'})
        elif mode == 'OFF': commands_to_send.append({'CMD': 'TH_OFF'})
    if 'mdns' in settings:
        commands_to_send.append({'CMD': 'SET_MDNS_NAME', 'MDNS': settings['mdns']})
    if 'port' in settings:
        commands_to_send.append({'CMD': 'SET_TCPIP_PORT', 'PORT': settings['port']})
    if termostat_id and termostat_id != 'N/A' and 'setpoint' in settings:
        commands_to_send.append({'CMD': 'SET_ROOM_TEMP', 'ID': termostat_id, 'VALUE': settings['setpoint']})
    if 'wifi_ssid' in settings and 'wifi_password' in settings:
        cmd = {'CMD': 'SET_SSID_PSWRD', 'SSID': settings['wifi_ssid']}
        if settings['wifi_password']: cmd['PSWRD'] = settings['wifi_password']
        commands_to_send.append(cmd)
    if 'ping_watchdog' in settings:
        if settings['ping_watchdog']: commands_to_send.append({'CMD': 'PINGWDG_ON'})
        else: commands_to_send.append({'CMD': 'PINGWDG_OFF'})
    if 'diff' in settings and settings['diff']:
        try:
            diff_value = int(float(settings['diff']) * 10)
            commands_to_send.append({'CMD': 'TH_DIFF', 'VALUE': diff_value})
        except ValueError:
            logging.warning(f"ADMIN SET: Pogrešna 'diff' vrijednost: {settings['diff']}")
    if 'ema_filter' in settings and settings['ema_filter']:
        try:
            ema_value = int(float(settings['ema_filter']) * 10)
            commands_to_send.append({'CMD': 'TH_EMA', 'VALUE': ema_value})
        except ValueError:
             logging.warning(f"ADMIN SET: Pogrešna 'ema_filter' vrijednost: {settings['ema_filter']}")
    if 'timer_on' in settings and 'timer_off' in settings:
         commands_to_send.append({'CMD': 'SET_TIMER', 'TIMERON': settings['timer_on'], 'TIMEROFF': settings['timer_off']})

    
    success_count = 0
    errors = []
    logging.info(f"ADMIN SET: Soba {soba_id} - Počinjem slanje {len(commands_to_send)} komandi...")
    for params in commands_to_send:
        logging.info(f"ADMIN SET ({soba_id}): Šaljem {params}...")
        response, message = send_esp_command(soba_id, params)
        if response and response.ok:
            success_count += 1
        else:
            errors.append(f"Komanda {params.get('CMD')} nije uspjela: {message}")
            
    if success_count == len(commands_to_send):
        logging.info(f"ADMIN SET ({soba_id}): Uspješno poslane sve komande.")
        return jsonify({'success': True, 'message': f'Sve postavke ({success_count}) uspješno poslane.'})
    else:
        logging.warning(f"ADMIN SET ({soba_id}): Neke komande nisu uspjele. Poslano {success_count}/{len(commands_to_send)}.")
        return jsonify({'success': False, 'message': f'Neke komande nisu uspjele. Poslano {success_count}/{len(commands_to_send)}. Prva greška: {errors[0]}'})

# ----------------------------------------------------------------------
#  API RUTA ZA DOHVAT STATUSA ZA GOSTA (Verzija 7.3 - Finalni Sigurni Sync)
# ----------------------------------------------------------------------
def parse_get_pins_response(response_text):
    """Parsira binarni odgovor GET_PINS i vraća string pinova ('00100...')."""
    match = re.search(r'Pins States = ([\d]+)', response_text)
    if match:
        # Odgovor je npr. '00100000'
        return match.group(1).strip() 
    return None

@app.route('/api/status')
def api_get_guest_status():
    token = request.cookies.get('token')
    if not token:
        return jsonify({'success': False, 'message': 'Niste prijavljeni'}), 401
    try:
        soba_data = jwt.decode(token, app.config['SECRET_KEY'], algorithms=['HS256'])
        if soba_data.get('tip') != 'gost':
             return jsonify({'success': False, 'message': 'Neispravan token'}), 401

        soba_id = soba_data.get('soba_id')
        pin_iz_tokena = soba_data.get('guest_pin')
        if not pin_iz_tokena:
            return jsonify({'success': False, 'message': 'Neispravan token (nedostaje pin). Prijavite se ponovo.'}), 401

        with config_lock:
            if not soba_id or soba_id not in CONFIG['sobe']:
                return jsonify({'success': False, 'message': 'Konfiguracija sobe nije pronađena.'}), 500
            trenutni_guest_pin = CONFIG['sobe'][soba_id].get('guest_pin')
            if trenutni_guest_pin != pin_iz_tokena:
                logging.warning(f"ODBIJENO (STATUS): Token za sobu {soba_id} je nevažeći (PIN promijenjen). Korisnik izbačen.")
                return jsonify({'success': False, 'message': 'PIN za sobu je promijenjen. Molimo prijavite se ponovo.'}), 401
            
            # Dohvat pin-to-device mape i termostat ID-a
            soba_config = CONFIG['sobe'][soba_id]
            termostat_id = soba_config.get('uredjaji', {}).get('termostat_set', {}).get('ID')
            uredjaji_config = soba_config.get('uredjaji', {})
            
            # Mapiranje kontrolera i pinova (npr. { '212': {'1':'light_luster', '2':'light_ambient', ...} })
            controller_to_pins = {}
            for device_name, device_data in uredjaji_config.items():
                if device_name.startswith('light_') and 'ID' in device_data and 'PIN' in device_data:
                    ctrl_id = device_data['ID']
                    pin_broj = device_data['PIN']
                    if ctrl_id not in controller_to_pins:
                        controller_to_pins[ctrl_id] = {}
                    controller_to_pins[ctrl_id][pin_broj] = device_name
        
        if not termostat_id:
            return jsonify({'success': False, 'message': 'ID termostata nije definiran'}), 500

        current_temp, setpoint = 22.0, 22.0
        is_thermostat_on = False
        light_states = {}
        
        # 1. DOHVAT TERMOSTATA I TEMPERATURE (GET_ROOM_TEMP)
        logging.info(f"GOST STATUS ({soba_id}): Tražim GET_ROOM_TEMP (ID: {termostat_id})...")
        params_temp = {'CMD': 'GET_ROOM_TEMP', 'ID': termostat_id}
        r_temp, msg_temp = send_esp_command(soba_id, params_temp)
        
        if r_temp and r_temp.ok:
            temp_match = re.search(r'Room Temperature = (\d+\.?\d*)', r_temp.text)
            set_match = re.search(r'Setpoint Temperature = (\d+\.?\d*)', r_temp.text)
            if temp_match: current_temp = float(temp_match.group(1))
            if set_match: setpoint = float(set_match.group(1))
        
        # 2. DOHVAT TERMOSTATA (TH_STATUS)
        logging.info(f"GOST STATUS ({soba_id}): Tražim TH_STATUS...")
        params_status = {'CMD': 'TH_STATUS'}
        r_status, msg_status = send_esp_command(soba_id, params_status)

        if r_status and r_status.ok:
            mode_match = re.search(r'Mode=([A-Z]+)', r_status.text)
            if mode_match:
                mode = mode_match.group(1)
                if mode == 'COOLING' or mode == 'HEATING':
                    is_thermostat_on = True
        
        # 3. DOHVAT STANJA SVJETALA (GET_PINS)
        # Šaljemo JEDAN zahtjev za SVAKI podređeni kontroler
        for ctrl_id, pin_map in controller_to_pins.items():
            logging.info(f"GOST STATUS ({soba_id}): Tražim GET_PINS za kontroler {ctrl_id}...")
            params_pins = {'CMD': 'GET_PINS', 'ID': ctrl_id}
            r_pins, msg_pins = send_esp_command(soba_id, params_pins)
            
            if r_pins and r_pins.ok:
                pins_states_str = parse_get_pins_response(r_pins.text) # npr. '00100000'
                
                if pins_states_str:
                    # Mapiramo pinove: PIN '1' je prvi karakter, PIN '2' je drugi, itd.
                    # String je 8 karaktera. Pinovi su 1-based (1-8).
                    for pin_str, device_name in pin_map.items():
                        try:
                            pin_index = int(pin_str) - 1 # Pin '1' je indeks 0
                            if pin_index >= 0 and pin_index < len(pins_states_str):
                                # '1' = ON (True), '0' = OFF (False)
                                light_states[device_name] = (pins_states_str[pin_index] == '1')
                            else:
                                logging.warning(f"Mapiranje: Neispravan PIN {pin_str} za {device_name}.")
                        except ValueError:
                            logging.error(f"Mapiranje: Greška konverzije PIN-a za {device_name}.")
            else:
                 logging.warning(f"Nije uspjelo dohvaćanje GET_PINS za kontroler {ctrl_id}.")
        
        
        final_status = {
            'currentTemp': current_temp,
            'setpoint': setpoint,
            'isThermostatOn': is_thermostat_on,
            'lights': light_states
        }

        logging.info(f"GOST STATUS ({soba_id}): Vraćam Temp={current_temp}, Setpoint={setpoint}, On={is_thermostat_on}, Svjetla={len(light_states)}.")
        return jsonify({'success': True, 'status': final_status})

    except jwt.ExpiredSignatureError:
        return jsonify({'success': False, 'message': 'Sesija istekla'}), 401
    except Exception as e:
        logging.error(f"Greška u /api/status (Sigurni Sync): {e}")
        return jsonify({'success': False, 'message': 'Greška servera'}), 500
# ----------------------------------------------------------------------
#  FUNKCIJE ZA EKSTERNI API (PIN SYNC)
# ----------------------------------------------------------------------
def sync_pin_on_server_and_device(room_id, new_pin):
    with config_lock:
        if room_id not in CONFIG['sobe']:
             return False, f'Greška: Room ID {room_id} nije pronađen u konfiguraciji.'
        soba_config = CONFIG['sobe'][room_id]
        if 'pin_controller' not in soba_config.get('uredjaji', {}):
            return False, f'Greška: Soba {room_id} nema definiran "pin_controller" u config.json.'
        pin_ctrl_config = soba_config['uredjaji']['pin_controller']
        controller_id = pin_ctrl_config.get('ID')
        if not controller_id:
             return False, f'Greška: "pin_controller" za sobu {room_id} nema definiran ID.'

    validity = '1200010130' 
    password_param = f"G1,{new_pin},{validity}"
    params = {'CMD': 'SET_PASSWORD', 'ID': controller_id, 'PASSWORD': password_param}
    
    response, message = send_esp_command(room_id, params, timeout=5) 
    
    if response and response.ok:
        with config_lock:
            CONFIG['sobe'][room_id]['guest_pin'] = new_pin
        
        if save_config():
            logging.info(f"PIN SYNC: Soba {room_id} uspješno sinkronizirana na PIN {new_pin}.")
            return True, f'PIN sobe {room_id} uspješno sinkroniziran na {new_pin} (Uređaj & Server).'
        else:
            logging.critical(f"PIN SYNC GREŠKA: PIN promijenjen na uređaju, ali NE I u config.json! Soba: {room_id}.")
            return False, f'PIN je promijenjen na uređaju, ali ne i na serveru (greška pri upisu)!'
    else:
        logging.error(f"PIN SYNC GREŠKA: ESP32 ({room_id}) nije prihvatio PIN. Poruka: {message}")
        return False, f'Greška: ESP32 nije prihvatio PIN. ({message})'


@app.route('/api/external/sync_pin', methods=['POST'])
def api_external_sync_pin():
    data = request.json
    external_key = data.get('api_key'); room_id = data.get('room_id'); new_pin = data.get('new_pin')
    
    with config_lock:
        config_api_key = CONFIG.get('external_api_key')
    
    if not config_api_key or external_key != config_api_key:
        logging.warning(f"AUTH: Odbijen eksterni API poziv (pogrešan ključ).")
        return jsonify({'success': False, 'message': 'Neispravan eksterni API ključ.'}), 401

    with config_lock:
        soba_postoji = room_id in CONFIG['sobe']
        
    if not all([room_id, new_pin]) or not soba_postoji:
        logging.warning(f"API SYNC: Neispravan zahtjev (Room ID: {room_id}, Novi PIN: {new_pin}).")
        return jsonify({'success': False, 'message': 'Nedostaje room_id, new_pin ili room_id nije pronađen.'}), 400

    success, message = sync_pin_on_server_and_device(room_id, new_pin)

    if success:
        return jsonify({'success': True, 'message': message})
    else:
        return jsonify({'success': False, 'message': message}), 500

@app.route('/api/external/delete_pin', methods=['POST'])
def api_external_delete_pin():
    data = request.json
    external_key = data.get('api_key'); room_id = data.get('room_id')
    
    with config_lock:
        config_api_key = CONFIG.get('external_api_key')
    
    if not config_api_key or external_key != config_api_key:
        logging.warning(f"AUTH: Odbijen eksterni API poziv (pogrešan ključ).")
        return jsonify({'success': False, 'message': 'Neispravan eksterni API ključ.'}), 401

    with config_lock:
        if room_id not in CONFIG['sobe']:
            return jsonify({'success': False, 'message': f'Room ID {room_id} nije pronađen.'}), 400
        soba_config = CONFIG['sobe'][room_id]
        if 'pin_controller' not in soba_config.get('uredjaji', {}):
            return jsonify({'success': False, 'message': f'Soba {room_id} nema "pin_controller".'}), 400
        controller_id = soba_config['uredjaji']['pin_controller'].get('ID')
        if not controller_id:
            return jsonify({'success': False, 'message': f'Soba {room_id} "pin_controller" nema ID.'}), 400

    logging.info(f"PIN DELETE: Brišem PIN za sobu {room_id} (ID: {controller_id})")
    params = {'CMD': 'SET_PASSWORD', 'ID': controller_id, 'PASSWORD': 'G1X'}
    response, message = send_esp_command(room_id, params, timeout=5)

    if response and response.ok:
        with config_lock:
            CONFIG['sobe'][room_id]['guest_pin'] = ""
        
        if save_config():
            logging.info(f"PIN DELETE: PIN za sobu {room_id} uspješno obrisan (Uređaj & Server).")
            return jsonify({'success': True, 'message': f'PIN za sobu {room_id} uspješno obrisan.'})
        else:
            logging.critical(f"PIN DELETE GREŠKA: PIN obrisan na uređaju, ali NE I u config.json! Soba: {room_id}.")
            return jsonify({'success': False, 'message': 'PIN je obrisan na uređaju, ali ne i na serveru!'})
    else:
        logging.error(f"PIN DELETE GREŠKA: ESP32 ({room_id}) nije prihvatio G1X. Poruka: {message}")
        return jsonify({'success': False, 'message': f'Greška: ESP32 nije prihvatio komandu za brisanje. ({message})'})

# ----------------------------------------------------------------------
#  DODATNE ADMIN API RUTE
# ----------------------------------------------------------------------
@app.route('/api/admin/restart_esp', methods=['POST'])
def api_admin_restart_esp():
    if not provjeri_admin_token(request.cookies.get('admin_token')):
        return jsonify({'success': False, 'message': 'Niste prijavljeni'}), 401
    
    data = request.json
    mdns_name = data.get('mdns')
    soba_id = find_soba_id_by_mdns(mdns_name)
    if not soba_id:
        return jsonify({'success': False, 'message': 'mDNS nije pronađen.'}), 400

    logging.info(f"ADMIN RESTART: Pokrećem restart za {soba_id} ({mdns_name})")
    response, message = send_esp_command(soba_id, {'CMD': 'RESTART'}, timeout=5)
    
    if response and response.ok and "Restart in 3s" in response.text:
        return jsonify({'success': True, 'message': 'Komanda za restart poslana. Uređaj se restartuje.'})
    else:
        return jsonify({'success': False, 'message': message})

@app.route('/api/admin/set_esp_time', methods=['POST'])
def api_admin_set_esp_time():
    if not provjeri_admin_token(request.cookies.get('admin_token')):
        return jsonify({'success': False, 'message': 'Niste prijavljeni'}), 401
    
    data = request.json
    mdns_name = data.get('mdns')
    date_str = data.get('date') # Očekuje DDMMYY
    time_str = data.get('time') # Očekuje HHMMSS
    
    soba_id = find_soba_id_by_mdns(mdns_name)
    if not soba_id:
        return jsonify({'success': False, 'message': 'mDNS nije pronađen.'}), 400
    if not date_str or not time_str:
        return jsonify({'success': False, 'message': 'Nedostaje datum ili vrijeme.'}), 400

    logging.info(f"ADMIN SET TIME: Šaljem vrijeme {date_str} {time_str} na {soba_id}")
    params = {'CMD': 'SET_TIME', 'DATE': date_str, 'TIME': time_str}
    response, message = send_esp_command(soba_id, params, timeout=5)
    
    if response and response.ok and "RTC Date & Time Set OK" in response.text:
        return jsonify({'success': True, 'message': 'Vrijeme na uređaju uspješno postavljeno.'})
    else:
        return jsonify({'success': False, 'message': message})

@app.route('/api/admin/esp_pin_control', methods=['POST'])
def api_admin_esp_pin_control():
    if not provjeri_admin_token(request.cookies.get('admin_token')):
        return jsonify({'success': False, 'message': 'Niste prijavljeni'}), 401
    
    data = request.json
    mdns_name = data.get('mdns')
    pin = data.get('pin')
    state = data.get('state')
    
    soba_id = find_soba_id_by_mdns(mdns_name)
    if not soba_id:
        return jsonify({'success': False, 'message': 'mDNS nije pronađen.'}), 400
    if not pin:
        return jsonify({'success': False, 'message': 'Nedostaje PIN.'}), 400

    komanda = 'ESP_SET_PIN' if state else 'ESP_RESET_PIN'
    
    logging.info(f"ADMIN PIN CONTROL: Soba {soba_id}, PIN {pin}, Stanje {state}, Komanda {komanda}")
    params = {'CMD': komanda, 'PIN': pin}
    
    response, message = send_esp_command(soba_id, params, timeout=5)
    
    if response and response.ok:
        return jsonify({'success': True, 'message': f'PIN {pin} postavljen.'})
    else:
        return jsonify({'success': False, 'message': message})

# ----------------------------------------------------------------------
#  NOVO: API RUTA ZA PROMJENU PINA (ADMIN)
# ----------------------------------------------------------------------
@app.route('/api/admin/change_pin', methods=['POST'])
def api_admin_change_pin():
    """API za promjenu PIN-a pozvan iz Admin Dashboarda."""
    if not provjeri_admin_token(request.cookies.get('admin_token')):
        return jsonify({'success': False, 'message': 'Niste prijavljeni (neispravan token).'}), 401
    
    data = request.json
    room_id = data.get('room_id')
    new_pin = data.get('new_pin')

    if not all([room_id, new_pin]):
        return jsonify({'success': False, 'message': 'Nedostaje ID sobe ili novi PIN.'}), 400

    # Koristimo istu centralnu logiku kao i eksterni API
    logging.info(f"ADMIN PIN CHANGE: Admin mijenja PIN za sobu {room_id} na {new_pin}")
    success, message = sync_pin_on_server_and_device(room_id, new_pin)

    if success:
        return jsonify({'success': True, 'message': message})
    else:
        return jsonify({'success': False, 'message': message}), 500
        
# ----------------------------------------------------------------------
#  POZADINSKI PROCES ZA AŽURIRANJE IP ADRESA
# ----------------------------------------------------------------------
def background_resolver_task():
    logging.info("Pokrenut pozadinski IP resolver.")
    while True:
        try:
            time.sleep(60) # Provjera svakih 60 sekundi
            logging.info("PROAKTIVNI RESOLVER: Pokrećem provjeru IP adresa...")
            with config_lock:
                soba_ids = list(CONFIG.get('sobe', {}).keys())
            if not soba_ids:
                logging.info("PROAKTIVNI RESOLVER: Nema soba u config.json.")
                continue
            for soba_id in soba_ids:
                resolve_and_cache_ip(soba_id)
                time.sleep(1)
            logging.info("PROAKTIVNI RESOLVER: Provjera IP adresa završena.")
        except Exception as e:
            logging.error(f"Greška u pozadinskom resolveru: {e}")
            time.sleep(60)

# ----------------------------------------------------------------------
#  POKRETANJE SERVERA
# ----------------------------------------------------------------------
if __name__ == '__main__':
    load_config() 
    
    logging.info("Pokrećem inicijalno mapiranje IP adresa U POZADINI...")
    with config_lock:
        initial_soba_ids = list(CONFIG.get('sobe', {}).keys())
    
    def initial_map():
        for soba_id in initial_soba_ids:
            resolve_and_cache_ip(soba_id)
            time.sleep(0.5)
        logging.info("Inicijalno mapiranje završeno.")
        
    initial_map_thread = threading.Thread(target=initial_map, daemon=True)
    initial_map_thread.start()
    
    resolver_thread = threading.Thread(target=background_resolver_task, daemon=True)
    resolver_thread.start()

    print("--- Pokrećem Toplik Service (PRODUKCIJA Faza 6.3) ---")
    print("--- Server radi na http://0.0.0.0:5000 ---")
    print("--- Pritisnite CTRL+C za zaustavljanje ---")
    
    try:
        serve(app, host='0.0.0.0', port=5000, threads=10)
    except ImportError:
        logging.critical("!!! GRESKA: 'waitress' nije instaliran.")
        logging.critical("!!! Pokrenite 'instaliraj_biblioteke.bat' ponovo.")