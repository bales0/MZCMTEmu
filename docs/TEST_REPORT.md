> Aktuální r15 (14. 11. 2026): [UL, UL700 a UL800 podle SD2CMT2](UL_FAMILY_PERFORMANCE.md). Odstraněné 10ms čekání po každé krátké dávce UL, vypnutý detailní trace, účinné -O2. Všechny tři režimy prošly host přenosem 44 033 bajtů; Z80 přijímače LOW/HIGH a 100Hz plánování PASS, cílový build PASS. Uživatel potvrdil LOAD v r14 UL800 s nízkou rychlostí; r15 a UL700 zatím bez HW potvrzení. S1 bypass zůstává zapnutý. Následující starší záznamy jsou historie.

# Aktuální test S1 bypass / UL chyba

INTERNAL je vynucený bez ohledu na GPIO21.5 dotčených sad ASan/UBSan a target build PASS, včetně konkrétního UL TIMEOUT po close/reset. [Aktuální test](S1_BYPASS_TEST.md), [host](validation/s1-bypass-host.txt), [build](validation/s1-bypass-build.txt), [hash](validation/s1-bypass-summary.json). RAM155532 B, aplikace468005 B. GPIO4 matrix84 odpovídá RMT výstupu3 a poslední uživatelský log ukazuje dokončený RMT předběh, pak selhání UL-MZ800. Skutečný READ konektorový průběh/LOAD dosud není ověřený ani problém označený jako vyřešený. Obraz neflashován.

---

# Diagnostika TX-TRACE r3

Oprava RMT STOP zahazuje neodeslanou frontu/rezervu; dokončené čítače uchovává. Doloženo regresí4095 neodeslaných událostí.5 dotčených sad s ASan/UBSan PASS, target build PASS. Nové pojmenované stavy, STOP_CAUSE, FILE, RMT callback počty a čtení GPIO4 konfigurace mají určit příčinu chybějícího HW signálu; **ten zatím není vyřešený/ověřený**. Uživatelský log z několika minut ukazuje PLAY=3/STOPPED a TX running0, nikoli aktivní TX. [Diagnóza a další reprodukce](TX_OUTPUT_DIAGNOSIS.md), [testy](validation/tx-trace-host.txt), [build](validation/tx-trace-build.txt), [souhrn](validation/tx-trace-summary.json). RAM155532 B, aplikace467709 B; obraz neflashován. Testovací S1 HIGH=EXT zůstává.

---

# Aktuální oprava referenčního ovládání a QD náhledu

**24 sad ASan/UBSan PASS, cílový build PASS.** Main f5cda0fa04566e4008ef40ab0c9c49480316ddac + pracovní změny. Dlouhé REC/PLAY z filebrowseru otevírají samostatná menu; RECORD položky nejsou v PLAY. I první návštěva jiného QD rekordu vynechá úplný výpočet délky, ten proběhne až při PLAY. Pinout všech backendů souhlasí se zadáním/DOCX/schématem, beze změny GPIO. [Podrobný audit](PINOUT_REFERENCE_AUDIT.md), [host log](validation/reference-fix-host-linux.txt), [build](validation/reference-fix-build.txt), [souhrn](validation/reference-fix-summary.json). RAM155516 B, aplikace464705 B. Nový obraz není flashován ani HW akceptovaný. Všech92 fixture nezměněno.

---

# Historická oprava PLAY — 13. 11. 2026

Výchozí main f5cda0fa04566e4008ef40ab0c9c49480316ddac + pracovní změny. **24 sad/ASan/UBSan PASS, cílový build PASS; nový obraz neflashován a na desce neověřen.** RAM155548 B, aplikace464553 B. Uživatel hlásí chyby předchozího obrazu na zařízení, potvrdil MOTOR ON=HIGH a COM6. Příčina odmítnutého RMT GPIO−1, QD session/cache, RECORD SETUP, nové testy a reprodukce monitoru jsou v [PLAY_DIAGNOSTICS_FIX](PLAY_DIAGNOSTICS_FIX.md). [Aktuální host log](validation/play-fix-host-linux.txt), [build](validation/play-fix-build.txt), [souhrn](validation/play-fix-summary.json). Všechny92 fixture nezměněné.

---

# Historický report UI, metadat a PCNT — 13. 11. 2026

