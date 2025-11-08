import sys
# Prisilno dodaj ispravnu sistemsku putanju
sys.path.insert(0, '/usr/local/lib/python3.13/dist-packages')
import logging
import requests
import time
import json
from fauxmo import Fauxmo
from fauxmo.utils import get_local_ip

# --- POSTAVKE (NEKORIŠTENO, SLANJE IDE PREKO mDNS) ---
# Ovdje unesite stvarni IP vašeg Raspberry Pi servera
TOPLIK_BASE_URL = 'http://192.168.88.70:5000/api/control'

# =================================================================
# AŽURIRANO: Svjetla (light_luster, light_ambient, light_krevet, light_wc, light_ogledalo)
# =================================================================

ROOM_CONFIG = {
    '301': {'mdns': 'soba301.local', 'port': '8020', 'id': {'luster': '200', 'ambient': '200', 'krevet': '200', 'wc': '200', 'ogledalo': '200'}},
    '302': {'mdns': 'soba302.local', 'port': '8020', 'id': {'luster': '201', 'ambient': '201', 'krevet': '201', 'wc': '201', 'ogledalo': '201'}},
    '303': {'mdns': 'soba303.local', 'port': '8020', 'id': {'luster': '202', 'ambient': '202', 'krevet': '202', 'wc': '202', 'ogledalo': '202'}},
    '501': {'mdns': 'soba501.local', 'port': '8020', 'id': {'luster': '203', 'ambient': '203', 'krevet': '203', 'wc': '203', 'ogledalo': '203'}},
    '502': {'mdns': 'soba502.local', 'port': '8020', 'id': {'luster': '204', 'ambient': '204', 'krevet': '204', 'wc': '204', 'ogledalo': '204'}},
    '503': {'mdns': 'soba503.local', 'port': '8020', 'id': {'luster': '205', 'ambient': '205', 'krevet': '205', 'wc': '205', 'ogledalo': '205'}},
    '504': {'mdns': 'soba504.local', 'port': '8020', 'id': {'luster': '206', 'ambient': '206', 'krevet': '206', 'wc': '206', 'ogledalo': '206'}},
    '505': {'mdns': 'soba505.local', 'port': '8020', 'id': {'luster': '212', 'ambient': '212', 'krevet': '212', 'wc': '213', 'ogledalo': '213'}},
}

DEVICE_CONFIG = {
    'LIGHTS': [
        # SVE SOBA (301-505)
        
        # SOBA 301
        {'name': 'Chandelier 301', 'port': 11000, 'device_type': 'luster'},
        {'name': 'Ambient Light 301', 'port': 11001, 'device_type': 'ambient'},
        {'name': 'Bed Light 301', 'port': 11002, 'device_type': 'krevet'},
        {'name': 'WC Light 301', 'port': 11003, 'device_type': 'wc'},
        {'name': 'Mirror Light 301', 'port': 11004, 'device_type': 'ogledalo'},
        
        # SOBA 302
        {'name': 'Chandelier 302', 'port': 11005, 'device_type': 'luster'},
        {'name': 'Ambient Light 302', 'port': 11006, 'device_type': 'ambient'},
        {'name': 'Bed Light 302', 'port': 11007, 'device_type': 'krevet'},
        {'name': 'WC Light 302', 'port': 11008, 'device_type': 'wc'},
        {'name': 'Mirror Light 302', 'port': 11009, 'device_type': 'ogledalo'},
        
        # SOBA 303
        {'name': 'Chandelier 303', 'port': 11010, 'device_type': 'luster'},
        {'name': 'Ambient Light 303', 'port': 11011, 'device_type': 'ambient'},
        {'name': 'Bed Light 303', 'port': 11012, 'device_type': 'krevet'},
        {'name': 'WC Light 303', 'port': 11013, 'device_type': 'wc'},
        {'name': 'Mirror Light 303', 'port': 11014, 'device_type': 'ogledalo'},
        
        # SOBA 501
        {'name': 'Chandelier 501', 'port': 11015, 'device_type': 'luster'},
        {'name': 'Ambient Light 501', 'port': 11016, 'device_type': 'ambient'},
        {'name': 'Bed Light 501', 'port': 11017, 'device_type': 'krevet'},
        {'name': 'WC Light 501', 'port': 11018, 'device_type': 'wc'},
        {'name': 'Mirror Light 501', 'port': 11019, 'device_type': 'ogledalo'},
        
        # SOBA 502
        {'name': 'Chandelier 502', 'port': 11020, 'device_type': 'luster'},
        {'name': 'Ambient Light 502', 'port': 11021, 'device_type': 'ambient'},
        {'name': 'Bed Light 502', 'port': 11022, 'device_type': 'krevet'},
        {'name': 'WC Light 502', 'port': 11023, 'device_type': 'wc'},
        {'name': 'Mirror Light 502', 'port': 11024, 'device_type': 'ogledalo'},
        
        # SOBA 503
        {'name': 'Chandelier 503', 'port': 11025, 'device_type': 'luster'},
        {'name': 'Ambient Light 503', 'port': 11026, 'device_type': 'ambient'},
        {'name': 'Bed Light 503', 'port': 11027, 'device_type': 'krevet'},
        {'name': 'WC Light 503', 'port': 11028, 'device_type': 'wc'},
        {'name': 'Mirror Light 503', 'port': 11029, 'device_type': 'ogledalo'},
        
        # SOBA 504
        {'name': 'Chandelier 504', 'port': 11030, 'device_type': 'luster'},
        {'name': 'Ambient Light 504', 'port': 11031, 'device_type': 'ambient'},
        {'name': 'Bed Light 504', 'port': 11032, 'device_type': 'krevet'},
        {'name': 'WC Light 504', 'port': 11033, 'device_type': 'wc'},
        {'name': 'Mirror Light 504', 'port': 11034, 'device_type': 'ogledalo'},
        
        # SOBA 505
        {'name': 'Chandelier 505', 'port': 11035, 'device_type': 'luster'},
        {'name': 'Ambient Light 505', 'port': 11036, 'device_type': 'ambient'},
        {'name': 'Bed Light 505', 'port': 11037, 'device_type': 'krevet'},
        {'name': 'WC Light 505', 'port': 11038, 'device_type': 'wc'},
        {'name': 'Mirror Light 505', 'port': 11039, 'device_type': 'ogledalo'},
    ]
}


