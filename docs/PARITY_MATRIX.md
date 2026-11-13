# Matice funkční parity

Stav 13. 11. 2026 vůči [SD2CMT2-Reborn 2.0.3 / 600a788](https://github.com/bales0/MZ-SD2CMT2-Reborn/tree/600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb). DONE znamená implementováno; TESTED znamená konkrétní host test s mock periferií/RTOS; HW-TESTED vyžaduje skutečné měření. **Žádná oblast zatím není HW-TESTED a úplná akceptace zadání není splněna.**

| Oblast | Implementace | Stav SW / důkaz | Zbývající akceptace |
|---|---|---|---|
| Browser a PSRAM index | file_list.c | TESTED: test_file_list, 1000+ souborů, sort, parent/scroll, dvě lifetime alokace, cancel/retry | FAT long paths, plný limit a OLED při velkém adresáři |
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
| OLED | ui.c, oled.c, main.c | TESTED driver/UI: page addressing,20 Hz dirty refresh,10 ms I²C timeout, retry po chybě, clipping a jas; obrazovky DONE | Bez screenshotu/měření na připojeném OLED; latence za zátěže |
| RMT TX DMA | audio/raw_pulse_rmt.c | TESTED produkční encoder:12000 intervalů/refill, jediná transakce, native ticks, long runs, clock pause, stop/error/replay, no start allocation; target build | Retence při clock gate a bezspárovost DMA musí projít analyzátorem |
| MCPWM HW RX | audio/edge_capture.c | TESTED produkční driver: latch/IRQ delay, wrap/multi-wrap idle, stale IRQ, pending pause tail, overflow, init failure, no start allocation; target build | First edge, cache stalls, zatížení a detekce ztráty hran na HW |
| N16R8 / footprint | platformio.ini, sdkconfig | TESTED build/image:16MB header, octal PSRAM config; RAM153372 B, aplikace454617 B; callbacky IRAM/FIFO DRAM | Boot, skutečná PSRAM a hodinový heap/stack/fragmentace stress |

Aktuální logy jsou [port-host](validation/port-host.txt), [port-build](validation/port-build.txt), [port-image](validation/port-image.txt) a [kontrolní součty fixture](validation/fixture-sha256.json). Metodika a omezení jsou v [TEST_REPORT](TEST_REPORT.md); historické reporty TAP/počátečního rekordéru nejsou aktuální inventurou chybějících funkcí.
