> Aktuální r40 (20. 11. 2026): LEP/L16 mají blokovou analýzu délky s procenty dokončení, bez časového limitu. ▶ přeskočí do PLAY s procenty, ■ zruší start; dokončená analýza zobrazí aktuální/celkový čas. Uplynulý čas odpovídá fyzicky odvysílanému signálu. Sedm host sad s ASan/UBSan i cílový build PASS. [Souhrn](validation/edge-time-summary.json). Bez commitu a nahrání; HW ověření zbývá.

> Aktuální r33 (20. 11. 2026): [zjednodušený sidecar editor](SIDECAR_EDITOR.md). Jeden řádek LOADER s rychlostí; SAVE CHANGES rovnou spouští bezpečnou transakci bez přehledu a CREATE/UPDATE dialogu. Potvrzení profilu zachováno. Tři host sady a cílový build PASS; HW ověření zbývá.

> Aktuální r31 (20. 11. 2026): [náhled názvu rekordu v editoru](SIDECAR_EDITOR.md). Při listování se zobrazuje název kandidáta bez změny profilu, CHOOSE LOADER bez rychlosti. ABOUT bez spodní navigační nápovědy. Oprava zásobníku sidecar_find zachována; čtyři host sady a cílový build PASS, HW ověření zbývá.

> Aktuální r30 (20. 11. 2026): [jedno potvrzení profilu editoru](SIDECAR_EDITOR.md). LOADER → SPEED → CONFIRM PROFILE; UL bez kroku rychlosti, SAVE nezávislý. INTERNAL i EXTERNAL CMT mají hlášku na 1,2 s. Tři host sady a cílový build PASS; QD/MOTOR návrat do browseru zatím v diagnostice, bez změny PLAY. HW ověření zbývá.

> Aktuální r29 (20. 11. 2026): [editor sidecarů a S1](SIDECAR_EDITOR.md). MFI/M2I/MTI a nové QDI, potvrzení každého pole a samostatný SAVE, transakce s obnovou. S1 opět aktivní: LOW=EXTERNAL, HIGH=INTERNAL; EXTERNAL blokuje pouze PLAY a dovoluje RECORD i editor. HW ověření a nahrání zbývají. Následující starší záznamy jsou historie.

> Aktuální r28 (14. 11. 2026): [oprava předplnění WAV](WAV_PREFILL_FIX.md). Reprodukováno předčasné RMT ukončení při tichém úvodu; před startem WAV nyní minimálně 256 intervalů vedle časové zásoby. Regrese 44,1/48/96 kHz, tři host sady a cílový build PASS; bez nahrání a HW ověření. Ikona RECORD a MIN z r27 zachovány. Následující starší záznamy jsou historie.

 Aktuální r15 (14. 11. 2026): [UL, UL700 a UL800 podle SD2CMT2](UL_FAMILY_PERFORMANCE.md). Odstraněné 10ms čekání po každé krátké dávce UL, vypnutý detailní trace, účinné -O2. Všechny tři režimy prošly host přenosem 44 033 bajtů; Z80 přijímače LOW/HIGH a 100Hz plánování PASS, cílový build PASS. Uživatel potvrdil LOAD v r14 UL800 s nízkou rychlostí; r15 a UL700 zatím bez HW potvrzení. S1 bypass zůstává zapnutý. Následující starší záznamy jsou historie.

# Matice funkční parity

Nejnovější oprava: dlouhé PLAY/REC a samostatná PLAY/RECORD/SYSTEM menu podle reference, první QD NEXT/PREV pouze náhled bez výpočtu celé délky. Pinout beze změny a celý zkontrolovaný.24 sad ASan/UBSan + target PASS, HW nového obrazu neověřeno. [Aktuální audit/testy](PINOUT_REFERENCE_AUDIT.md).

Aktuální PLAY oprava na main f5cda0f + pracovní změny:24 sad ASan/UBSan a target build PASS, HW nového obrazu neověřen. RMT vytváří kanál s GPIO4; QD selector drží validovanou session a omezenou duration cache; RECORD SETUP je samostatné menu. Podrobnosti a hranice důkazů: [PLAY_DIAGNOSTICS_FIX](PLAY_DIAGNOSTICS_FIX.md).

