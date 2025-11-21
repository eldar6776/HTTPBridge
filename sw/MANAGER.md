potrebno je doraditi ovaj projekat na način da se doda još jedna web stranica za managera, manager unosi svoj pin i poslije toga mu se isporučuje stranica sa 
grafičkim stilom kao gostima, manager na svojoj stranici ima ikonice soba 301,302,303,501,502,503,504,505,  klikom na bilo koju od ovih ikonica manageru se
isporučuje identična stranica kao i gostu sobe samo šta manager nema prekid validnsoti kao gost kada se promjeni pin sobe, dakle manager može uvijek otvoriti 
interefejs za kontolu sobe, poslije tih ikonica ima još jednu ikonicu "Vanjska rasvjeta" ili kako god je agent nazove, ova ikonica je toogle tipa i pošto naš python
server svakako u pozadini non stop uzima statuse soba onda se može lako imati status vanjske rasvjete, ako je i jedno od vanjskih svjetala na esp32 uređajima (Light Relay State=ON)
uključeno onda je i estetsko stanje ove ikone uključeno i obratno ako u sva vanjska svjetla isključena onda je stanje ove ikone isključeno, klik na ovu ikonu će 
pokrenuti niz komandi kao što je ova  http://soba501.local:8020/sysctrl.cgi?CMD=OUTDOOR_LIGHT_ON / OUTDOOR_LIGHT_OFF dakle na svih 8 esp32 uređaja, dalje 
manager ima još jednu ikonicu grijanje, klik na tu ikonicu neka isporuči novu stranicu sa elementima: kontrola termostata kao što je za sobu napravljeno samo 
što je ovo "termostat fitnesa" dodatno će taj termostat imati on/off toogle dugme za isključenje/uključenje termostata i toogle dugme grijanje/hlađenje koje određuje mod rada
te komande će izgledati ovako http://soba501.local:8020/sysctrl.cgi?CMD=TH_HEATING / CMD=TH_COOLING ili http://soba501.local:8020/sysctrl.cgi?CMD=TH_ON / CMD=TH_OFF  ili
slider http://soba501.local:8020/sysctrl.cgi?CMD=TH_SETPOINT&VALUE=24 dakle ovo ćemo u pozadini podesiti negdje da li u config.json fajlu ili negdje drugo ove komande.
ispod te kontrole termostata imaćemo i toogle prpekidače PUMPA FANCOIL / PUMPA PODNO ove komande će biti ovakvog tipa http://soba501.local:8020/sysctrl.cgi?CMD=ESP_SET_PIN&PIN=22