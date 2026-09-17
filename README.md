# Line Follower - ESP32 + L298N + TCRT5000

Robot sledující čáru s PID regulací a jednoduchým webovým ovládáním
(ladění PID konstant a rychlosti bez nutnosti přenahrávat kód).

## Co budeš potřebovat

- ESP32 (dev kit)
- L298N motor driver
- 5-kanálový IR senzor TCRT5000 (LaskaKit LA131112)
- Podvozek + 2x DC motorek (máš)
- Napájení pro motory (např. 2x 18650, 7.4V) + napájení pro ESP32
  (USB powerbanka, nebo z L298N přes jeho 5V regulátor - viz níže)

## Zapojení

### 5x IR senzor TCRT5000 → ESP32

Senzory číslujeme zleva doprava (1 = úplně vlevo, 5 = úplně vpravo).

| Senzor | ESP32 pin |
|--------|-----------|
| OUT 1  | GPIO34    |
| OUT 2  | GPIO35    |
| OUT 3  | GPIO32    |
| OUT 4  | GPIO33    |
| OUT 5  | GPIO25    |
| VCC    | 3V3 nebo 5V (podle modulu - u LaskaKit desky obvykle 3.3-5V) |
| GND    | GND       |

### L298N → ESP32

| L298N pin | ESP32 pin |
|-----------|-----------|
| ENA       | GPIO27    |
| IN1       | GPIO26    |
| IN2       | GPIO14    |
| IN3       | GPIO16    |
| IN4       | GPIO17    |
| ENB       | GPIO13    |
| GND       | GND (společná se senzory i ESP32!) |

### L298N → motory a napájení

| L298N          | Připoj              |
|----------------|----------------------|
| OUT1, OUT2     | levý motor           |
| OUT3, OUT4     | pravý motor          |
| 12V / VCC vstup| baterie motorů (7-12V) |
| GND            | mínus baterie + společná zem s ESP32 |

**Důležité:** Pokud je napájecí napětí motorů vyšší než 12 V, nepoužívej
palubní 5V regulátor na L298N pro napájení ESP32 - mohl by se přehřát.
V takovém případě napájej ESP32 zvlášť (powerbanka / USB / vlastní
5V regulátor). GND musí být propojená mezi ESP32, L298N a senzory,
i když je napájení oddělené.

## Struktura projektu

```
line_follower/
  line_follower.ino   <- firmware (PID, motory, senzory, API)
  data/
    index.html        <- webová stránka - uprav klidně jen tenhle soubor
```

Webová stránka **není** součástí .ino souboru. Žije jako samostatný
`index.html` v podsložce `data/` a ESP32 ji čte za běhu z vlastní
flash paměti (LittleFS souborový systém). Díky tomu:

- Když chceš změnit vzhled/rozložení stránky, staci editovat
  `data/index.html` - nemusíš se prokousat celým C++ kódem.
- Po úpravě stačí nahrát **jen filesystem** (viz níže), ne celý
  firmware - je to rychlejší a nehrozí, že si omylem rozbiješ logiku
  robota.

## Instalace

1. V Arduino IDE otevři **Soubory → Předvolby** a do "Additional Board
   Manager URLs" přidej:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. **Nástroje → Deska → Správce desek** → vyhledej `esp32` (Espressif
   Systems) → nainstaluj verzi **3.0.0 nebo vyšší** (kvůli `ledcAttach`).
3. Vyber svoji desku (např. "ESP32 Dev Module").
4. Otevři `line_follower.ino` a nahraj do ESP32 (klasické tlačítko
   Upload - nahraje se jen samotný firmware, **ne** webová stránka).

## Nahrání webové stránky (LittleFS)

Webová stránka (`data/index.html`) se do ESP32 nahrává zvlášť,
samostatným nástrojem - ne přes běžné tlačítko Upload.

**Arduino IDE 2.x:**

1. Nainstaluj plugin **arduino-littlefs-upload**:
   https://github.com/earlephilhower/arduino-littlefs-upload
   (stlač "Releases" vpravo, stahni `.vsix` soubor a ulož ho do
   složky `~/Documents/Arduino/plugins/` - pokud složka `plugins`
   neexistuje, vytvoř ji ručně). Pak Arduino IDE restartuj.
