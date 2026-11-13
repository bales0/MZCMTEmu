# Předání portu ESP32-S3 – 13. 11. 2026

Uživatel autorizoval pracovní změny a žádá pokračovat do dokončení. **Bez samostatného souhlasu žádný commit, staging, push, PR ani nové větve.** Checkout zůstává main, HEAD 25246a327ed2187cd24e7d11b4cbf7c511555a1d. V tomto pokračování neproběhla žádná uvedená Git operace. Referenční MZ-SD2CMT2-Reborn600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb (2.0.3) a MZTools jsou read-only.

## Aktuální výsledek

Implementované funkce a architekturu popisuje [README](README.md), podrobně [PARITY_MATRIX](docs/PARITY_MATRIX.md) a [TIMING_PORT](docs/TIMING_PORT.md). Port má všech 17 Sharp režimů/loadery/UL, MZT/QD selektor a MZT návaznost, sidecary/AUTO, TAP4rychlosti, společný WAV/edge/TAP/Sharp RMT DMA, MCPWM HW capture, MZF/WAV/LEP/L16 RECORD, AutoName Sharp/Sinclair/CPM, jednorázové PSRAM pooly, SD actor/cache, asynchronní finalize, systémové menu/NVSv2 a odrušená tlačítka s prioritou STOP.

Při poslední kontrole prošlo 20 host sad a cílový build ESP-IDF6.0.1, Espressif32@7.0.1. RAM153372 B, aplikace454617 B, image16MB. Přesný rozsah a logy viz [TEST_REPORT](docs/TEST_REPORT.md). Všech92 uživatelských binárních fixture mělo před/po testu stejné SHA256. .gitignore zpřístupňuje pouze tests/host, binární fixture zůstávají ignorované.

## Co ještě není akceptované

- Žádná deska nebyla flashována a nebyl proveden HW test. Uživatel potvrdil **neinvertující U1/U2**; jejich typy a elektrické parametry nejsou potvrzené. Uživatel 13. 11. potvrdil, že HW zatím není připojený. Podle zadání nejprve změřit napětí, DIR/reset/power-off a safe idle. Pak teprve testovat Sharp LOAD/SAVE a UL.
- RMT source-clock pause musí prokázat retenci rozpracovaného symbolu/DMA na S3; host mock to předpokládá a ověřuje protokol kolem tohoto předpokladu. Retired-slot clock je konzervativní wall-time odhad (+10 µs guard), ne hardwarový start timestamp. HW musí potvrdit kontinuitu a zátěžové rezervy.
- MCPWM první hrana, pause tail, cache stalls a nemožnost tiché ztráty hran vyžadují analyzátor. Skutečný FreeRTOS souběh, FAT removal/retry, OLED layout/latence a hodinový heap/stack/PSRAM stress jsou otevřené.
- QuickDisk přijal5 ze14 obrazů. Nový compare_quickdisk_reference.py nezávisle zkompiloval původní validační funkce a doložil shodu všech 14 výsledků včetně9 odmítnutí; u přijatých porovnal každou kanonickou hlavičku a payload. To ověřuje parserové jádro, nikoli AVR duration cache/playback nebo reálný LOAD. Log quickdisk-reference.txt.
- Windows MinGW MAX_PATH omezuje skutečné long-path host fixture;1024 B FAT cesty ověřit na desce. Host sanitizery nejsou dostupné. Žádný z těchto bodů nezakrývat tvrzením HW-TESTED nebo hotovou akceptací.

## Prostředí a příkazy

Windows; standardní shell runner dříve selhával740, funguje child_process.execFile přes mcp__node_repl__js. CWD C:/Dokumenty/GitHub/MZCMTEmu. Python/pio v C:/Users/alesb/.platformio/penv/Scripts, host GCC v packages/toolchain-gccmingw32/bin/gcc.exe (5.1), SDK XtensaGCC15.2. Běžný incremental build kolem7–23 s, regenerace celého SDK několik minut; používat -j1. Jednou -j2 selhalo interní chybou SDK kompilátoru, sériový retry prošel. Nikdy nepouštět dva buildy současně.

    pio run -e MZCMTEmu -j 1
    python tests/host/run_tests.py --cc C:/Users/alesb/.platformio/packages/toolchain-gccmingw32/bin/gcc.exe --tap-fixtures tests/TAP --qd-fixtures tests/QD --qd-fixtures tests/MZQ

Host testy jsou samostatné překladové jednotky. Periferie/RTOS jsou mockované; test_storage navíc kompiluje skutečnou ESP_PLATFORM větev actoru se statickou simulovanou frontou a test_recorder má explicitně odložený finalize. Surové výsledky v docs/validation/port-*. Starší soubory validation a počáteční inventura jsou historické, nikoli aktuální stav.