**23 host sad s ASan/UBSan, cílový build PASS; HW akceptace NEPROVEDENA.** Výchozí commit main d3f67c4a978a38dad23e49c918b145182046452b plus necommitnuté pracovní změny tohoto zadání. Nebyl proveden staging, commit, vytvoření větve, push ani PR. Reference 2.0.3 / 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb a MZTools nebyly měněny. Uživatelské odstranění starého zadání je zachováno.

## Reprodukce a důkazy

Debian ve WSL, GCC14.2, Python3.13: produkční moduly samostatně, C11/O2/Wall/Wextra/Werror, ASan+UBSan. Izolované dočasné výstupy v .tmp. Cílový Xtensa GCC15.2, ESP-IDF6.0.1, espressif32@7.0.1.

    python3 tests/host/run_tests.py --cc gcc --sanitize --tap-fixtures tests/TAP --qd-fixtures tests/QD --qd-fixtures tests/MZQ
    pio run -e MZCMTEmu -j 1

[Úplný host log](validation/ui-metadata-host-linux.txt), [build](validation/ui-metadata-build.txt), [image_info](validation/ui-metadata-image.txt), [symboly](validation/ui-metadata-symbols.txt), [konfigurace](validation/ui-metadata-config.txt), [souhrn](validation/ui-metadata-summary.json). PCNT je v hlavním obrazu vypnuté; samostatný guard byl navíc úspěšně přeložen s PCNT-on proti skutečným aktuálním target flags/SDK, viz [PCNT compile](validation/ui-pcnt-target.txt). Tento důkaz není link celého PCNT-on obrazu ani HW test.

Windows MinGW5.1 nemá ASan/UBSan. I po uživatelem potvrzeném povolení COMODO nový test_program_metadata.exe vrací WinError5 při spuštění; [Windows log](validation/ui-metadata-host-windows.txt). Nastavení ochrany nebylo měněno. Funkčním alternativním důkazem je kompletní Linux běh se sanitizéry, nikoli úspěšný Windows běh.

## Rozsah automatických testů

Původních 20 sad je zachováno; přibyly test_ui_policy, test_program_metadata a test_capture_pcnt. Formátování starších C testů bylo upraveno pro novější GCC/Werror bez potlačování warningů.

