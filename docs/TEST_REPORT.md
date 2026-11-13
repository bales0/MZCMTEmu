# Ověření portu – 13. 11. 2026

**20 host sad PASS, cílový firmware build PASS, HW akceptace NEPROVEDENA.** Host testy ani úspěšný build neprokazují kompatibilitu se skutečným Sharpem. Přesný rozsah každé oblasti uvádí [PARITY_MATRIX](PARITY_MATRIX.md).

Repozitář při pokračování i po práci: main, HEAD 25246a327ed2187cd24e7d11b4cbf7c511555a1d. Žádný staging/commit/branch/push/PR. Referenční firmware 2.0.3, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb, zůstal beze změn; kontrola git status je čistá. MZTools a uživatelské binární fixture nebyly upravovány.

## Reprodukce

Windows, Python3.11 PlatformIO, host MinGW GCC5.1, cílový Xtensa GCC15.2, esptool4.11. Platforma je pinována na Espressif32@7.0.1 / ESP-IDF6.0.1.

    python tests/host/run_tests.py --cc C:/Users/alesb/.platformio/packages/toolchain-gccmingw32/bin/gcc.exe --tap-fixtures tests/TAP --qd-fixtures tests/QD --qd-fixtures tests/MZQ
    pio run -e MZCMTEmu -j 1
    python C:/Users/alesb/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 image_info .pio/build/MZCMTEmu/firmware.bin

Produkční C moduly se linkují jako samostatné překladové jednotky s -std=c11 -O2 -Wall -Wextra -Werror. Test nevkládá produkční .c přes include. Dočasné soubory jsou v izolovaných podadresářích .tmp, RECORDINGS_DIR je pro test přesměrován. Standardní shell runner měl Windows chybu740; použit child_process.execFile. Výsledky jsou [port-host.txt](validation/port-host.txt) a [port-build.txt](validation/port-build.txt).

## Host důkazy

| Sada | Co skutečně ověřuje |
|---|---|
| test_buttons | Produkční debounce/repeat/STOP priorita a tick wrap; GPIO a tick jsou mock |
| test_sdcard | Produkční lifecycle driver včetně všech init/mount/unmount/free selhání a retry; ESP SD API je mock |
| test_storage | Skutečná ESP_PLATFORM větev actoru, statický queue pump, vlastnictví native deskriptorů, byte cache, seek/ungetc/EOF, exclusive create/pool8, async slot, ISR refusal, read/write/seek/sync/close; skutečné soubory host FS a vložené IO chyby |
| test_raw_rmt | Produkční RMT encoder,16 MHz, jedno continuous transaction/refilly12000 událostí, long runs/UINT32, mixed level,1 tick, pause/stop/replay/underrun/init failure; mock simuluje DMA a retenci clock gate |
| test_player_rmt | Skutečný player + produkční RMT:17 Sharp režimů/simulovaný UL peer, TAP4 rychlosti, WAV a L16, přesný active tick total a user-pause precedence |
| test_capture_mcpwm | Produkční MCPWM driver: latched timestamp nezávislý na IRQ delay, wrap/multi-wrap, pending tail/pause/resume, polarity/overflow errors a init cleanup; mock simuluje hardware latch |
| test_loader_choice | Všech17 módů a per-family speed memory, validní MZ700/TC rychlosti |
| test_writer_io | Produkční WAV/LEP/L16/MZF writery; injected write/seek/flush/close chyby, odstranění provisional output, exclusive create, RIFF/FAT overflow, izolovaná MFI chyba |
| test_file_list |1000+ souborů, řazení, metadata filtr, parent/scroll, cancel/retry a dvě lifetime PSRAM alokace |
| test_autoname | Referenční Sharp/IC/TC/Sinclair/CPM detektory, čtyři AUX rychlosti, checksum a kolize; limity legacy WAV22k a LEP kvantizace |
| test_mzf_writer | NORMAL/IC/TC, native/copier normalizace, checksum/duplicate recovery,0/65535 B, incomplete/abort/delete |
| test_quickdisk | Logical/QDF/HxC/FlashFloppy, všech 16 MFM fází, CRC, bounded payload, špatné offsety/trailer/count a cancellation |
| test_route_switch |40 ms S1 debounce, safe initial EXT a čistá tabulka SENSE/monitor/UL ownership/pause/READY |
| test_settings | NVS schema v2/CRC/ranges, v1 migration, namespace, no rewrite unchanged a NVS fault cleanup |
| test_sharp_stream | Úplné waveform intervaly17 režimů, původní Z80 LOW/HIGH vectors, fallback/overlap, sidecary a UL deadline/bounds |
| test_player_sharp | Skutečný player s diagnostickým GPTimer HAL,17 režimů/UL, MZT selector/profily/MTI a návaznost včetně UL→UL |
| test_recorder | Skutečný recorder, quantization/round-trips, WAV fáze/RIFF, MOTOR/user/AUTO, MZF/AutoName, media removal/REC9999 a explicitně odložený finalize/cancel/removal |
| test_tap_stream |4 rychlosti a všechny intervaly, max65535 B, dlouhý čas, malformed input,2000 deterministických fuzz případů, cancel/retry a všechny dodané TAP |
| test_player_tap | Skutečný player + GPTimer driver, absolutní časy TAP/WAV4 sample rates, hystereze/invert/pause/EOF/underrun a32bit carry |