# --- LOGIKA SLANJA KOMANDE DIREKTNO NA ESP32 ---
def send_toplik_command(device_name, state):
    """
    Prevodi Fauxmo komandu (ON/OFF) u direktan HTTP zahtjev za ESP32.
    """
    value = '1' if state.lower() == 'on' else '0'
    
    try:
        parts = device_name.split()
        if len(parts) < 2 or not parts[-1].isdigit():
            logging.error(f"Neispravan format imena uređaja: {device_name}")
            return False
            
        room_id = parts[-1] 
        device_type = parts[0].lower() 
        
        # Mapiranje generičkog imena na ključ iz ROOM_CONFIG
        if 'chandelier' in device_type: device_key = 'luster'
        elif 'ambient' in device_type: device_key = 'ambient'
        elif 'bed' in device_type: device_key = 'krevet'
        elif 'wc' in device_type: device_key = 'wc'
        elif 'mirror' in device_type: device_key = 'ogledalo'
        else:
            logging.error(f"Nepoznat tip uređaja: {device_type}")
            return False

        if room_id not in ROOM_CONFIG:
            logging.error(f"Soba ID {room_id} nije pronađen u ROOM_CONFIG.")
            return False
        
        room_conf = ROOM_CONFIG[room_id]
        ctrl_id = room_conf['id'][device_key] 
        mdns_name = room_conf['mdns']
        port = room_conf['port']
        
        # === ISPRAVLJENO MAPIRANJE PINOVA (Sintaksno ispravno) ===
        if ctrl_id == '212': 
            pin_map = {'luster': '1', 'ambient': '2', 'krevet': '3'}
        elif ctrl_id == '213': 
            pin_map = {'wc': '1', 'ogledalo': '2'}
        else: 
            # Default za sve ostale kontrolere (npr. 200-206)
            pin_map = {'luster': '1', 'ambient': '2', 'krevet': '3', 'wc': '5', 'ogledalo': '6'}
            
        if device_key not in pin_map:
            logging.error(f"PIN MAP ERROR: {device_key} nije definiran za kontroler ID {ctrl_id}.")
            return False
            
        pin = pin_map[device_key] # Dohvaćanje ispravnog pina

        
        # Puna ESP komanda (CMD=SET_PIN&ID=212&PIN=1&VALUE=1)
        params = {'CMD': 'SET_PIN', 'ID': ctrl_id, 'PIN': pin, 'VALUE': value}
        url = f"http://{mdns_name}:{port}/sysctrl.cgi"

        logging.info(f"ALEXA -> ESP: Soba {room_id}, {device_key} (ID {ctrl_id}/PIN {pin}) -> {state}")
        
        # Šaljemo HTTP zahtjev direktno ESP-u
        response = requests.get(url, params=params, timeout=5)
        
        if response.status_code == 200 and "Pin Set OK" in response.text:
            return True
        else:
            logging.error(f"ESP ERROR: Status {response.status_code}. Odgovor: {response.text}")
            return False

    except Exception as e:
        logging.error(f"Kritična greška pri slanju komande: {e}")
        return False

# --- FAUXMO PLUGIN (Isto) ---
class ToplikLight(Fauxmo):
    def __init__(self, name, port, device_type):
        self.name = name
        self.port = port
        self.ip_address = get_local_ip()
        self.url_base = f'http://{self.ip_address}:{self.port}'
        self.state = 'off'

    def on(self):
        result = send_toplik_command(self.name, 'ON')
        if result:
            self.state = 'on'
            return True
        return False

    def off(self):
        result = send_toplik_command(self.name, 'OFF')
        if result:
            self.state = 'off'
            return True
        return False

    def get_state(self):
        return {'state': self.state}


# --- GLAVNA FUNKCIJA ZA POKRETANJE BRIDGEA (Isto) ---
def main():
    logging.basicConfig(level=logging.INFO)
    
    # 1. Kreiraj listu Fauxmo uređaja
    fauxmo_plugins = []
    
    for light in DEVICE_CONFIG['LIGHTS']:
        # Kreiraj instancu ToplikLight za svaki uređaj
        fauxmo_plugins.append(ToplikLight(
            name=light['name'],
            port=light['port'],
            device_type=light['device_type']
        ))

    # 2. Pokreni Fauxmo server
    print("\n--- Pokrećem Alexa Bridge (Fauxmo) ---")
    print("Pretraživanje uređaja mora biti pokrenuto na Alexa aplikaciji.")
    
    if not fauxmo_plugins:
        logging.error("Nema konfigurisanih uređaja. Provjerite DEVICE_CONFIG.")
        return

    fauxmo = Fauxmo(fauxmo_plugins)
    fauxmo.run()

if __name__ == '__main__':
    main()