| Sada | Doložený rozsah |
|---|---|
| test_ui_policy | Okamžitý RX pokles, pomalé zotavení, alarmy podle skutečného stavu, marquee a tick wrap |
| test_program_metadata | 80B model/snapshot, původní Sharp/IC/TC hlavičky a první payload, neznámá/neplatná pole, multi-program a oddělený filename, checksum recovery, přímá PLAY/RECORD intervalová shoda, skutečné generované WAV44k/LEP/L16 přes PLAY čtečky, ZX BASIC/CODE/ARRAY/XOR a více bloků |
| test_capture_pcnt | Nezávislý mock čítač fyzických hran, pending latch a tail, jedna/dvě ztracené hrany, 60001 hran přes SDK-like akumulaci, fail-closed bezpečnostní limit; společné MCPWM scénáře |
| test_oled_ui | Skutečný pixelový renderer, inverze browseru, samostatné bary, overlay priority/latch/expiry/tick wrap, dirty pages, 20Hz limit, I²C selhání/retry, jas a clipping |
| test_buttons | Debounce, STOP priorita, repeat/startup-held, tick wrap |
| test_sdcard | Init/mount/unmount/free chyby a retry, bez formátování |
| test_storage | Skutečná ESP větev actoru nad mock queue, native IO pouze worker, pool8/cache/seek/exclusive create, async job, ISR refusal, IO chyby |
| test_player_rmt | Skutečný player/RMT encoder, 17 Sharp módů včetně simulovaného UL, TAP4 rychlosti, WAV/L16, přesné aktivní ticks a user pause |
| test_raw_rmt | 12000 událostí/refilly, dlouhé intervaly/64bit rezerva, 1s pulse s 1 FIFO slotem, částečné dokončení a bytes až po odvysílání, pause/stop/replay/underrun/init failure |
| test_loader_choice | Všech 17 módů, rodiny a zapamatované rychlosti |
| test_capture_mcpwm | Latch nezávislý na IRQ delay, wrap/multi-wrap, pending/stale IRQ, pause tail/resume, FIFO overflow/polarita a init cleanup; PCNT-off |
| test_writer_io | WAV/LEP/L16/MZF write/seek/flush/close failure, provisional cleanup, exclusive create, RIFF/FAT overflow, izolovaná MFI chyba |
| test_file_list | 0/1/7/8/1000/10000 položek, case-insensitive sort/filtry, parent selection/scroll, cancel a neúspěšná podsložka obnoví původní seznam, dlouhé Linux cesty, dvě lifetime PSRAM alokace |
| test_autoname | Sharp/IC/TC a AUX Sinclair/CPM rychlosti/checksumy, název a progress, kolize bez přepsání, ztrátová WAV/LEP omezení |
| test_mzf_writer | Všechny native/copier profily, normalizace TC/IC, duplicitní copy recovery, checksum/partial cleanup, 0..65535 B |
| test_quickdisk | MZQ/QDF CRC, HxC všech16 MFM fází, FlashFloppy, bounds/cancel, dodané obrazy |
| test_route_switch | LOW=EXT, bezpečný startup, bounce/40ms debounce a jednorázové změny |
| test_settings | NVS verze/CRC/migrace/ranges, restore/save/failure cleanup |
| test_player_sharp | Celý transport17 módů, původní metadata před generováním loaderu, MZT metadata při změně rekordu, selector/stop/replay/UL návaznost |
| test_sharp_stream | Golden intervaly/loadery všech módů, MZF/MZT/M12/QD kontejnerové větve, sidecary/fallback a bounds |
| test_recorder | MOTOR/MANUAL/AUTO, pause/timeout, dokončení a abort, overrun/removal nesmějí hlásit SAVED, skutečné soubory a AutoName všech RECORD formátů |
| test_tap_stream | 4 rychlosti a dodané TAP celé průběhy, bloky/pilot/sync/terminator/gap, malformed/empty/maxlen/cancel, 2000 bounded fuzz případů |
| test_player_tap | Skutečný player/GPTimer/TAP a WAV: všechny intervaly čtyř sample rates, hystereze, 256sample chunks, inverze, physical EOF/progress, MOTOR/user pause a underrun |

Modely/mocky neprokazují skutečný FreeRTOS souběh, DMA/cache/IRQ latenci ani retenci RMT při clock-gate. Host OLED náhledy [browser](validation/ui/browser.png), [PLAY](validation/ui/play.png), [RECORD](validation/ui/record.png), [chyba](validation/ui/overlay.png) byly vizuálně zkontrolované; čitelnost skutečného 128×64 OLED zůstává otevřená.

IC test z fyzického WAV44,1k vzorkování prokáže kanonickou hlavičku (HEADER_VALID), nikoli checksum rychlého payloadu. Stejný přesný intervalový vstup dosáhne PAYLOAD_VALID; parser nevyrobí jistotu, kterou ztrátové vzorkování nedodá. Živé RECORD AutoName dostává hrany před WAV/LEP kvantizací. Pasivní dekódování nerozšiřuje existující časové tolerance dekodéru. ZX TAP nikdy neprochází Sharp parserem a nemá vymyšlený EXEC.

Po úplném23sadovém průchodu byla při závěrečném auditu opravena nová PCNT epocha po resume. Doplněný test ztráty hran po pauze a všechny dotčené sady test_capture_pcnt/test_capture_mcpwm/test_recorder znovu prošly ASan/UBSan; [finální cílený log](validation/ui-capture-resume-linux.txt). Následný cílový build a image/symboly odpovídají této opravě.

## Dodané soubory a reference

SHA256 všech 92 souborů v původním [manifestu](validation/fixture-sha256.json) zůstalo shodných. Dodané TAP/QD/MZQ jsou pouze čteny. Historický [QuickDisk reference log](validation/quickdisk-reference.txt) dokládá shodu 14 výsledků (5 přijatých, 9 shodně odmítnutých) s původním parserovým jádrem; není novým HW důkazem. Generované testovací soubory nejsou uživatelské fixture. Starší port-*.txt a port-summary.json dokumentují předchozí port, nikoli aktuální HEAD/footprint.

## Paměť a časová náročnost