Golden loadery vznikly kompilací původních referenčních C++ generátorů přes [generate_loader_golden.py](../tests/host/generate_loader_golden.py) s host access shimem, nikoli přepisem očekávání podle portu. Kontroluje pevný referenční HEAD a čistotu zdrojů. Vektory pokrývají LOW/HIGH umístění, saved stack/relokátory a unsafe loader odmítnutí.

Sanitizery na tomto MinGW nejsou dostupné (-lasan/-lubsan chybí); fuzz PASS je běžný bounded test, ne ASan/UBSan důkaz. Mock fronta neběží v reálném FreeRTOS souběhu. Mock RMT retenci při zastavení zdrojových hodin předpokládá. UI obrazovky v main.c a samotný OLED nejsou celé spouštěné host testem. Windows CRT omezuje skutečné cesty nad MAX_PATH, přestože firmware buffery mají1024 B. Tyto limity zůstávají otevřené pro HW akceptaci.

## Dodané soubory

Všechny dodané TAP prošly celým waveformem ve všech čtyřech rychlostech. Před/po testech bylo shodných SHA256 všech 92 souborů ve složkách MZF/QD/MZQ/WAV/TAP/LEP, viz [manifest](validation/fixture-sha256.json).

QuickDisk přijal barbarian2.qd a barbarian2.qdf (každý 3 rekordy), FF_Flappy_UL800.qd a Flappy.qd (každý 1), qdtestbas.mzq (1). Odmítl FF_Blank.qd, FF_Blank_MZ_Formated.qd, FF_Blank_MZ_Formated_TC122.qd, hxc_Blank.QD, hxc_Blank_MZ_Formated.QD, hxc_Blank_MZ_Formated_TC122.QD, hxc_MarioMZ1500.qd, hxc_MarioMZ1500_MZ_Formatedandloaded.qd a mzfloader.mzq. Všech14 výsledků nyní souhlasí s původními validačními funkcemi reference. [compare_quickdisk_reference.py](../tests/host/compare_quickdisk_reference.py) vyjme nezměněné původní C++ funkce pro MFM decode, CRC/frame/count, logical parser a převod hlavičky, nahradí pouze SD přístup/cancel a porovná akceptaci i všechny kanonické hlavičky a payloady přijatých rekordů s portem. Kontroluje referenční HEAD a čistotu src. Physical/QDF cold-scan orchestrace je host harness podle reference; duration cache, AVR playback, UI a RTOS předmětem tohoto porovnání nejsou.

[Referenční výstup](validation/quickdisk-reference.txt): FF_Blank a FF_Blank_MZ_Formated i hxc_Blank_MZ_Formated mají nulový count; hxc_Blank nemá validní count/CRC; oba TC122 a oba Mario obrazy neprojdou frame/CRC/length validací; mzfloader.mzq neprojde logical hlavičkou. **Tato odmítnutí odpovídají validačním algoritmům reference, neznamenají obecně poškozený obraz pro jiný formát/přehrávač.**

Reprodukce nezávislého QD porovnání:

    python tests/host/compare_quickdisk_reference.py --cc C:/Users/alesb/.platformio/packages/toolchain-gccmingw32/bin/gcc.exe --cxx C:/Users/alesb/.platformio/packages/toolchain-gccmingw32/bin/g++.exe tests/QD tests/MZQ

## Cílový build a paměť

