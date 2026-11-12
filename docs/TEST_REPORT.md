# Protokol ověření 12. 11. 2026

**Výsledek: oprava rekordéru a doplněný ZX TAP prošly host testy a firmware buildem. Kompletní port nebyl dokončen. Žádný HW test nebyl proveden.** Větev port/esp32s3-parity, výchozí main 5e654f1683c0436f5f8cb3853ece24e8c618dbf7. Referenční repozitář 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb (2.0.3) byl čten lokálně a nebyl měněn.

## Prostředí a příkazy

Windows, Python 3.11 prostředí PlatformIO, MinGW-W64 GCC 5.1.0 (i686-posix-dwarf-rev0), Espressif32 7.0.1, ESP-IDF 6.0.1 (framework-espidf 4.60001.0), Xtensa GCC 15.2.0+20251204, esptool 4.11.0. Platforma v platformio.ini zatím není verzovaná. Příkazy z kořene repozitáře v PowerShellu:

    & C:/Users/alesb/.platformio/penv/Scripts/python.exe tests/host/run_tests.py --cc C:/Users/alesb/.platformio/packages/toolchain-gccmingw32/bin/gcc.exe
    & C:/Users/alesb/.platformio/penv/Scripts/pio.exe run -e MZCMTEmu -j 2
    & C:/Users/alesb/.platformio/penv/Scripts/python.exe C:/Users/alesb/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 image_info .pio/build/MZCMTEmu/firmware.bin

Použit byl náhradní Node procesní runner, protože standardní shell runner selhával Windows chybou 740. Nebyla požadována ani provedena eskalace oprávnění. MinGW bin adresář musí být na PATH také během kompilace; host runner to nastavuje pouze svým potomkům.

## Provedené testy

Test [test_recorder.c](../tests/host/test_recorder.c) linkuje samostatné produkční překladové jednotky recorder.c, edge_writer.c, edge_stream.c, wav_writer.c. Nenatahuje jejich implementace přes include. Jen capture periferie a RTOS čas jsou nahrazené. RECORDINGS_DIR lze při host sestavení přesměrovat do izolovaného dočasného adresáře; firmware má stále /sd/RECORDINGS.

| Kontrola | Výsledek | Rozsah důkazu |
|---|---|---|
| Historie hran 1/24/74/74 µs LEP, 1/7/23/23 µs L16 | PASS | Nezávislé minimální délky; starý residual tuto kontrolu poruší |
| Polovina jednotky, 126/127/128/254/255 jednotek | PASS | Nezávislé zaokrouhlení a signed remainder/zero extension |
| Dlouhý idle 1 s a UINT32_MAX µs pro obě úrovně | PASS | Bez overflow součtu při zaokrouhlení; správná souborová délka/extension |
| Vstupy odvozené z 13 statických timing profilů, osm dalších generací | PASS | Stejné kvantizované HIGH/LOW šířky po dalších záznamech; nejde o golden loader waveform |
| Playback parser po zápisu | PASS | Stejné úrovně a délky, předběh 300 ms, tail 500 ms; není to HW TX |
| WAV 44100 Hz, 10000 proměnlivých intervalů | PASS | 44210 PCM vzorků, jejich úrovně a hranice, RIFF/data délky |
| WAV 22050 Hz, stejné intervaly | PASS | 22105 PCM vzorků; zachovaná absolutní fáze |
| User pause proti MOTOR resume, WAIT MOTOR, cancel/delete | PASS | Veřejný recorder state machine s mock capture |
| Hlášený capture overrun a AUTO 5s timeout | PASS | Chyba zastaví záznam a odstraní otevřený soubor; mock dodává příznak overrun |
| Negativní kontrola původního recorder.c | FAIL očekávaně | Exit 3, assertion fgetc(f) == expected v on-disk oracle; upravena jen host cesta RECORDINGS_DIR |
| Firmware build po opravě flash deklarace | PASS | 174,56 s úplné přegenerované sestavení; shrnutí viz níže |
| ESP image checksum/hash | PASS | esptool image_info; flash size nibble 4 = 16 MB |

[Surový host výstup](validation/host-recorder.txt) a [shrnutí buildu](validation/firmware-build.txt) jsou malé textové důkazy. Kompletní lokální build log je .tmp/port-build.log a není verzovaný. Host testy používají generované vstupy a nedotýkají se uživatelských fixture tests/MZF, tests/QD ani tests/WAV.

Build hlásí RAM 195568 / 327680 B (59,7 %) a aplikaci 286537 / 1048576 B (27,3 %). Aplikační partition 1 MB neznamená 1 MB fyzické flash. Původní build používal --flash_size 8MB a hlásil konflikt s 16MB sdkconfig. Po board_upload.flash_size = 16MB se obraz generuje jako 16MB a konflikt zmizel. Velikost bin souboru byla 286960 B. Závěrečný incremental build po kontrole formátování zdroje také prošel (6,40 s), se stejnou spotřebou RAM a flash. To neprokazuje skutečnou velikost pamětí na desce.

## Neprovedené a zbývající kontroly

Pokus o stejný host příkaz s --sanitize skončil při linkování: dostupné MinGW nemá -lasan ani -lubsan. Sanitizer test není PASS. Runner přepínač podporuje; použít podporovaný GCC/Clang host a potom doplnit cílený fuzz parserů. V původním kroku rekordéru nebyl fuzz spuštěn. Navazující TAP krok níže obsahuje 2000 deterministických generovaných případů bez sanitizérů.

Neproběhly golden waveform testy všech 17 loader modes, MZF dekodéru, parserů MZT/QD/sidecars ani NVS migrací. Jejich porty chybí. TAP je popsán v navazujícím ověření níže. Host testy neověřují ISR latenci, fronty při SD/OLED souběhu, ukládání pause tail ve skutečném capture, reentrant stop/start nebo SD removal a obnovu. Skutečný souborový systém FAT a write-error injection nejsou simulované.