Finální výchozí build: **RAM154348 B/327680 B (47,1 %), aplikace461513 B/1048576 B (44,0 %), image461936 B, flash16 MB, octal PSRAM konfigurace**. Statická čísla neobsahují minima runtime heapu, SDK DMA alokace ani skutečné high-water task stacků. Boot N16R8 nebyl změřen. IRAM encoder/capture callbacky a interní DRAM FIFO32768/65536 B potvrzuje výpis symbolů; nedošlo k duplikaci TX FIFO.

program_metadata_t má80 B, store84 B; omezená4událostní timeline synchronizuje zobrazení s dokončeným transportním časem místo prefetch. Pasivní analyzátor sdílí existující AutoName/Sharp decoder a nedostává další64KiB payload buffer. Filename18 B a ZX header19 B jsou samostatné pevné položky. OLED má jediný1024B framebuffer,128B page scratch na stacku a byteové dirty porovnání; žádná alokace v render smyčce. Nejvýše20 renderů/s, jen změněné stránky přes I²C. Metadata se kopírují při změně revize; TX reserve snapshot je O(1), bez scanu FIFO.

MCPWM ISR přidává minimum volných slotů a čítač úspěšně vložených fyzických hran, bez decoderu/FS/I²C/heapu. Call-chain recorder_start_now → edge_capture_init → register_event_callbacks běží z app_task CPU1; IDF alokuje IRQ na volajícím jádře. Priorita3/IRAM jsou konfigurací a map doložené, runtime core a latence dosud neměřené. PCNT-off nemá čítačové prostředky; PCNT-on používá SDK přerušení po30000 hranách, nikoli app ISR na každou hranu.

Browser zachovává dvě lifetime PSRAM alokace; rollback znovu načte původní adresář bez druhé2MiB areny. Známé d_type omezí stat RPC; DT_UNKNOWN zůstává bezpečný fallback. Po neobnovitelném návratu explicitní DIRECTORY RECOVERY. Test10000 položek měří host chování, nikoli rychlost SD/FAT; qsort/SD latency a plný limit65535 čekají na HW.

## PCNT a pravdivá telemetrie

RX minimum je skutečné ISR minimum, sticky fault se nesmaže po vyprázdnění FIFO. Filtr zpomaluje pouze zotavení; alarm vychází ze skutečného vzorku. Diagnostický GPIO fallback nemá přesné historické ISR minimum ani PCNT důkaz. TX reserve zahrnuje rozpracovaný interval i encoded DMA pomocí existujícího konzervativního aktivního wall-time (+10µs guard); jde o odhad, nikoli měřený DMA timestamp. GPTimer fallback ukazuje pouze SW FIFO. UL/mezery nejsou underrun.

PCNT obě hrany GPIO5, bez glitch filtru, accum_count a watchpoints ±30000. A/B ohraničují enable MCPWM, C/D disable + pending latch/tail; skutečně enqueued fyzických N se porovná s [C−B,D−A]. Mimo interval: EDGE LOSS a chyba/odstranění záznamu. Uvnitř nenulového intervalu: EDGE CHECK SUSPECT, bez falešné jistoty i bez automatického smazání jen kvůli běžné hranici. Šířka>64 hran, neplatné/nesouvislé počty nebo count≥0x20000000: nespolehlivá kontrola, CAPTURE ERROR (fail closed). Syntetický tail se nepočítá. Počty neodhalí časový jitter a nepotvrzují absolutní bezeztrátovost; více neobsloužených HW overflow může překročit možnosti SDK akumulace.

## HW akceptace — NEPROVEDENO

Uživatel potvrdil neinvertující U1/U2 a nepřipojený HW. Žádný flash. Zbývá elektrické napájení/DIR/reset/power-off a mechanický S1; boot N16R8/PSRAM/OLED; Sharp MZ-700/800 LOAD/SAVE všech17 módů a TAP4; READ/WRITE/MOTOR/SENSE analyzátorem; PCNT versus generátor při SD/OLED/CPU zátěži včetně skutečného IRQ/core/cache jitteru; RMT pause/resume uprostřed symbolu a DMA kontinuita; SD removal/reinsert/full/readonly; S1 během PLAY/RECORD/UL/SAVING; 30–60min stress s heap minima, stack high-water, SD latency a FIFO watermarks. Teprve měření dovolí HW-TESTED/úplnou HW akceptaci.
