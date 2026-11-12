# Předání rozpracovaného portu ESP32-S3

Aktuální stav 12. 11. 2026: doplněný ZX TAP je připojený do browseru, playeru, OLED menu a stávající pulzní ISR. Golden testy, integrační testy se skutečným playerem/driverem a firmware build prošly. **Plná parita ani celé kroky A/B nejsou dokončené a žádný HW test neproběhl.** Toto je ověřený dílčí zásah podle §9 zadání, nikoli vydání kompletního portu.

## Souhlas a pracovní stav

Uživatel výslovně zakázal změny a commity bez souhlasu a vytváření větví na GitHubu. Následný pokyn „proveď změny dle zadání“ schvaluje pracovní úpravy v repozitáři. **V tomto pokračování nebyl proveden git add, commit, vytvoření větve, push ani PR.** Pro případný budoucí commit je nutný samostatný výslovný souhlas. Pokračovat v pracovních souborech bez další větve; nevracet nebo nepřepisovat uživatelské změny.

Repozitář už byl na lokální větvi port/esp32s3-parity; aktuální výchozí commit tohoto pokračování je 85558bcf071bb9d3096555dfec4abda87d4999f8 (backup, transport from sd2cmt2). Předchozí historické commity 65ec964 (kvantizace), f2df9d8 (flash) a b1ce07b (dokumentace) vznikly v předchozím běhu a tento běh je neměnil. Na začátku tohoto pokračování byl git status čistý. Během práce přibyly neversionované uživatelské testy tests/LEP, tests/MZQ a tests/TAP; ty se nesmí přepisovat ani automaticky zahrnovat do commitů. Pět TAP souborů bylo pouze čteno jako volitelné fixture. Nové host testy jsou oddělené v tests/host.

Referenční MZ-SD2CMT2-Reborn v sousedním adresáři je read-only, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb (2.0.3). MZTools se nemění. Pinmap a PCB se nemění.

## Nové pracovní změny

- src/audio/tap_stream.c/.h: samostatný TAP parser/pulse source. Reference play/tap_playback.cpp a tap_speed.cpp, GPLv3 atribuce v modulu. Jediná tabulka 1400/1800/2100/2500 Bd v přesných 16MHz ticks; pilot/sync/MSB-first data/terminator/header a data gap. Flagy jen 0x00/0xFF, délka minimálně 2 B včetně flagu a checksumu. Kontrola celého kontejneru před startem, streamovaný payload bez celé image v RAM. XOR checksum se stejně jako v referenci pouze přenáší.
- src/audio/player.c/.h: TAP je skutečný pulzní zdroj včetně stop/error/EOF a pause. Převod přes absolutní časy hran zabraňuje akumulaci chyby na současném 1µs HAL. Aktivní rychlost je oddělená od další volby v menu. Čas/průběh používají dokončené výstupní intervaly.
- Opravena předčasná EOF větev při MOTOR pause: vyčtený zdroj není dokončený fyzický výstup. Nový start z PAUSED nejprve ukončí starou session a její periferní cestu.
- src/audio/raw_pulse.c/.h: 64bit dokončený čas publikovaný dvěma 32bit částmi a sekvenčním čítačem. Bez nových 64bit atomik, RMW helperů v čítači, alokace nebo fronty. Writer je ISR nebo init po shutdownu; čtení dočkává konzistentní snapshot. Původní GPTimer backend zůstává.
- src/file_list.c: case-insensitive TAP přípona v browseru. src/main.c: šestá volba TAP SPEED a aktivní TAP baud v PLAY obrazovce. CMake překládá nový modul.
- tests/host/test_tap_stream.c: úplné golden pulzy pro všechny rychlosti, chybné soubory, maximální blok, 64bit celkový čas, 2000 generovaných parser případů. Volitelně čte externí TAP fixture.
- tests/host/test_player_tap.c + malé stubs: linkuje skutečný browser/player/TAP/raw_pulse ISR s mock periferií/RTOS. Porovnává každou naplánovanou hranu, inverzi, průběh, finální terminator, pauzy, replay, skutečný FIFO underrun, clock carry a partial-pulse resume.
- tests/host/run_tests.py: tři oddělené malé sady, přepínač --tap-fixtures. Výchozí testy jsou nezávislé na uživatelských binárních datech. README, matice, timing/report a textové výstupy aktualizované.

