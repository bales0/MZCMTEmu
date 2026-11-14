# Kontrola pinoutu a referenčního ovládání

Kontrola13. 11. 2026: skutečný zdrojový kód všech aktivních i diagnostických backendů odpovídá pinové tabulce zadání z commitu d3f67c4, původnímu MZCMTEmu.docx a vodičům v PCB/MZCMTEmu.pdf. Nebyla nalezena odchylka GPIO a žádné GPIO se nepřesouvalo. [Strojová kontrola definic](validation/reference-fix-pinmap.txt). Jde o kontrolu zdrojů/schématu, nikoli proměření osazené desky.

| Funkce | GPIO firmware / zadání | Pad modulu IO1 ve schématu | Směr / stav |
|---|---:|---:|---|
| READ |4|4|OUT; RMT TX i GPTimer fallback4, idle LOW |
| WRITE |5|5|IN; MCPWM, GPIO fallback, volitelný PCNT a UL5 |
| MOTOR |6|6|IN pull-up; ON=HIGH dle posledního potvrzení uživatele |
| SENSE |7|7|OUT; idle HIGH, řízené PLAY/RECORD/UL |
| PLAY |8|12|IN pull-up, tlačítko proti GND |
| STOP |9|17|IN pull-up, tlačítko proti GND |
| NEXT/FFWD |10|18|IN pull-up, tlačítko proti GND |
| PREV/REWIND |11|19|IN pull-up, tlačítko proti GND |
| REC |42|35|IN pull-up, tlačítko proti GND |
| SD CS |13|21|OUT SPI2 |
| SD SCK |14|22|OUT SPI2 |
| SD MOSI/CMD |15|8|OUT SPI2 |
| SD MISO/DAT0 |16|9|IN SPI2 |
| SD detect |47|24|IN pull-up, vloženo LOW |
| OLED SDA |17|10|I²C0,400 kHz |
| OLED SCL |18|11|I²C0,400 kHz |
| S1 SWITCH |21|23|IN pull-up; LOW=EXT,40ms debounce; pouze snímání kontaktu |
| Monitor |41|34|OUT enable/mute PAM8302A SD; HIGH enable |
| USB D− |19|13|Nativní USB, aplikace nepřemapovává |
| USB D+ |20|14|Nativní USB, aplikace nepřemapovává |

Čísla GPIO nejsou pořadová čísla na konektoru DevKitu ani čísla padů modulu. Například tlačítko REC je GPIO42, ale v symbolu IO1 má pad35. Nepoužívá se pomocný GPIO12 ani PSRAM GPIO35–37. USB konzole zůstává na původním USB Serial/JTAG. DOCX je původní návrh: jmenuje I2S/PWM, současný port používá stejné4/5 pro RMT/MCPWM a41 pro analogový monitor enable podle pozdějšího schématu; to není změna pinoutu.

## Referenční ovládání

Read-only reference600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb: ui/browser.cpp RIGHT_LONG → RECORD_MENU_REQUESTED, SELECT_LONG → PLAY_MENU_REQUESTED, LEFT_LONG v rootu → SETTINGS. ui/record_menu.cpp má TYPE/CONTROL/AUTONAME a vrací se přímo do browseru. PLAY menu má INVERT/PLAY CONTROL/LOADER/SPEED/TAP SPEED; RECORD položky do něj nepatří. Systémové CMT/brightness/ABOUT jsou oddělené. KEYMAP je navíc ESP32 OLED informace.

Nová mapa: krátký PLAY/REC až při uvolnění, dlouhý PLAY/REC700 ms, STOP long800 ms zachován z dosavadního ESP32 ovládání. NEXT/PREV repeat beze změny, STOP má prioritu. Dlouhé menu stisky platí ve filebrowseru, nikoli během transportu nebo v QD selektoru; z něj nejprve krátký STOP do browseru. REC short v QD selektoru dál mění profil rekordu. Dlouhý REC při chybějící SD vyžádá retry jako reference. S1 EXT dovoluje procházení/menu, blokuje interní start.

## QD první průchod

Reference play/mzf_playback.cpp při přípravě fyzického QD nastaví duration_deferred a úplný průchod pro délku provede až v mzf_playback_start. Předchozí oprava portu ponechávala tento průchod při první návštěvě každého rekordu; osmipoložková cache zrychlila pouze opakovanou návštěvu. Nyní sharp_stream_preview připraví kanonickou hlavičku/loader a nejvýše90 B metadata payloadu, ale nevytváří průběh celého rekordu. Otevřený QD index se používá už při prvním NEXT/PREV. Přesná délka se počítá při PLAY pouze pro skutečně spuštěný rekord; do té doby je total_ticks neznámé0 s explicitním duration_deferred příznakem a READY progress/elapsed0. První otevření celého QD stále validuje obraz; další zvolený rekord se znovu neskenuje. STOP/SD removal/error cache invalidují.

Testy kontrolují první návštěvu druhého rekordu s nezvýšeným analysis counterem, nikoli jen druhé kolo cache. Nové menu/input testy ověřují oddělení tří nabídek, dlouhé REC/PLAY bez předčasného startu, nepřítomnost krátké události po long release, STOP chord, startup-held a tick wrap. Přesnost všech17 waveformů kontrolují existující golden testy. Výsledky jsou v validation/reference-fix-*.

HW potvrzení nového obrazu, pinová kontinuita a elektrické úrovně stále vyžadují skutečnou desku. Tento zásah neflashuje ani nemění referenční repozitář.

## Finální ověření

24 sad GCC14.2 ASan/UBSan PASS (114,97 s), včetně původních20 transportních/parserových sad, vstupních long/short testů a první QD návštěvy. [Host log](validation/reference-fix-host-linux.txt), [cílový build](validation/reference-fix-build.txt), [image_info](validation/reference-fix-image.txt), [souhrn/hash](validation/reference-fix-summary.json). Cílový ESP-IDF6.0.1 build prošel bez nových warningů. RAM155516 B, aplikace464705 B, image465136 B. Všech92 uživatelských fixture SHA256 shodných. Žádný flash/commit/nová větev/push; index je prázdný.

Nový obraz poznat v COM6/115200 podle banneru MZCMTEmu REF-MENU-QD r2 2026-11-13. GPIO a časování fyzické desky tímto nebyly proměřeny.
