# Časování a vlastnictví periferií

Stav 13. 11. 2026. Autoritou je zdroj SD2CMT2-Reborn 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb, zejména mz_tape_profiles, mzf_playback, mzf_loader a tap_speed. Host důkazy jsou v [TEST_REPORT](TEST_REPORT.md); žádné níže uvedené časování nebylo změřeno na HW.

## TX

Výchozí raw_pulse_rmt.c používá 16 MHz RMT s DMA, 128 interními symboly, frontou jedné transakce a průběžným encoderem. Jedna zdrojová FIFO má 4096 osmibajtových událostí (32768 B); 512 B bitset označuje nativní tick šířky. Další stejně velká DMA fronta nevzniká. Symbolové půlperiody mají maximum 32767 ticků; dlouhá úroveň pokračuje více symboly beze změny úrovně. Poslední neúplný symbol a RMT EOF uzavírají přenos.

Sharp a TAP dodávají přesné šestnáctiny µs. WAV počítá nejbližší absolutní čas sample_index × 16000000 / sample_rate. Hystereze přechází HIGH při >=192 a LOW při <=64; konstantní run se dělí po 256 vzorcích bez umělé hrany. LEP/L16 dodávají celé µs násobené 16. Žádná samostatná sample ISR neobsluhuje WAV PLAY.

Pauza hradluje funkční zdrojové hodiny RMT skupiny, zachová encoder, DMA a rozpracovanou půlperiodu; resume hodiny obnoví. STOP nejprve hodiny obnoví, zastaví kanál a fyzicky odpojí RMT GPIO mux. UL pak vlastní GPIO4 přímo. Další RMT uživatel ve stejné skupině není dovolen. Mock testuje retenci po 70 tickách uprostřed intervalu, **skutečnou retenci je nutné prokázat analyzátorem**.

Uvolňování zdrojových slotů vychází z konzervativní dolní meze aktivního wall-time po rmt_transmit (+10 µs guard), s odečtením pauz. TX-done dorovná přesný součet zakódovaných intervalů. Tento odhad není měřený timestamp začátku DMA. Dokončený čas se publikuje pomocí 32bit sekvence a dvou 32bit slov; v ISR nejsou nové 64bit atomické operace. EOF a hranice bloků nastávají až po fyzickém drainu.

Diagnostický raw_pulse.c s GPTimerem se vybírá pouze CONFIG_MZ_TX_GPTIMER_FALLBACK. Absolutní Sharp/TAP/WAV přechody převádí na nejbližší µs bez kumulativního driftu (matematická chyba nejvýše ±0,5 µs; ISR jitter tím omezen není). Legacy raw_timer/raw_capture nejsou aktivní transport a jejich velké fronty linker ve výsledném ELF odstranil.

## Sharp a TAP

Jediná Sharp tabulka obsahuje 13 profilů, nabídka má 17 režimů. Zachovány jsou regionové leader/mark/data časy, checksum, kopie, loadery a trailing fáze. NORMAL 1:1 má leader LOW4141, mark LOW4096/7795 a data krátký LOW4042/4087 či dlouhý7741/7786 dle následujícího bitu. Testy porovnávají celé průběhy i původní LOW/HIGH Z80 loadery, nikoli jen průměrnou rychlost.

Logické HIGH/LOW profilu nelze zaměnit s READ na konektoru. Sharp waveform zachovává pevnou výstupní polaritu reference; TAP používá přímou referenční polaritu. Uživatelská INVERT se aplikuje až v HAL. U1/U2 podle uživatele neinvertují, proto nepřidávají žádný další logický negátor. Safe idle je vždy READ LOW, SENSE HIGH, MONITOR LOW. Toto je firmware požadavek, nikoli důkaz elektrického resetového stavu.

TAP má pilot 4032/1610 celých pulzů, samostatný LOW/HIGH sync, MSB-first data, terminator a mezery 350 ms/1 s pouze mezi bloky. Všechna čtyři timingová pole pocházejí z referenční tap_speed. Předběžná analýza je přerušitelná; callback UI se při vlastním přehrávání nevolá.

UL je samostatný neblokující FSM se třemi variantami, cílovými loadery, timeouty a user-pause časem. Vlastnictví READ/SENSE dostane až po zastavení RMT; každá chyba vrací safe idle. MZT další rekord otevírá nad stejným souborem bez opakovaného scanu celého kontejneru; po UL→UL se podle reference zastaví.

## RX a kvantizace

Výchozí edge_capture.c používá MCPWM capture timer APB80 MHz, GPIO5 obě hrany, prescale1, IRQ prioritu3. Druhý kanál bez vstupního GPIO poskytuje soft-capture snapshot pro start/pause. Událost nese hardwarový latch; ISR latence neposouvá uložený timestamp. FIFO8192 × 8 B = 65536 B je interní DRAM. 32bit wrap se rozšiřuje pomocí wall-time pouze pro určení počtu otočení při dlouhém tichu. Pauza zakáže vstupní capture, vybere pending latch a zachytí částečný tail; resume vynechá čas pauzy. Overflow či detekovaná ztráta polarity jsou chyby.

CONFIG_MCPWM_ISR_CACHE_SAFE, CONFIG_MCPWM_CTRL_FUNC_IN_IRAM, CONFIG_RMT_TX_ISR_CACHE_SAFE a CONFIG_RMT_ENCODER_FUNC_IN_IRAM jsou zapnuté. [Výpis symbolů](validation/ui-metadata-symbols.txt) ověřuje encoder/helpers/capture callback v IRAM a obě FIFO v interní DRAM. Callbacky nevolají FS, malloc ani I²C. GPIO/GPTimer capture zůstává jen diagnostická alternativa CONFIG_MZ_CAPTURE_GPIO_FALLBACK. První hrana, cache stalls, skutečná ztráta hran a wrap za souběhu SD musí projít HW zkouškou.

