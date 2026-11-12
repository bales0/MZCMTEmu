# Přenos časování na ESP32-S3

## Provedená oprava

recorder.c již neobsahuje s_edge_quantization_residual_us, jeho resetovací funkci ani volání resetů. Pro každý dokončený capture interval se nezávisle použije:

    units = max(1, (duration_us + unit_us / 2) / unit_us)

Součet se počítá v uint64_t, aby nepřetekl u UINT32_MAX. LEP má jednotku 50 µs, L16 16 µs. Při přesné polovině se zaokrouhluje nahoru. Kvantizace délky HIGH nemá žádný stav, který by změnil následnou délku LOW. Příklad regrese: 1, 24, 74, 74 µs v LEP musí dát 1, 1, 1, 1 jednotku; starý residual ovlivnil sousední interval.

WAV používá jinou časovou osu: součet duration_us × sample_rate a zbytek modulo 1 000 000 s počáteční fází 500 000. Tento zbytek zůstává přes hrany, aby počet PCM vzorků odpovídal celkovému času. Jeho odstranění by způsobilo sample drift. Počáteční fáze zachovává dosavadní nejbližší zaokrouhlení, nikoli nový algoritmus WAV.

Záznamové testy linkují skutečný recorder.c, edge_writer.c, edge_stream.c a wav_writer.c. Hardware capture a RTOS hodiny jsou nahrazeny malou host implementací. Neověřují ISR latenci, souběh, pause tail vytvořený HW capture ani přenos na skutečný Sharp. Viz [testy](../tests/host/test_recorder.c) a [report](TEST_REPORT.md).

## Referenční jednotky a zbývající port

Autorita: SD2CMT2-Reborn commit 600a788, formats/mz_tape_profiles.h/.cpp, play/mzf_playback.cpp, play/mzf_loader.cpp a obě guide/CMT_*TIMING_REFERENCE.md. Timer3 ticks jsou šestnáctiny µs. HIGH a LOW v profilech jsou logické úrovně na straně Sharp/8255; reference aplikuje pevnou konektorovou inverzi až na výstupu, bez výměny délek.

Současné mzf_stream.c používá 208/208/416/417 µs a jiné počty leaderu. Není referenčně kompatibilní jen tím, že přehraje MZF. Budoucí port musí mít jedinou tabulku 13 statických profilů, mapování všech 17 loader modes včetně AUTO/UL, přesné počty leader/mark/gap/checksum a samostatné vlastnictví linek během UL. Testovací timing vstupy rekordéru nejsou náhradou těchto golden waveform testů.

Pro budoucí HAL vyhodnotit 20MHz RMT: převod šestnáctin µs je 5/4 ticku. Zaokrouhlení každé hrany může driftovat; pro generování použít absolutní racionální čas přechodů. To je odlišné od zakázaného přenosu chyby při záznamu fyzických HIGH/LOW intervalů. Zvolená HW frekvence zatím nebyla ověřena. Nezaměňovat tento návrh za funkční RMT backend.

## Skutečný stav periferií

Lokální ESP-IDF je 6.0.1. ESP32-S3 soc_caps.h definuje SOC_RMT_SUPPORT_DMA=1. raw_pulse_set_rmt_enabled() zatím nic nezapíná; edge_capture_rmt.c je obálka GPIO ISR + GPTimer. WAV TX běží přes GPTimer sample ISR. Tento zásah periferní backendy nemění.

[ESP-IDF RMT](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/peripherals/rmt.html) dokumentuje 15bit délky symbolů a TX DMA konfiguraci. Dlouhé konstantní úrovně musí pokračovat bez umělé hrany. Fronta více transakcí sama nedokazuje bezspárový stream. Ověřit vlastní kontinuální encoder, interní DMA buffery, refill při souběhu SD/OLED a pause/stop/error hranice na analyzátoru.

[MCPWM capture](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/peripherals/mcpwm.html) poskytuje hardwarový latch timestampu. Před nahrazením GPIO ISR zhodnotit wraparound, dlouhý idle, první hranu, stop snapshot a cache/IRAM bezpečnost. RMT RX také vyžaduje důkaz kontinuity na batch hranicích a při tichu. Výběr backendu zůstává otevřený.

## Paměťový výchozí stav

Fronty raw_timer a raw_capture mají každá 65536 B, edge_capture 8192 událostí po 8 B (65536 B) a raw_pulse 4096 struktur. Linker odstraňuje nevyužité sekce, takže jejich součet není automaticky skutečná BSS výsledného ELF. Build tohoto zásahu hlásí celkovou RAM 195568 B; runtime heap/stack/PSRAM a špičky front nebyly měřeny. Nové DMA fronty se nesmí prostě přičíst ke všem starým backendům. Během tohoto zásahu nebyl přidán žádný runtime buffer ani alokace do rekordéru; odstraněn byl residual a jeho resetovací kód. Ostatní backendy zatím zůstávají kvůli funkčnosti rozpracovaného portu.

## Doplněný ZX TAP

TAP má vlastní jedinou tabulku v audio/tap_stream.c se stejnými 16MHz ticks jako referenční tap_speed.cpp. Zdroj vrací přesné celočíselné ticks; player.c převádí absolutní časy hran na 1µs HAL s nejbližším zaokrouhlením, maximální matematická chyba je ±0,5 µs bez kumulativního driftu. Jde o generování přechodů, nikoli rekordérovou mezihra­novou kompenzaci. Skutečná ISR latence a jitter nejsou tímto výpočtem omezené; RMT DMA zůstává neimplementované.

Pilot má 4032 nebo 1610 celých LOW/HIGH pulzů, sync má odlišné LOW/HIGH časy, data jdou MSB-first po dvojici stejných půlperiod. Po bloku následuje terminator LOW dlouhý jako polovina pilotu, mezi bloky konstantní LOW 350 ms po headeru nebo 1 s po datech, za posledním blokem žádná další mezera. Polarita je přímá podle TAP reference mz_read_set_fast_from_isr; volitelná inverze se aplikuje až v HAL.

raw_pulse.c publikuje čas pouze dokončených fyzických intervalů přes 32bit sekvenční čítač a dvě 32bit části. Nevyužívá nové 64bit atomické operace, další frontu ani alokaci. Writer je ISR, případně init po zastavení timeru a dočkání ISR; čtenář je UI/player. Reset rovněž používá sekvenční publikaci. Původní IRQ depth RMW helpery leží podle ELF v IRAM (4037e288/4037e2e8), původní dělení __udivdi3 v ROM (40002544); latenci jsme neměřili. Čas nepočítá předběžné načtení ani pozastavený wall time a aktualizuje se až dokončením intervalu (u 1s mezery tedy skokem).

Aktuální build po TAP: RAM 195696 B (+128 B proti výchozím 195568), flash aplikace 289305 B (+2768 B). Nepřibyla kopie pulzní fronty. Golden a integrační testy jsou v test_tap_stream.c a test_player_tap.c, výstupy v TEST_REPORT.md.