Starší oprava recorder.c bez residualu a board_upload.flash_size = 16MB zůstávají funkční. Původní recorder regrese prošla i po tomto zásahu. Nebyly odstraněny ani nahrazeny staré realtime backendy; odstraněná EOF větev playeru byla chybná, nešlo o náhradu celé architektury.

## Ověření

Příkazy a výsledky jsou v docs/TEST_REPORT.md, aktuální soubory docs/validation/tap-*.txt. Host command lze spustit bez --tap-fixtures. S přepínačem tests/TAP prošlo všech pět dodaných TAP souborů při všech čtyřech rychlostech; SHA256 po ověření je v manifestu.

Finální ESP-IDF 6.0.1 / Espressif32 7.0.1 build prošel (6,55 s incremental). RAM 195696 B (+128 proti předchozímu zásahu), app flash 289305 B (+2768), obraz pro 16MB flash. Nejprve bylo třeba po přegenerování SDK dokončit build přerušený timeoutem nástroje 180 s. Žádná chyba nového zdroje firmware z toho neplyne.

Negativní kontrola dočasně vrácené původní MOTOR EOF větve selže při očekávání PAUSED, exit 3; opravená produkce prochází. Testy mockují periferii, nikoli produkční ISR; nedokazují skutečnou latenci/jitter, souběh jader nebo LOAD do Sharpu. Sanitizéry chybí lokálnímu MinGW (asan/ubsan), takže fuzz je bez nich.

## Otevřené limity a další pořadí

1. Dokončit sémantický audit A. docs/SOURCE_INVENTORY.md je úplný původní seznam 98 referenčních souborů, nikoli důkaz sémantické parity. Zachovat licence každého přebíraného algoritmu. TAP je nyní jediná další portovaná oblast, většina matice zůstává BLOCKED.
2. B: sdílený transport/session a jediný vlastník SD/TX/RX, audit interní paměti a stacků. RMT TX DMA zůstává neimplementované; raw_pulse_set_rmt_enabled je no-op. edge_capture_rmt je stále GPIO ISR/GPTimer obálka. Posoudit MCPWM/RMT capture včetně wrap, ticha, první hrany a stop/pause snapshotu. Nové buffery nepřičítat ke starým frontám.
3. C: jediná referenční tabulka MZ profilů, všech 17 loader modes, golden fyzické přechody a inverze, MZF/MZT/M12, per-record selector a MFI/M2I/MTI, skutečný AUTO reset, QuickDisk, UL vlastnictví a timeouty. TAP zdroj si ponechává přesné ticks pro budoucí RMT HAL; dnešní ±0,5µs matematická konverze neomezuje ISR jitter.
4. D: MZF decoder s checksum/duplicate recovery, bounded AutoName a klasifikace; capture a zápis mají přednost před sekundární analýzou. Dřívější otázka pause tail v recorder capture při stejné úrovni zůstává otevřená; test recorder mock neprodukuje skutečný HW pause tail.
5. E: robustní SD reinit a chyby record→browser, kolize/rename, NVS schéma/migrace, úplná OLED keymap/EXT stav. TAP rychlost se zatím neukládá do NVS. WAIT MOTOR bez přípravy TAP neukazuje celkový čas. Předstartovní TAP scan může na velkém souboru zdržet UI a nemá cancel; logické formáty zůstanou validované.
6. F: sanitizer/fuzz i pro ostatní parsery, reentrant eventy, SD removal, skutečný HW protokol podle zadání. U1/U2 typy nejsou ve schématu uvedené; změřit 3,3/5V úrovně a resetovou polaritu. Není prokázaná nutná změna PCB.

Backend GPTimer není vyhovující finální náhrada požadovaného DMA transportu. TAP TESTED v matici je host úroveň, nikoli HW-TESTED. Volba rychlosti a informace na OLED jsou sestavené, ale nebyly vizuálně ověřené na fyzickém displeji. Během 1s idle se dokončený čas publikuje po konci intervalu. NVS, hardware a zbytek parity brání označení celého portu za hotový.

## Prostředí

Standardní exec shell v předchozím běhu selhával Windows chybou 740. Funguje Node child_process.execFile se skrytým oknem a explicitními cestami PlatformIO/Python. Proces buildu je nutné vyčkat, nespouštět souběžná sestavení nad stejnou .pio složkou. Změny jsou pouze pracovní; případné git operace vyžadují samostatný souhlas uživatele.