Stav 13. 11. 2026 vůči [SD2CMT2-Reborn 2.0.3 / 600a788](https://github.com/bales0/MZ-SD2CMT2-Reborn/tree/600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb). DONE znamená implementováno; TESTED znamená konkrétní host test s mock periferií/RTOS; HW-TESTED vyžaduje skutečné měření. **Žádná oblast zatím není HW-TESTED a úplná akceptace zadání není splněna.**


## Rozšíření tohoto zadání (HEAD d3f67c4 + pracovní změny)

| Oblast | Stav SW / důkaz | Zbývá na HW |
|---|---|---|
| Grafické PLAY/REC/browser | TESTED: test_oled_ui + test_ui_policy, host PNG, overlay/dirty retry, okamžitý RX pokles | Čitelnost a I²C/CPU latence |
| Shared metadata/provenience | TESTED:80B model, původní Sharp/IC/TC a TC první payload, multi-record, oddělený filename, ZX/XOR | Reálné nahrávky všech loaderů |
| WAV/LEP/L16 pasivní metadata | TESTED: stejné intervaly jako RECORD + skutečné generované soubory přes PLAY čtečky; IC WAV44k pouze HEADER_VALID | Ztrátové/zašuměné signály, neznámé loadery |
| TX časová rezerva | TESTED: O(1), encoded/current interval, long-pulse/64bit/pause, bytes až po dokončení; odhad wall-time | DMA timestamp/drain odhad a RMT retence |
| RX headroom/min/sticky fault | TESTED MCPWM ISR + UI policy; diagnostický GPIO fallback má omezené minimum | Worst-case SD/cache/IRQ zátěž |
| Volitelný PCNT guard | TESTED host PCNT-on/off; target PCNT-on object compile; výchozí obraz off. Potvrzená ztráta / hranicová nejistota / nespolehlivé počty oddělené | Generátor, epochy/overflow/IRQ latence, skutečný PCNT-on image |
| S1 přechod | Policy/debounce TESTED; main abortuje rozpracovaný RECORD i SAVING, INTERNAL bez auto restartu; mechanika zachována | Kontinuita/polarita a všechny stavy skutečného přepínače |

Celkem23 sad, GCC14.2 ASan+UBSan; [aktuální log](validation/ui-metadata-host-linux.txt), [build](validation/ui-metadata-build.txt). HW stále NEPROVEDENO.

| Oblast | Implementace | Stav SW / důkaz | Zbývající akceptace |
|---|---|---|---|
| Browser a PSRAM index | file_list.c | TESTED: test_file_list, 0/1/7/8/1000/10000 položek, sort, parent/scroll, dvě lifetime alokace, cancel/failed-child rollback | FAT long paths, plný limit a OLED při velkém adresáři |
| MZF playback | sharp_stream.c, mzf_stream.c | TESTED: test_sharp_stream, test_player_sharp, test_player_rmt; celé intervaly všech 17 režimů | LOAD na MZ-700/800 a analyzátor |
| MZT selektor/profily | player.c, sharp_stream.c, main.c | TESTED transport: individuální profily/MTI, navigace, STOP/replay; UI DONE | OLED a STOP/RETURN včetně jediného rekordu na HW |
| MZT návaznost | sharp_stream_continue_mzt, player.c | TESTED: native/turbo a smíšené UL; UL→UL stop podle reference | Dlouhá sekvence se skutečným MOTOR |
| M12 | sharp_stream.c, sidecar.c | TESTED: společný kodek/loadery a M2I | Skutečný M12 LOAD |
| QuickDisk | formats/quickdisk.c | TESTED: MZQ/QDF CRC, HxC/FlashFloppy všech 16 MFM fází, cancel/bounds; všech 14 fixture má shodný výsledek s původními parsery, přijaté i shodná data | Skutečný LOAD, duration cache a úplný firmware na HW |
| WAV PLAY | wav_pulse_stream.c, player.c | TESTED: všechny intervaly44100/22050/48000/11025 Hz, hystereze,256-sample runy, invert/pause/EOF; nativní RMT44100 Hz | Zatížení SD a fyzické časy |
| LEP/L16 PLAY | edge_stream.c, raw_pulse_rmt.c | TESTED: signed intervaly, zero extension, preroll/tail, round-trip a RMT integrace | Soubory Sharp/MZTools oběma směry na HW |
| ZX TAP | tap_stream.c, player.c | TESTED:4 rychlosti, pilot/sync/MSB/terminator/gap, všechny dodané TAP,2000 fuzz případů, cancel, oba TX backendy | Skutečný ZX/Sharp přijímač a timing |
|17 režimů / Z80 loadery | formats/mz_tape_profiles.c, play/mzf_loader.c, mz700_fast3.c | TESTED: LOW/HIGH golden získané kompilací původních generátorů; bounds/fallback | IC/TC/FAST3 a cílové RAM varianty na Sharpu |
| UL handshake | play/mzf_loader.c | TESTED:3 varianty, simulovaný peer, timeout/pause, safe GPIO handoff | Elektrické úrovně, reálný peer, všechny chybové návraty |
| MFI/M2I/MTI / AUTO | formats/sidecar.c | TESTED: manuální priorita, case-insensitive TYPE, per-record fallback, chyby a export MFI | SD soubory z externích nástrojů |
| WAV RECORD | recorder.c, wav_writer.c | TESTED: absolutní sample fáze22/44k, RIFF odd pad, overflow guard, write/seek/flush/close fail | Dlouhý SAVE, FAT32 limit a SD timing |
| LEP/L16 RECORD | recorder.c, edge_writer.c | TESTED: nezávislé zaokrouhlení,127 extension/placeholder/tail,8 generací, chyby zápisu | Capture pause/idle a opakované kopie s MZTools/HW |
| MZF RECORD | mz_tape_decoder.c, mzf_writer.c | TESTED: NORMAL/IC/TC, checksum, normalizace loaderu, druhá kopie,0/65535 B, incomplete/delete | SAVE na skutečném Sharpu |
| AutoName / klasifikace | record_autoname.c, record_rename.c, mz_title.c | TESTED: Sharp/IC/TC, Sinclair a CP/M4 rychlosti, názvy, checksum, kolize_01…_99, live pre-quantization | Dlouhý rychlý záznam a cizí tituly na HW |
| RECORD řízení | recorder.c, main.c | TESTED core: user/MOTOR/AUTO, pause priority, overrun, removal, sequence exhaustion | Fyzická tlačítka a MOTOR souběh |
| Asynchronní finalize | recorder.c, storage/fs.c | TESTED: odložený job, frozen UI state/path, cancel a removal nesmějí hlásit SAVED; bounded async slot | Skutečný FreeRTOS/FAT souběh a vizuální SAVING |
| PLAY řízení | player.c, main.c | TESTED core: prefill/drain, user pause priority, MOTOR, EOF/underrun/replay, formátové hranice | Latence fyzických povelů a skutečná fronta za zátěže |
| SENSE/monitor/S1 | app/cmt_bus_policy.h, route_switch.c, main.c | TESTED policy: READY HIGH, WAIT/pause SENSE LOW, mute, UL ownership, EXT priorita a40 ms debounce | Neinvertující U1/U2 potvrzeny uživatelem; napětí/reset/monitor latence neměřeny |
| Tlačítka | drivers/buttons.c | TESTED:30 ms debounce, long/short STOP, chord priority, startup-held, repeat a tick wrap | Ergonomie/latence na fyzických klávesách |
| SD lifecycle | drivers/sdcard.c, main.c | TESTED driver: init/mount/unmount/free failure/retry bez ztráty resource handle; UI recovery DONE | Vyjmutí při skutečné operaci, opětovné vložení, jiná karta |
| SD jediný vlastník | storage/fs.c | TESTED skutečná ESP větev se simulovanou frontou: native IO pouze worker,8 slotů/1 KiB cache, seek patch v RAM, pool exhaustion, ISR refusal, faults | Reálný scheduler, blokující SD latency a stack/heap maxima |
| NVS / menu | settings_codec.c, nvs_settings.c, loader_choice.h, main.c | TESTED core: CRC/version/ranges, migracev1→v2, no rewrite,17 módů a remembered speed; SYSTEM/ABOUT/KEYMAP/brightness DONE | Restart/recovery po výpadku a OLED vizuální QA |
| OLED | ui.c, oled.c, main.c | TESTED pixelový renderer: šest inverzních řádků, metadata/bary, overlay priority/expiry, marquee,20 Hz dirty refresh,10 ms timeout/retry; host PNG viz TEST_REPORT | Host náhledy existují; skutečná čitelnost a měření na OLED chybí; latence za zátěže |
| RMT TX DMA | audio/raw_pulse_rmt.c | TESTED produkční encoder:12000 intervalů/refill, jediná transakce, native ticks, long runs, clock pause, stop/error/replay, no start allocation; target build | Retence při clock gate a bezspárovost DMA musí projít analyzátorem |
| MCPWM HW RX | audio/edge_capture.c | TESTED produkční driver: latch/IRQ delay, wrap/multi-wrap idle, stale IRQ, pending pause tail, overflow, init failure, no start allocation; target build | First edge, cache stalls, zatížení a detekce ztráty hran na HW |
| N16R8 / footprint | platformio.ini, sdkconfig | TESTED build/image:16MB header, octal PSRAM config; RAM155516 B, aplikace464705 B; callbacky IRAM/FIFO DRAM | Boot, skutečná PSRAM a hodinový heap/stack/fragmentace stress |

Aktuální logy jsou [reference-fix-host-linux](validation/reference-fix-host-linux.txt), [reference-fix-build](validation/reference-fix-build.txt), [reference-fix-image](validation/reference-fix-image.txt) a [kontrolní součty fixture](validation/fixture-sha256.json). Metodika a omezení jsou v [TEST_REPORT](TEST_REPORT.md); historické reporty TAP/počátečního rekordéru nejsou aktuální inventurou chybějících funkcí.
