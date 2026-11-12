# Matice funkční parity

Stav k 12. 11. 2026: **kompletní port není hotov a není akceptovatelný jako první cílová verze**. Reference [SD2CMT2-Reborn](https://github.com/bales0/MZ-SD2CMT2-Reborn/tree/600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb), firmware 2.0.3.

Status je úroveň důkazu celé oblasti: DONE = implementováno; TESTED = ověřeno host testem; HW-TESTED = ověřeno na cílovém HW. BLOCKED zde znamená nesplněnou akceptaci z uvedeného důvodu (také dosud chybějící implementaci), nikoli tvrzení, že je nutná změna PCB. Žádná oblast není označena za splněnou jen přítomností starého modulu.

| Oblast | Implementační modul nyní | Status | Referenční soubor | Důkaz a zbývající práce |
|---|---|---|---|---|
| Browser | src/file_list.c; src/main.c | BLOCKED | ui/browser.cpp; drivers/sdcard.cpp | 512 položek, alokace jmen/cest; TAP filtr ověřen integračně, bez testů obnovy a dlouhých adresářů — test dosud chybí |
| MZF playback | src/audio/mzf_stream.c | BLOCKED | play/mzf_playback.cpp; formats/mz_tape_profiles.cpp | Pevné 208/416 µs nejsou referenční profily; chybí golden waveform — test dosud chybí |
| MZT selector | src/audio/mzf_stream.c; src/main.c | BLOCKED | play/mzf_playback.cpp; play/play_controller.cpp; ui/browser.cpp | Chybí mini-browser, jednotlivé profily, MTI a čas rekordu — test dosud chybí |
| M12 | src/audio/mzf_stream.c | BLOCKED | play/mzf_playback.cpp; formats/mzi_sidecar.cpp | Základní zdroj existuje; chybí loadery a M2I — test dosud chybí |
| QuickDisk MZQ/QDF/QD | žádný parser | BLOCKED | play/mzf_playback.cpp; ui/browser.cpp | Neimplementováno; CRC/offsety/HxC/FlashFloppy neověřeny — test dosud chybí |
| WAV play | src/audio/wav_reader.c; player.c; raw_timer.c | BLOCKED | formats/wav_reader.cpp; streams/wav_sample_stream.cpp; drivers/wav_playback_driver.cpp | GPTimer sample ISR; chybí waveform test a jednotný TX — test dosud chybí |
| LEP/L16 play | src/audio/edge_stream.c; raw_pulse.c | BLOCKED | play/edge_playback.cpp | Host round-trip pokrývá znaménka, extension, preroll/tail; úplná reference a HW neověřeny — [host test](../tests/host/test_recorder.c), [výsledky](TEST_REPORT.md) |
| ZX TAP | src/audio/tap_stream.c; player.c; raw_pulse.c; src/main.c | TESTED | play/tap_playback.cpp; play/tap_speed.cpp | [Golden parser/waveform a fuzz](../tests/host/test_tap_stream.c), [skutečný player/ISR s mock periferií](../tests/host/test_player_tap.c), [výstup](validation/tap-host.txt). 1400/1800/2100/2500, menu, sync/pilot/data/gap a bad-file validace. HW a RMT DMA neověřeny; rychlost zatím není perzistentní. |
| Všech 17 loader modes | src/audio/mzf_stream.c | BLOCKED | play/loader_mode.h; formats/mz_tape_profiles.cpp; play/mzf_loader.cpp | Neimplementována úplná tabulka ani generování loaderů — test dosud chybí |
| UL handshake | src/audio/mzf_stream.c | BLOCKED | play/mzf_loader.cpp; play/mzf_playback.cpp; play/play_controller.cpp | Částečné UL; chybí všechny varianty, adresní kontroly a testy — test dosud chybí |
| MFI/M2I/MTI | žádný parser | BLOCKED | formats/mzi_sidecar.cpp; guide/MFI_M2I_MTI_FORMAT.md | Neimplementováno — test dosud chybí |
| AUTO loader | src/audio/mzf_stream.c | BLOCKED | play/play_engine.cpp; play/mzf_playback.cpp | Chybí metadata i úplný reset na NORMAL 1:1 — test dosud chybí |
| WAV record | src/audio/recorder.c; wav_writer.c | BLOCKED | record/wav_record_engine.cpp; drivers/wav_record_driver.cpp | Host fáze a RIFF procházejí; chyby FS, dlouhý idle, 4GB hranice a HW chybí — [host test](../tests/host/test_recorder.c), [výsledky](TEST_REPORT.md) |
| LEP/L16 record | src/audio/recorder.c; edge_writer.c; edge_capture.c | BLOCKED | record/edge_record_driver.cpp; record/edge_record_engine.cpp | Residual odstraněn; host hranice/round-trip prošly. Pause ve stejné úrovni a capture na HW neověřeny — [host test](../tests/host/test_recorder.c), [výsledky](TEST_REPORT.md) |
| MZF record | žádný dekodér | BLOCKED | record/mzf_record_engine.cpp; formats/mz_tape_decoder.cpp | Neimplementováno včetně checksum/recovery/duplicit — test dosud chybí |
| AutoName | žádná implementace | BLOCKED | record/record_autoname.cpp; formats/mz_title.cpp; record/record_engine.cpp | Neimplementováno včetně kolizí a sidecar rename — test dosud chybí |
| RECORD klasifikace | žádná implementace | BLOCKED | record/record_autoname.cpp; formats/mz_tape_decoder.cpp; formats/mz_loader_profiles.cpp | Chybí NORMAL/IC/TC/MZ700/CPM/Sinclair a rychlý lock — test dosud chybí |
| RECORD ovládání | src/audio/recorder.c; src/main.c | BLOCKED | record/record_engine.cpp; ui/record_menu.cpp; ui/record_screen.cpp | Host user pause/MOTOR/AUTO/cancel/error prošly; fyzické hrany a tlačítka chybí — [host test](../tests/host/test_recorder.c), [výsledky](TEST_REPORT.md) |
| PLAY ovládání | src/audio/player.c; src/main.c | BLOCKED | play/play_controller.cpp; play/play_engine.cpp | Chybí jednotný transport a výběr rekordů. TAP testuje skutečné pauzy/underrun/replay a opravu předčasného EOF při MOTOR pause — [test](../tests/host/test_player_tap.c) |
| SENSE/monitor/CMT trasa | src/main.c; src/audio/mzf_stream.c | BLOCKED | drivers/mzio.cpp; drivers/monitor_mute.cpp | Fyzický přepínač S1; SW stav neřídí mux. Typy U1/U2 a měření úrovní chybí — test dosud chybí |
| SD lifecycle | src/drivers/sdcard.c; src/main.c | BLOCKED | drivers/sdcard.cpp; ui/browser.cpp; main.cpp | SPI DMA existuje; record→browser/reinit/remove/write-error neotestovány — test dosud chybí |
| Nastavení | src/main.c | BLOCKED | drivers/calibration_store.cpp; ui/menu.cpp; ui/record_menu.cpp; ui/settings_menu.cpp | Chybí NVS schéma, migrace a všechny volby — test dosud chybí |
| OLED | src/ui/ui.c; src/drivers/oled.c; src/main.c | BLOCKED | src/ui/*.cpp | Dirty pages existují; chybí kompletní obrazovky a UI latence — test dosud chybí |
| Realtime TX/RX a vlastnictví | src/audio/raw_pulse.c; raw_timer.c; edge_capture.c | BLOCKED | zadání §5; ESP-IDF RMT/MCPWM | RMT TX no-op, RX GPIO ISR+GPTimer. Chybí kontinuální DMA, HW timestamp capture a zatěžovací měření — test dosud chybí |

## Dokončený dílčí zásah

| Požadavek | Modul | Status | Reference | Test |
|---|---|---|---|---|
| Nezávislá kvantizace LEP/L16 bez residualu a jeho resetů | src/audio/recorder.c | TESTED | record/edge_record_driver.cpp: edge_quantize_short_from_isr | [test_edges](../tests/host/test_recorder.c), původní verze FAIL, opravená PASS; [report](TEST_REPORT.md) |

Testovací timing vstupy všech 13 statických profilů slouží pouze pro regresi rekordéru. Nejsou golden waveform testem 17 loader modes, loaderů nebo UL. Pro žádnou oblast nebyl proveden HW test.

TAP byl doplněn v necommitovaných pracovních změnách na výslovný pokyn uživatele. Nevznikl nový commit, větev ani push. Status TESTED pro TAP označuje software a simulovaný HAL; časování na desce, OLED ovládání a funkční LOAD musí ještě projít HW akceptací. Ostatní povinné oblasti zůstávají otevřené.