LEP/L16 RECORD kvantizuje každý dokončený fyzický interval samostatně:

    units = max(1, ((uint64_t)duration_us + unit_us / 2) / unit_us)

LEP unit50 µs, L16 unit16 µs; žádný residual do sousedního HIGH/LOW intervalu. WAV RECORD má odlišnou absolutní sample fázi s počátečním zbytkem500000 a modulo1000000. Jeho zbytek zachovává sample rate a nesmí být odstraněn. AutoName analyzuje původní hrany převedené samostatně do L16 před ztrátovou WAV/LEP kvantizací.

## Úlohy, paměť a UI

Capture/RMT ISR mají prioritu3 a interní metadata. Storage je statická úloha na CPU0 s prioritou6, 8192 B stackem, čtyřmístnou frontou a osmi 1 KiB souborovými cache. Používá deskriptory open/read/write/lseek/fsync/close, bez alokace stdio FILE při každém otevření. Sekvenční byte API pracuje v cache; seek placeholderu uvnitř cache zůstává v RAM. Synchronous RPC používá notification slot0 vyhrazený callerům. Jediný asynchronní job dokončuje záznam a zveřejní výsledek release/acquire příznakem; UI nevstupuje do writeru během SAVING.

Player je na CPU0, priorita5, 8192 B stack, aktivní obsluha po1 ms; storage má vyšší task prioritu kvůli krátkému obsloužení jeho RPC. UI/record foreground je CPU1, priorita2, stack8192 B, poll5 ms. Realtime capture zůstává ISR nad těmito úlohami. Pinning a hodnoty priorit jsou implementační volba čekající na zatěžovací měření. Žádný UI mutex se nepoužívá uvnitř ISR.

Browser používá přibližně2,5 MiB PSRAM, rekordové profily64 KiB, MZF payload65535 B; alokují se jednou a ISR do nich nepřistupuje. Statická interní RAM finálního buildu154348 B, aplikace461513 B; hodnoty nezahrnují runtime SDK heap/stack a PSRAM. Před akceptací změřit nejmenší volný blok, stack high-water, SD latency a FIFO watermarks při dlouhém provozu.

OLED přenáší jen změněné stránky nejvýše 20 Hz, s10 ms timeoutem a potvrzením úspěšně odeslaných stránek; analýza aktualizuje stav nejvýše 10 Hz a dovoluje STOP. Monitor je LOW při READY/WAIT/pauze/UL/idle/finalize a HIGH při aktivním RECORD nebo slyšitelném PLAY. TAP gap a LEP/L16 preroll/tail mají mute značky v transportu. GPIO41 obsluhuje foreground, takže jeho přepnutí není přesně na hranici symbolu a čeká na HW ověření latence.

## Telemetrie, metadata a grafika — aktuální změna

TX reserve_us se počítá O(1) z přijatých ticks minus konzervativní aktivní wall-time, včetně encoded DMA i zbytku současného dlouhého intervalu; +10µs guard znamená odhad, nikoli přesný DMA timestamp. Nový zdrojový byte counter se posouvá až při fyzickém dokončení označeného intervalu. Nejde o druhou FIFO; pause clock-gate architektura je zachována. Status reset/publication má krátkou kritickou sekci.

RX MCPWM ISR udržuje skutečné min_free a počet úspěšně enqueued fyzických hran. Volitelný PCNT guard (default off) porovnává ukončené epochy intervalem [C−B,D−A], bez per-edge app ISR. Šířka nejistoty>64 nebo count≥0x20000000 je fail-closed; timestamp správnost tím není ověřena. Viz [metodika](TEST_REPORT.md). IRQ se alokuje při recorder_start_now v app_task CPU1; skutečný core binding/latence nebyly změřeny.

Metadata80 B a84B atomický word store mají jediného foreground writeru, UI čte snapshot bez ISR/FS. AutoName a pasivní PLAY sdílejí decoder v exkluzivních transportních režimech; nové64KiB payload úložiště nevzniká. Omezená4událostní timeline brání zobrazování budoucího titulu z prefetch; přetečení nezastaví transport a přejde na UNKNOWN. WAV/edge parser dostává původní úrovně před uživatelskou READ inverzí, vynechá syntetický preroll/tail. TAP je zvlášť ZX/XOR. IC WAV44,1k test potvrzuje embedded header, nikoli checksum rychlého payloadu; stav zůstane HEADER_VALID.

OLED má jeden1024B framebuffer a128B page scratch na stacku; přesné byte porovnání/dirty retry bez druhého framebufferu, max20 renderů/s a pouze změněné I²C stránky. RX alarm používá skutečnou hodnotu, filtr pouze zpomaluje zotavení. Browser rollback znovu čte původní adresář s dočasně vypnutým cancel hookem; není druhá2MiB arena. d_type šetří stat, DT_UNKNOWN používá původní bezpečný fallback. Host10k je funkční test, nikoli SD benchmark.

Aktuální statický footprint: RAM154348 B, aplikace461513 B. Runtime CPU/heap minima/largest block, stack high-water a SD latence **NEZMĚŘENO**; nutný30–60min souběh na desce. [Důkazy a HW seznam](TEST_REPORT.md).