HW akceptace vyžaduje ověřit typy a napájení U1/U2, úrovně GPIO4/5/6/7, polaritu a reset; poté analyzátorové měření časů, jitteru a návaznosti TX, RX peak queue při 1:4 leaderu, souběh SD/OLED/PSRAM, funkční LOAD na MZ-800/MZ-700 včetně IC/TC/UL a opakovaný RECORD→PLAY→RECORD. Dále MOTOR/user pause, STOP save/cancel, SD removal/reinit a nejméně hodinový heap/stack test. Žádná hodnota jitteru, SD latence, heap minima nebo HW peak queue v tomto reportu není změřena.

## Navazující implementace ZX TAP bez commitů

Výchozí HEAD tohoto pokračování je 85558bcf071bb9d3096555dfec4abda87d4999f8. Pracovní změny byly provedeny na výslovný pokyn „proveď změny dle zadání“. Nebyl vytvořen commit, nová větev, push ani PR. Původní výsledky rekordéru výše zůstávají historickým protokolem; aktuální stav je tento:

    & C:/Users/alesb/.platformio/penv/Scripts/python.exe tests/host/run_tests.py --cc C:/Users/alesb/.platformio/packages/toolchain-gccmingw32/bin/gcc.exe --tap-fixtures tests/TAP
    & C:/Users/alesb/.platformio/penv/Scripts/pio.exe run -e MZCMTEmu -j 2

Host runner má tři oddělené malé překladové celky a překládá s -std=c11 -O2 -g -Wall -Wextra -Werror. --tap-fixtures je volitelný read-only adresář; syntetické testy fungují bez něj. Soubory tests/TAP nejsou přidávané ani přepisované touto implementací. SHA256 po ověření jsou v [manifestu](validation/tap-fixtures.txt).

| Kontrola | Výsledek | Důkaz |
|---|---|---|
| Původní recorder regrese | PASS | Všechny původní testy LEP/L16/WAV a recorder stavů |
| TAP golden ve 4 rychlostech | PASS | Přesně 23855 intervalů pro header/data/header; LOW/HIGH, asymetrický sync, MSB-first, terminator a obě mezery; součet ticks se rovná metadatům |
| TAP vstupní validace | PASS | Prázdný soubor, chybné délky/flagy, zkrácení včetně pozdějšího bloku, neplatná rychlost, chybějící soubor, cleanup |
| Maximální blok a dlouhá páska | PASS | 65535 B přehráno celé; metadata pro 1000 drobných bloků přesahují 32bit µs čas bez overflow |
| Generovaný parser fuzz | PASS bez sanitizérů | 2000 deterministických platných/poškozených případů do 64 B, bounded dokončení |
| Read-only uživatelské vzorky | PASS | Boovie.tap (8 bloků), Bubl-128.tap (5), Circlelu.tap (4), Rapid128.tap (7), Xfiles.tap (4), každý při 1400/1800/2100/2500 Bd; všechny intervaly a součty časů |
| Celá browser/player/TAP/raw_pulse cesta | PASS v host simulaci | Skutečný browser, player a GPTimer ISR, mock jen periferie/RTOS a nepoužité výstupy; porovnání každé naplánované hrany, inverze a absolutního času |
| Transport a průběh | PASS v host simulaci | Prefill zůstává 0 %, 100 % až po posledním terminatoru; MOTOR pauza po source EOF, user pause priorita, změna zvolené rychlosti nemění aktivní přehrávání, STOP/replay/underrun/error |
| Dokončený čas v raw_pulse | PASS v host simulaci | Carry přes UINT32_MAX, reset session a částečně odehraný 100µs interval: pause po 37 µs, resume se zbývajícími 63 µs |
| Negativní kontrola původní MOTOR EOF větve | FAIL očekávaně | Do dočasné kopie player.c byla vrácena pouze původní větev; test selže při očekávání PAUSED, exit 3 |
| Firmware ESP-IDF 6.0.1 / Espressif32 7.0.1 | PASS | Poslední build 6,55 s, RAM 195696 B, app flash 289305 B, image 16 MB |

Výstupy: [host testy](validation/tap-host.txt), [build](validation/tap-build.txt), [negativní kontrola](validation/tap-negative-control.txt). První přegenerování SDK překročilo timeout nástroje 180 s; navazující build dokončil chybějící objekty a finální incremental build prošel. Nenastala chyba kompilace nového firmwaru.

TAP parser přenáší i původní checksum bajty; stejně jako reference netestuje jejich XOR hodnotu. Podporuje pouze referenční flagy 0x00 a 0xFF, minimálně 2 B obsahu bloku. Celý soubor se validuje a spočítá před startem, poté se streamuje. Velký kontejner může před startem zdržet UI; není implementované rušení této analýzy. Žádná nová fronta nebo per-pulse alokace nepřibyla. RAM je proti předchozímu buildu vyšší o 128 B, flash aplikace o 2768 B.

Softwarový převod absolutních hran z 16MHz ticks na 1µs HAL má matematickou chybu nejvýše ±0,5 µs bez driftu. Test měří naplánované alarmy na mock timeru, nikoli fyzický jitter. GPTimer ISR latence, souběh na obou jádrech/SD/OLED a funkční LOAD do MZ stále vyžadují HW test. Sanitizéry zde stále nejsou dostupné. Volba TAP SPEED zatím není uložená do NVS a OLED menu bylo zkontrolováno kódem/buildem, nikoli na displeji. WAIT MOTOR zatím neukazuje vypočtenou TAP délku před skutečnou přípravou zdroje.