Finální build prošel s aktivním Werror. Statická RAM153372 B/327680 B (46,8 %), aplikace454617 B/1048576 B (43,4 %). PIO generický board popis uvádí N8; override i výsledný flash-size nibble4 znamenají16 MB. Octal PSRAM je zapnuta v sdkconfig; fyzický boot/velikost PSRAM nebyly ověřeny. Checksum a validation hash obrazu jsou validní: [image_info](validation/port-image.txt).

[Symboly](validation/port-symbols.txt) potvrzují encode/half_symbol/event_ticks/transmitted/on_capture v IRAM, RMT FIFO32768 B a MCPWM FIFO65536 B v interní DRAM. Storage cache pool8512 B, jeho stack8192 B a WAV render4096 B jsou interní. Staré raw_timer/raw_capture velké buffery ve výsledných aktivních symbolech nejsou. Runtime heap, SDK DMA buffery, task stacks a PSRAM pooly nejsou samotným PIO RAM procentem změřené. [Konfigurace cache-safe/backendů](validation/port-config.txt).

Při vývoji jednou paralelní -j2 kompilace SDK skončila interní chybou kompilátoru; sériové -j1 sestavení prošlo. Při finálním překladu byly opraveny dvě misleading-indentation chyby nového kódu; žádné warningy nebyly potlačovány. Starší logy v validation jsou historické a nejsou výsledkem této verze.

## HW akceptace, která dosud neproběhla

1. Uživatel potvrdil neinvertující U1/U2. Změřit napětí, DIR, napájení/power-off, idle/reset READ LOW/SENSE HIGH/MONITOR LOW a fyzickou trasu S1. Typy převodníků nejsou doloženy. Bez elektrického ověření nepřipojovat Sharp.
2. Boot N16R8, skutečná8 MiB PSRAM, OLED layout/jas/keymap a NVS restart. Reálné debounce/chords, krátký/dlouhý STOP a držená klávesa při změně obrazovky.
3. Analyzátor na READ/WRITE/MOTOR/SENSE: všech 13 timing profilů,17 režimů a4 TAP rychlostí. Dlouhé leader/runy, přesná polarita, loadery/checksum, hranice MZT/QD, DMA refill bez idle/glitch.
4. Pauza RMT uprostřed symbolu,5 s držení a resume; STOP/UL převzetí GPIO. Ověřit clock-gate retenci a konzervativní uvolňování source slotů proti skutečnému času. Monitor mute hranice a latenci změřit zvlášť.
5. MCPWM first edge, pending edge + pause tail, obě polarity, dlouhý idle/wrap, cache stalls a přetížení. Prokázat nulové tiché ztráty hran a správné overflow errors.
6. Sharp MZ-700/MZ-800 LOAD/SAVE ve všech režimech, UL handshake a timeouty. Záznam/playback round-trip s SD2CMT2 a MZTools, více generací LEP/L16, WAV22/44k a MZF native/copier.
7. Vyjmutí SD při PLAY/RECORD/SAVING, write failure, readonly/full disk, reinsert/retry a jiná karta. Ověřit zavření handle, žádný falešný SAVED, manual recovery selektoru a zachování existujících souborů.
8. Hodinový souběh SD/OLED, rychlé1:4 a navazující hlavičky: RX/TX watermarks, SD worst latency, internal heap minimum/largest block, PSRAM a stack high-water; žádný restart, glitch, silent drop ani rostoucí fragmentace.

Teprve po doplnění těchto měření může být časově kritická oblast HW-TESTED a celé zadání akceptované.

## OLED audit 13. 11. 2026

Nový test_oled_ui linkuje skutečné ui.c a oled.c. Ověřuje page mode, adresovací/data pakety, limit 20 Hz, dirty pages,21znakový výřez, jas a retry po command/data failure. Missing OLED ukončí frame prvním 10 ms timeoutem. UI cache se aktualizuje jen pro skutečně odeslané stránky, takže chyba I²C neztrácí další pokus. Oprava sjednocuje inicializaci20h/02h s B0h/column příkazy: viz výrobní [SSD1306 datasheet Solomon Systech, §9/10.1.3 a10.1.13](https://tropratik.fr/wp-content/uploads/2021/03/SSD1306-SolomonSystech.pdf#page=33). Vizuální vzhled a skutečné I²C časování stále nejsou HW-TESTED.

Uživatel potvrdil13. 11. 2026, že HW zatím není připojený. Žádný flash ani fyzický test proto nebyl proveden.
