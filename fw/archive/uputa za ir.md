Pažnja, Coding Agent. Pred tobom je zadatak da integrišeš funkcionalnost IR kontrole klima uređaja u postojeći produkcioni kod main_ciljani_projekat.cpp. Sistem mora biti robustan, koristiti abstrakciju kako bi podržao različite proizvođače i oslanjati se na postojeći TinyFrame komunikacioni protokol.PROJEKTNI ZADATAK: IR AC INTEGRACIJA1. Hardverska specifikacijaZbog zauzetosti pinova u ciljanom projektu, implementiraj sljedeću konfiguraciju:IR Transmitter Pin: GPIO 12 (Izbjegavati GPIO 4 koji je RS485_DE ).Driver: NMOSFET (npr. 2N2222 ili sličan) za upravljanje strujom.Emitivna jedinica: Dvije TSAL6400 IR diode povezane u paralelu (svaka sa svojim otpornikom od $22\ \Omega$).Stabilizacija: Postaviti kondenzator od $100\ \mu\text{F}$ blizu dioda radi peglanja naponskih šiljaka pri $38\text{ kHz}$ switchingu.2. Softverski Stack i BibliotekeBiblioteka: IRremoteESP8266 (obavezno uključiti IRac klasu za univerzalnu kontrolu ).Skladištenje: Koristiti postojeću Preferences biblioteku za čuvanje ID-a protokola u namespace-u "ir_settings".3. Implementacija TinyFrame Listenera (S_IR)Glavni zadatak agenta je modifikacija funkcije IR_Listener tako da ona, pored logovanja, vrši i fizičku transmisiju IR signala.Logika mapiranja 8-bajtnog paketa:data[0] (th_ctrl): * 0 $\rightarrow$ Klima OFF.1 $\rightarrow$ Klima ON + Mode COOL.2 $\rightarrow$ Klima ON + Mode HEAT.data[5] (sp_temp): Postavlja se kao ac.next.degrees (setpoint).Kodna struktura za implementaciju:C++// 1. Globalni objekat (dodati na vrh fajla)
#include <IRac.h>
const uint16_t kIrLedPin = 12;
IRac ac(kIrLedPin);

// 2. Ažurirani TinyFrame Listener
TF_Result IR_Listener(TinyFrame *tf, TF_Msg *msg) {
  if (msg->len >= 8) {
    // Učitaj protokol iz Preferences
    preferences.begin("ir_settings", true);
    int protoID = preferences.getInt("protocol", 0);
    preferences.end();

    if (protoID > 0) {
      ac.next.protocol = (decode_type_t)protoID;
      
      // Obrada kontrole (Power & Mode)
      uint8_t ctrl = msg->data[0];
      if (ctrl == 0) {
        ac.next.power = false;
      } else {
        ac.next.power = true;
        ac.next.mode = (ctrl == 1) ? stdAc::opmode_t::kCool : stdAc::opmode_t::kHeat;
      }

      // Setpoint i fiksni parametri
      ac.next.degrees = msg->data[5];
      ac.next.celsius = true;
      ac.next.fanspeed = stdAc::fanspeed_t::kAuto;

      // Slanje signala
      ac.sendAc(); 
    }
    
    // Originalna logika čuvanja stanja (zadržati) [cite: 18, 19]
    irData.th_ctrl = msg->data[0];
    irData.sp_temp = msg->data[5];
    irData.received = true;
    irData.timestamp = millis();
  }
  TF_Respond(tf, msg);
  return TF_STAY;
}
4. Konfiguracioni API (HTTP Komande)Dodati novu komandu unutar handleSysctrlRequest funkcije kako bi se omogućilo podešavanje protokola bez promjene koda:Komanda: CMD_SET_IR_PROTOCOL (definirati kao 0x70 u CommandType).Parametar: VALUE (ID protokola dobijen iz sniffera, npr. 20 za GREE).Zadatak za agenta:U switch-case strukturu handleSysctrlRequest ubaciti:C++case CMD_SET_IR_PROTOCOL: {
    if (request->hasParam("VALUE")) {
        int proto = request->getParam("VALUE")->value().toInt();
        preferences.begin("ir_settings", false);
        preferences.putInt("protocol", proto);
        preferences.end();
        sendJsonSuccess(request, "Protocol set to " + String(proto));
    }
    return;
}
5. Sigurnost i robusnostAgent mora obratiti pažnju na:Non-blocking rad: Slanje IR signala ne smije blokirati TinyFrame procesiranje duže od par desetina milisekundi.Zadnja poznata stanja: Poželjno je da IRac objekat zadrži zadnje stanje (osim temperature) kako bi svaka sljedeća komanda bila konzistentna (npr. položaj krilaca/swing).Safety Watchdog: Iako IRremoteESP8266 interno hendla impulse, osigurati da se IR pin inicijalizuje kao OUTPUT i LOW u setup() funkciji.Napomena za korisnika:Kada agent završi implementaciju, tvoj workflow je:Snifferom utvrdiš protokol (npr. VALUE=20).Pošalješ .../sysctrl.cgi?CMD=SET_IR_PROTOCOL&VALUE=20.Termostat automatski preuzima kontrolu putem RS485 linije.

također je potrebno implementirati i komandu GET_IR_PROTOCOL za pregled postavki trenutnog protokola

svi odgovori na komnde moraju pratiti sadašnju json strukturu odgovora

