> Aktuální r15 (14. 11. 2026): [UL, UL700 a UL800 podle SD2CMT2](docs/UL_FAMILY_PERFORMANCE.md). Odstraněné 10ms čekání po každé krátké dávce UL, vypnutý detailní trace, účinné -O2. Všechny tři režimy prošly host přenosem 44 033 bajtů; Z80 přijímače LOW/HIGH a 100Hz plánování PASS, cílový build PASS. Uživatel potvrdil LOAD v r14 UL800 s nízkou rychlostí; r15 a UL700 zatím bez HW potvrzení. S1 bypass zůstává zapnutý. Následující starší záznamy jsou historie.

# Nejnovější oprava reference a pinoutu

Main f5cda0fa04566e4008ef40ab0c9c49480316ddac + pracovní změny. Bez commitu/větve/stagingu/push/flash. Aktuální boot banner REF-MENU-QD r2. Uživatel potvrdil MOTOR ON=HIGH a COM6, původní LOW opraveno.

Dlouhé REC/PLAY700ms jsou samostatné události driveru; krátké akce až po release, žádný start transportu před dlouhým menu stiskem. Z filebrowseru REC long → RECORD MENU, PLAY long → PLAY MENU, STOP long → SETTINGS. Tři nabídky jsou nezávislé; RECORD není pod PLAY. STOP z menu ukládá a vrací browser, info má vlastní návrat. V QD selektoru jsou dlouhé menu stisky inertní jako v referenci; STOP do browseru a potom long REC. RECORD formáty a MOTOR→AUTO→MANUAL pořadí podle reference.

První NEXT/PREV QD už nepočítá celý waveform. sharp_stream_preview čte validovaný header/loader metadata s bounded90B prefixem, duration_deferred označuje neznámý čas. Exact duration až při skutečném PLAY; index read-only session platí pouze do STOP/removal/error. READY elapsed/progress0. První otevření obrazu zůstává plná validace. Původní oprava RMT neplatného GPIO−1 je zachovaná.

Pinout všech backendů zkontrolován proti tabulce zadání (git d3f67c4), originálnímu DOCX a PDF vodičům. Žádná odchylka GPIO. REC GPIO42 je pad35 modulu IO1, ne fyzický pin42. Úplná tabulka a metodika: [PINOUT_REFERENCE_AUDIT](docs/PINOUT_REFERENCE_AUDIT.md). Jde o statickou kontrolu, nikoli proměření desky.

24 sad GCC14.2 ASan/UBSan PASS, target ESP-IDF6.0.1 PASS bez nových warningů. RAM155516 B, aplikace464705 B, image465136 B;92 fixture SHA256 nezměněno. [Aktuální host](docs/validation/reference-fix-host-linux.txt), [build](docs/validation/reference-fix-build.txt), [image/hash](docs/validation/reference-fix-summary.json), [pinmap](docs/validation/reference-fix-pinmap.txt). Reference600a788/MZTools neměněny, uživatelské .gitignore a odstranění zadání zachovány.

Nový obraz není flashován/akceptován na HW. Je nutné nové ovládání/LOAD/SAVE/napětí/reset/časování ověřit na COM6 a analyzátorem; žádný host test to nenahrazuje. Starší předání níže popisuje předchozí řešení a jeho menu/cache pravidla jsou překonaná.

---

# Předání opravy PLAY — 13. 11. 2026

Main HEAD f5cda0fa04566e4008ef40ab0c9c49480316ddac + pracovní změny. Bez stagingu/commitu/větve/push/PR. Uživatelské .gitignore a odstraněné minulého zadání zachovány. Reference600a788 a MZTools neměněny.

Uživatel hlásí chyby běžícího zařízení. **Poslední oprava: MOTOR ON je HIGH**, GPIO6 input. COM6 monitor115200. Původní LOW údaj byl uživatelem opraven, firmware zachovává HIGH. Žádné flashování tohoto opravného obrazu neproběhlo;12s read-only odposlech COM6 bez resetu byl prázdný.

Opravena skutečná SDK neplatnost RMT gpio_num=-1: platný GPIO4 už při vytvoření, safe idle po init, reattach při PLAY a odpojení při STOP. Doplněny konkrétní SDK chyby a jejich OLED/serial prezentace. Nový boot banner a foreground rate-limited diag obsahuje MOTOR6, režimy/WAIT, S1/SD, TX FIFO/reserve/error/heap a QD select elapsed/cache flags.

QD/MZT selector drží jediný validovaný open handle/index;8položková duration cache pouze do STOP/removal/error. READY→PLAY reuse bez další analýzy. Přepočet délky přeskakuje shodné leader/mark cykly, skutečné waveformy nezměněny. Připravený kontejner používá vlastní vybranou cestu, bezpečný alias player current_path. RECORD SETUP samostatné menu FORMAT/CTRL/AUTONAME, REC zkratka v menu, STOP nahoru/uložení po opuštění kořenového menu.

24 sad Debian GCC14.2 ASan/UBSan PASS (111,17 s), target ESP-IDF6.0.1/espressif32@7.0.1 PASS. RAM155548 B, aplikace464553 B, image464976 B. Všech92 fixture SHA256 nezměněno. [Report příčin a ovládání](docs/PLAY_DIAGNOSTICS_FIX.md), [test report](docs/TEST_REPORT.md), [host log](docs/validation/play-fix-host-linux.txt), [build](docs/validation/play-fix-build.txt), [hash](docs/validation/play-fix-summary.json).

Další skutečné ověření vyžaduje nový obraz na desce: boot banner/COM6, MOTOR ON/OFF proti GPIO6, PLAY MANUAL/MOTOR MZF a QD, selector opakované NEXT/PREV a cache flags, RECORD menu a SAVE. Úplné Sharp LOAD/SAVE, elektrické/reset/power-off úrovně, RMT pause retence, PCNT/MCPWM timestampy, SD/OLED stress a runtime heap/stack měření stále nejsou akceptované. Starší UI/PCNT reporty mají historické HEAD/footprint a jsou tak označené.