2. Otevři sketch `line_follower.ino`.
3. Stiskni **Ctrl+Shift+P** (nebo Cmd+Shift+P na Macu) a napiš
   "Upload LittleFS to Pico/ESP8266/ESP32" - vyber tuto akci.
4. Nástroj zabalí obsah složky `data/` a nahraje ho do ESP32.

**Důležité - Partition Scheme:** V menu **Nástroje → Partition
Scheme** vyber nějaké schéma, které obsahuje SPIFFS/LittleFS prostor,
např. **"Default 4MB with spiffs"**. Bez toho není pro webovou
stránku kam ukládat.

Poté když upravíš `data/index.html`, stačí zopakovat jen krok 3
(LittleFS upload) - firmware (.ino) se přitom vůbec nemusí
překompilovávat ani nahrávat znovu.

## Kalibrace senzorů

Po nahrání otevři Sériový monitor (115200 baud) nebo rovnou web.
Přilož robota nad černou čáru:

- Pokud senzory na webu **zezelenají**, když jsou nad čárou → OK.
- Pokud se chovají **obráceně** (svítí nad bílou plochou), nastav
  v kódu `INVERT_SENSORS = true;` a nahraj znovu.

## Použití webového rozhraní

1. Po zapnutí robota se z mobilu/notebooku připoj na WiFi síť
   **`LineFollower`**, heslo **`lajnovac123`**.
2. V prohlížeči otevři **`http://192.168.4.1`**.
3. Uvidíš 5 senzorů (zelené = vidí čáru), aktuální error a posuvníky
   pro Kp, Ki, Kd a rychlost.
4. Polož robota na čáru a stiskni **START**.

## Ukládání nastavení (přežije restart)

Posuvníky mění hodnoty jen v paměti RAM - hned se projeví v jízdě,
ale po odpojení napájení by se ztratily. Když naladiš hodnoty, které
se ti líbí, stiskni na webu tlačítko **"Uložit nastavení natrvalo"**.
Hodnoty (Kp, Ki, Kd, rychlost) se uloží do trvalé paměti ESP32 (NVS /
Preferences) a po každém dalším restartu/zapnutí se automaticky
nahrají zpět místo výchozích hodnot z kódu.

Nešetři tlačítko na každou malé úpravu posuvníku - flash paměť má
životnost cca 100 000 zápisů, takže ulož až když jsi s nastavením
spokojený (ne po každém posunutí slideru).

## Ladění PID (doporučený postup)

1. Nastav `Ki = 0` a `Kd = 0`, `Kp` zkus kolem 15-20, rychlost nízkou
   (80-120).
2. Postupně zvyšuj **Kp**, dokud robot nesleduje čáru, ale trochu
   kmitá ze strany na stranu.
3. Přidávej **Kd** (např. o 2-5), dokud kmitání nezmizí a jízda je
   plynulá.
4. **Ki** většinou u line followeru není potřeba (nech na 0), případně
   jen malou hodnotu (0.1-0.5) pokud robot má trvalou odchylku od čáry
   na rovných úsecích.
5. Až bude jízda stabilní, postupně zvyšuj rychlost a znovu dolaď Kp/Kd
   - s vyšší rychlostí obvykle potřebuješ vyšší Kd.

## Pokud motor jede opačným směrem

Buď prohoď dva vodiče motoru na L298N (OUT1↔OUT2, nebo OUT3↔OUT4),
nebo v kódu ve funkci `setMotors()` prohoď `HIGH`/`LOW` u
příslušného IN1/IN2 nebo IN3/IN4.

## Možná rozšíření

- Přidat ultrazvukový senzor (HC-SR04) pro zastavení před překážkou.
- Připojit ESP32 do domácí WiFi (STA mód) místo AP, aby šel ovládat
  i mimo dosah jeho vlastní sítě.
- Přidat tlačítko/OLED displej přímo na robotovi pro rychlé zapnutí
  bez nutnosti webu.
