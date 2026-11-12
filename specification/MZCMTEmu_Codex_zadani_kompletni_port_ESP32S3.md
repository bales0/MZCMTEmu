# ZADÁNÍ PRO CODEX – MZCMTEmu: kompletní funkční port SD2CMT2-Reborn na ESP32-S3

**Datum analýzy:** 12. 11. 2026  
**Cílový repozitář:** https://github.com/bales0/MZCMTEmu (`main`, prověřený HEAD `5e654f1683c0436f5f8cb3853ece24e8c618dbf7`)  
**Referenční repozitář – ZÁVAZNÝ FUNKČNÍ VZOR:** https://github.com/bales0/MZ-SD2CMT2-Reborn (`master`, prověřený HEAD `600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb`)  
**Referenční firmware podle `src/version.h`: 2.0.3.** Pozor: úvodní README ještě uvádí 2.0.2; při konfliktu rozhoduje skutečný zdrojový kód uvedeného commitu.

> **Hlavní zadání:** Reimplementuj/kompletně přestav MZCMTEmu tak, aby v **první cílové verzi** poskytoval **všechny uživatelské funkce a kompatibilní chování aktuálního SD2CMT2-Reborn (1:1 funkční parita)**. Nemá jít o mechanický překlad AVR zdrojů ani o pouhé rozšíření stávajících menu. Využij nativní periferie, obě jádra, DMA, paměť a OLED ESP32-S3. Pokud je to čistší a bezpečnější, přepiš firmware od základu, ale zachovej kompatibilní existující hardware a ověřené části. **Nevypouštěj žádnou referenční funkci jen proto, že je její port složitý.** Všechny reálné odchylky a hardwarové limity výslovně zdokumentuj.

**Rozsah oprávnění:** úpravy pouze v cílovém repozitáři MZCMTEmu, ideálně na nové pracovní větvi, se zachováním původní `main`. Referenční repozitář SD2CMT2-Reborn i projekt MZTools jsou read-only. Nedělej žádné změny v MZTools ani v jeho testech či CI. Nevyžaduj změnu PCB ani přesun GPIO bez předchozího prokázání nutnosti; jakákoli nevyhnutelná HW změna musí být označená jako blokující a popsaná. Všechny portované cizí části musí zachovat licenci/atribuci. V průběhu práce veď evidenci funkční parity a výsledků testů.

---

## 1. Ověřený výchozí stav – NEZAMĚŇOVAT ZA POŽADOVANÝ CÍL

### 1.1 Cílový projekt MZCMTEmu

- ESP32-S3 DevKitC-1 / ESP-IDF, PlatformIO, deklarovaný modul **N16R8** (16 MB flash, 8 MB octal PSRAM), 240 MHz, dva procesorové tasky.
- **OLED SSD1306 128 × 64**, I²C 400 kHz, adresa 0x3C; 8 textových řádků, framebuffer 1024 B.
- Aktuálně zvládá základní procházení SD, přehrávání **WAV, LEP, L16, MZF, MZT, M12**, záznam **WAV, LEP, L16**, základní MOTOR/MANUAL/AUTO signal režimy, uživatelské pauzy a částečnou UL implementaci.
- MZT se nyní zpracovává jako posloupnost záznamů, nikoli jako plnohodnotný referenční selektor s individuálními profily a MTI. Stávající M12 neznamená automaticky paritu M2I a rychlých loaderů.
- **Chybí zejména:** nahrávání přímo do MZF, AutoName, klasifikace Sharp/turbo/copier/CPM/Sinclair při záznamu, profily NORMAL 1:1–1:4 / MZ700 / IC / TC / všechny UL varianty, výběr záznamů MZT a QuickDisk, přehrávání MZQ/QDF/QD, TAP, úplné MFI/M2I/MTI, perzistentní nastavení a většina pokročilých funkcí referenčního menu.
- V `src/audio/recorder.c` je **stále aktivní `s_edge_quantization_residual_us`**: chyba zaokrouhlení jedné LEP/L16 délky se přičítá k následující hraně. Jde o rizikový mechanismus pro round-trip RECORD→PLAY; zrušit tuto mezihra­novou kompenzaci a ověřit správné nezávislé zaokrouhlování.
- `src/audio/edge_capture_rmt.c` výslovně říká, že jde jen o **kompatibilní obálku pro GPIO ISR + GPTimer**, žádný RMT RX kanál se nepoužívá. `raw_pulse_set_rmt_enabled()` v `src/audio/raw_pulse.c` je no-op: žádný RMT TX. Současný WAV výstup `raw_timer.c` generuje vzorky v GPTimer ISR.
- `src/file_list.c` udržuje max. 512 položek, opakovaně alokuje cesty/jména přes `malloc` a prochází/sortuje adresář v RAM. Přestavbu navrhni tak, aby nedocházelo ke zbytečné fragmentaci a aby dlouhé adresáře neblokovaly CMT.
- Aktuální `src/main.c` má cca 1720 řádků a slučuje UI, transport, nastavení i SD lifecycle; rozděl odpovědnosti. `service_switch_logic_stub()` jen čte GPIO, **nepřepíná skutečný externí CMT hardware**.
- `src/drivers/sdcard.c` používá SPI2 + `SPI_DMA_CH_AUTO` a momentálně 10 MHz; DMA pro SPI tedy už existuje, ale nikoli u signálové CMT cesty.

### 1.2 Referenční projekt SD2CMT2-Reborn

- AVR ATmega2560/Arduino, SdFat 2.x, LCD 16×2, analogová tlačítka, EEPROM, časovače AVR. **Neportovat přímo `ISR(TIMERx...)`, PROGMEM, AVR registr/TCCR, `ATOMIC_BLOCK`, AVR EEPROM ani `millis()` časování jako jedinou časovou autoritu pro pulzy.** Přenášej protokolové algoritmy, formáty, reference a chování.
- Funkční moduly: `src/play/`, `src/record/`, `src/formats/`, `src/streams/`, `src/drivers/`, `src/ui/`. Autoritativní zdroje zdokumentovány níže.
- U referenční verze nepovažovat `.hex` z `build/` ani starší informace v README za přesnější než poslední zdrojový kód. Všechna nejednoznačná místa ověřit na `master` commitu výše.

## 2. Hardware – zachovat současné zapojení ESP32-S3

| Signál/funkce | GPIO MZCMTEmu | Směr/poznámka |
|---|---:|---|
| `READ` – data do Sharpu | GPIO4 | výstup CMT do MZ; idle LOW podle dosavadního kódu |
| `WRITE` – data ze Sharpu | GPIO5 | vstup, time-critical zachycení hran |
| `MOTOR` | GPIO6 | vstup s pull-up (logiku polarity ověřit měřením) |
| `SENSE` | GPIO7 | výstup; kompatibilní stavy motor/record/play/UL |
| `PLAY` | GPIO8 | tlačítko proti GND |
| `STOP` | GPIO9 | tlačítko; krátký/dlouhý stisk |
| `NEXT` / `FF` | GPIO10 | tlačítko s repeat |
| `PREV` / `REW` | GPIO11 | tlačítko s repeat |
| `REC` | GPIO42 | tlačítko |
| SD SPI CS/SCK/MOSI/MISO | 13/14/15/16 | SPI2, DMA; max. frekvenci odladit empiricky |
| OLED I²C SDA/SCL | 17/18 | 400 kHz, SSD1306 128×64, 0x3C |
| `SWITCH` | GPIO21 | stávající vstup pro externí CMT/volbu trasy; neimprovizovat HW mux |
| audio monitor / mute | GPIO41 | polarita/fyzický význam ověřit z HW; stávající kód používá výstup |
| SD card detect | GPIO47 | active LOW, debounce |

**Zásadní:** ESP32-S3 je 3,3V zařízení. Před zkouškou na skutečném Sharpu ověřit ochrany/úrovně/oddělení WRITE/MOTOR a kompatibilitu READ/SENSE s 5V logikou, polaritu a bezpečný stav při resetu. Nikdy nepřipojovat 5 V přímo k ne-5V-tolerantnímu GPIO. Ověřit dostupnost GPIO na konkrétním modulu a omezení desky/PSRAM/JTAG/USB. Nespojovat výstupy proti sobě a nepoužít GPIO12 jako „pomocné hodiny“ bez důvodu.

Interní/external CMT přepínání podporovat na úrovni funkce, ale pokud stávající PCB skutečně nemá řízený elektronický přepínač, zobrazit dostupný fyzický přepínač/stav. **Netvrdit, že software umí přepínat trasu, která na HW fyzicky neexistuje.**

## 3. Závazná matice 1:1 funkční parity

Každý řádek musí mít v novém `docs/PARITY_MATRIX.md` konkrétní implementační modul, status `DONE/TESTED/HW-TESTED/BLOCKED`, referenční soubor a odkaz na test. "Připraveno", "placeholder", "TODO" nebo samotná položka v menu **není** splnění.

| Oblast | Povinné chování první cílové verze | Reference |
|---|---|---|
| Browser | SD adresáře, abecední řazení, návrat nadřazené složky, dlouhé názvy/scroll, plynulé FF/REW, rozumné zachování pozice, retry po SD chybě, skrytí companion souborů | `ui/browser.cpp`, `drivers/sdcard.cpp` |
| MZF | autentické hlavičky/marky/bitové pořadí/checksum, datový blok, úvodní/koncové mezery, všechny profily a fyzická polarita | `play/mzf_playback.cpp`, `formats/mz_tape_profiles.cpp` |
| MZT | analýza celého kontejneru, **mini-browser logických rekordů**, číslo, název, profil, čas, next/prev, korektní hranice a návrat po STOP; profil pro každou položku zvlášť | `play/mzf_playback.cpp`, `play/play_controller.cpp`, `ui/browser.cpp`, `formats/mzi_sidecar.cpp` |
| M12 | plný single-record playback, tytéž použitelné rychlé loadery co MZF, **M2I**, AUTO fallback | `play/mzf_playback.cpp`, `formats/mzi_sidecar.cpp` |
| QuickDisk | **MZQ, QDF a QD** – read-only obsahová detekce logického/HxC/FlashFloppy formátu, bezpečná analýza, mini-browser, přehrání zvoleného obsahu jako CMT/MZF, správné offsety/CRC dle formátu | `play/mzf_playback.cpp`, `ui/browser.cpp` |
| WAV play | podpora kompatibilních 8bit mono PCM z reference, sample rate dle souboru, prahování/hystereze/polarita, pauzy a stav fronty | `formats/wav_reader.cpp`, `streams/wav_sample_stream.cpp`, `drivers/wav_playback_driver.cpp` |
| LEP/L16 play | znaménkové časované intervaly, **50 µs LEP / 16 µs L16**, extension/long-run pravidla, předběh/tail, správná polarita | `play/edge_playback.cpp` |
| ZX TAP | kompletní ZX Spectrum TAP (header/data, pilot, sync, data, gap), konfigurovatelná rychlost, validace vadných bloků | `play/tap_playback.cpp`, `play/tap_speed.cpp` |
| Profily | `NORMAL 1:1,1:2,1:3,1:4`, `MZ700 1:1`, `MZ700 FAST3/1:3`, `IC 1:1,1:2,1:3,1:4`, `TC 1:1,1:2,1:3`, `UL`, `UL MZ800`, `UL MZ700`, `AUTO`. Zachovat všechny neobvyklé fallbacky | `play/loader_mode.h`, `formats/mz_tape_profiles.cpp`, `play/mzf_loader.cpp` |
| UL handshake | přenos po READ/WRITE/SENSE včetně rozpoznání cílového MZ-700/MZ-800, příslušného loaderu, variant pro RAM/adresy, timeoutů, detekce chyb, navrácení běžné trasy, fyzické kompatibility | `play/mzf_loader.cpp`, `play/mzf_playback.cpp`, `play/play_controller.cpp` |
| MFI/M2I/MTI | companion `.MFI` pro MZF, `.M2I` pro M12, `.MTI` pro **individuální rekordy** MZT, `TYPE/SPEED`, `RECORD=`, validace, prioritní manuální volba, `I` indikátor; skrýt i legacy `.MZI` | `formats/mzi_sidecar.cpp`, `guide/MFI_M2I_MTI_FORMAT.md` |
| AUTO loader | platná metadata -> požadovaný profil, chybějící/neplatná metadata -> **skutečný NORMAL 1:1**, nikoliv částečně aktivovaný předchozí UL/turbo; platí též pro QD rekord | `play/play_engine.cpp`, `play/mzf_playback.cpp`, metadata guide |
| Záznam WAV | 44 100 / 22 050 Hz, 8 bit mono, validní RIFF délky, přesné zastavení, prealloc/truncate nebo ekvivalent, signálové prahy, spolehlivé dokončení | `record/wav_record_engine.cpp`, `drivers/wav_record_driver.cpp` |
| Záznam LEP/L16 | nahrávání hran do formátově správných intervalů, komprese dlouhých runů, správný tail/extension, bez přenesené kvantizační chyby | `record/edge_record_driver.cpp`, `record/edge_record_engine.cpp` |
| Záznam **MZF** | softwarová dekódovací cesta pro validní hlavičku a data, kontrolní součty, duplicity/opravy, vypisování průběhu a ukládání validního souboru | `record/mzf_record_engine.cpp`, `formats/mz_tape_decoder.cpp` |
| AutoName | detekce titulku z kazetové hlavičky u záznamu, normalizace znaků, bezpečné přejmenování, kolize (`-01...`), metadata/copier profil; platí pro referenčně podporované WAV/LEP/L16/MZF | `record/record_autoname.cpp`, `formats/mz_title.cpp`, `record/record_engine.cpp` |
| Profily při RECORD | rozpoznání Sharp NORMAL 1:1–1:4, IC/TC, MZ700; oddělené profilové informace a pomocná detekce Sinclair a CP/M InterCopy CMT podle reference; zachovat detekci po rychlém leaderu | `record/record_autoname.cpp`, `formats/mz_tape_decoder.cpp`, `formats/mz_loader_profiles.cpp` |
| Ovládání RECORD | MOTOR (čekání na motor), AUTO (čekání na aktivitu, 5 s nečinnost dle reference), MANUAL, krátký STOP uloží, dlouhý STOP zruší a smaže, správná pauza / restart | `record/record_engine.cpp`, `ui/record_menu.cpp`, `ui/record_screen.cpp` |
| Ovládání PLAY | MOTOR/MANUAL, uživatelská vs motorová pauza, bezpečné pozastavení přesně na intervalu, volba rekordů, konec/underrun, chybové stavy | `play/play_controller.cpp`, `play/play_engine.cpp` |
| Stav SENSE/monitor | shodné stavy signálů vůči Sharpu, přístup k CMT zdroji, monitor při přehrávání/nahrávání a mute během UL; bezpečná úroveň při idle/restartu | `drivers/mzio.cpp`, `drivers/monitor_mute.cpp` |
| SD lifecycle | startup bez SD, vložení/vyjmutí, debounce, obnovení po vyjmutí během záznamu/přehrávání, chyby zápisu, spolehlivé otevření `/RECORDINGS`, **reinit po record/dir fail** | `drivers/sdcard.cpp`, `ui/browser.cpp`, `main.cpp` |
| Nastavení | perzistentní volby playback/record/konfigurace, jejich úplné obnovení, validace rozsahů, bezpečný upgrade verze dat; referenční keymap a systémové informace | `drivers/calibration_store.cpp`, `ui/menu.cpp`, `ui/record_menu.cpp`, `ui/settings_menu.cpp` |
| OLED | veškeré informace 16×2 LCD nabídnout na 128×64 OLED a využít prostor na víceřádkové menu, stavový řádek, název, profil, čas, progress, motor/SD, chybu; bez zahlcení I²C | všechny `ui/*.cpp` + původní OLED |

**Nepřidávat do povinné parity něco, co nemá referenční firmware:** `.MZQ/.QD/.QDF` jen pro read-only prohlížení/přehrávání, nikoli zápis QD. Vlastní emulátor není povinen integrovat desktopové konverzní nástroje MZTools. Neoznačovat **BSD AutoName** za již implementovanou funkci reference – v `record_autoname.cpp` aktuálního commitu není; může být až samostatné budoucí rozšíření, ne náhrada 1:1 portu.

## 4. Autorita pro časování – žádná aproximace „z původních WAV“

Primární reference:

1. `guide/CMT_TIMING_REFERENCE.md` – ROM/Z80/loader-derived timing, vysvětlení context-dependent pulzů, profilů MZ-700/MZ-800 a tolerancí.
2. `guide/CMT_INTERCOPY_TURBOCOPY_TIMING_REFERENCE.md` – InterCopy v10.2 / TurboCopy v1.22, odlišení **historického zapisovacího waveformu** od **receiver-compatible loader timing**, další původ a důvěryhodnost hodnot.
3. `src/formats/mz_tape_profiles.cpp` a `src/play/mzf_playback.cpp` – skutečně používané kompatibilní AVR Timer3 ticks pro přehrávání.
4. `src/play/mzf_loader.cpp` – loadery, mapování paměti, handshake, verze/timing.

**Přenášet časové jednotky, nikoliv magická AVR čísla:** původní timer @ 16 MHz → `duration_us = ticks / 16` (tam, kde jde opravdu o 16MHz tik), ale ideálně zachovávat přesné celočíselné racionální časy nebo větší tick resolution (např. RMT 10 MHz či 20 MHz, pokud je prakticky podporováno), aby nedocházelo k trvalému zaokrouhlovacímu driftu. Krátké a dlouhé pulzy mají nezávislé fyzické HIGH/LOW časy. Nezaměňovat čísla `HIGH/LOW` v tabulce s úrovní MZ READ nebo WRITE bez ověření inverze.

**Konkrétní kontrolní vektory** (`mz_tape_profiles.cpp`, AVR ticks / 16 MHz): MZ800 NORMAL 1:1 3807/4087/7506/7786; NORMAL 1:2 1818/2228/3753/4164; NORMAL 1:3 1407/1994/2815/3577; NORMAL 1:4 1232/1877/2522/2874; MZ700 NORMAL 1:1 3840/4224/7424/7904; IC 1:1 3753/4222/7506/7917; TC 1:1 4015/4000/8015/8015. Prověř další hodnoty přímo z uvedené tabulky a implementuj všechny.

Nedovol, aby „optimalizace na RMT“ změnila počet mark/gap pulzů, délku leaderu, data bit encoding, checksum, start-gap, koncový stav nebo posloupnost UL handshake. Pro každý profil udrž golden fyzických přechodů v čase s testem (včetně inverze a stavu na hranicích dat/loaderu).

### Zákaz chybné kvantizace mezi hranami

Původní ESP32 `s_edge_quantization_residual_us` z `recorder.c` **odstranit včetně resetů a závislých stavů**. Přímé LEP/L16 nahrávání má kvantizovat **každý fyzický časový interval zvlášť**, ne přenášet jeho chybu do sousedního (to může systematicky přesouvat šířky HIGH/LOW a rozbít 3:1/4:1 při RECORD→PLAY). Pro **WAV resampling** je naproti tomu přípustný jeden **absolutní časový akumulátor vzorkovacích fází** – jde o jiný problém a není to mezihra­nový „pulse quantization residual“. Rozdíl dokladovat v komentářích i testech.

## 5. Nová ESP32-S3 architektura (navržená, s nutností ověření)

### 5.1 Datový tok

```text
                  ESP32-S3 / ESP-IDF
   FAT / SD SPI2 DMA <---> storage task (jediný vlastník FS/session)
                           |
   browser / sidecars --> parser/profiles/codec --> ring událostí
                           |                     |
                      playback task          record/decoder task
                           |                     ^
                       RMT TX + DMA           HW edge capture
                           |                     ^
                    GPIO4 / READ          GPIO5 / WRITE
                           \                   /
               MOTOR GPIO6 / SENSE GPIO7 / UL handshake

   UI task --> SSD1306 dirty page I2C (nižší priorita)
   persistent settings --> NVS se schématem/verzí/CRC
```

- **Primární preferovaný PLAY backend:** RMT TX s DMA, přesný výstup sledů `(level, duration_ticks)`, asynchronní producer/consumer, dvoubufrování, nepřerušovaný výstup přes hranice DMA transakcí; deterministická latence. RMT TX skutečně použít, ne no-op stub. ESP32-S3 podle `SOC_RMT_SUPPORT_DMA` podporuje RMT DMA; ověřit požadavky konkrétní verze ESP-IDF a skutečné fungování kontinuálního streamu.
- **Primární zkoumaný RECORD backend:** RMT RX DMA/ping-pong, **jen pokud** prokazatelně zvládne kontinuální dlouhý leader, ticho, včasné proplachování a žádnou ztrátu na hranicích dávek. Jinak preferuj **MCPWM capture** (HW latched edge timestamps; ISR jen vloží čas/úroveň do ring bufferu), případně průkazně deterministickou metodu. Stávající GPIO ISR + GPTimer může zůstat pouze jako ověřený fallback pro test a diagnostiku. RMT RX i MCPWM je nutno posoudit testem při souběhu SD/OLED/PSRAM.
- **Není požadavek použít DMA úplně na všechno:** SPI SD DMA je vhodné; I²C SSD1306 nemusí mít DMA. Nepřidávej I²C DMA za cenu neudržovatelného ovladače. Primární je determinismus CMT.
- **WAV playback:** parser může konvertovat PCM vzorky na sled level-run intervalů s přesným výpočtem časů podle sample rate a hysterezí; preferuj jedno CMT TX časovací jádro, ne druhý trvalý GPTimer ISR s 44,1k/48k přerušeními. Zachovej výsledný fyzický waveform a přechodové hrany; experimentálně porovnej referenční WAV.
- **WAV record:** z hran rekonstruuj 8bit PCM v 22,05/44,1 kHz prostřednictvím absolutní časové osy a sample-phase accumulatoru; generování PCM ani zápisy FS NESMÍ běžet v ISR. Dlouhý idle musí být bezpečný a pozdější aktivita nesmí ztratit první hranu.
- **Long run:** RMT symbol má 15bit duration; delší dobu reprezentuj jako navazující stejnoúrovňové symboly bez umělé změny GPIO. Zvlášť otestuj ticho, prodlevy, start/stop a konečné úrovně.
- **UL:** handshake je interaktivní obousměrná operace; statické DMA waveforms ho nenahradí. Implementuj samostatný realtime stavový automat s přesně definovaným vlastnictvím READ/WRITE/SENSE, timeouty a návratem na normální TX/RX backend. Během UL vypnout konkurenční waveform driver, který by se pral o GPIO4.

### 5.2 Paměť, synchronizace a stabilita

- ISR, DMA metadata a fronty používané driverem umísti do příslušné **interní DMA-capable/IRAM** paměti. **PSRAM není automaticky vhodná pro DMA nebo ISR**; ověř DMA capability a cache-safety. PSRAM využij pro velké netiming-critical indexy a předčítání, pokud to přináší reálný zisk.
- **Nepoužívat runtime `malloc/free` v časově kritické cestě**, při start/stop nahrávání, v ISR ani pro každý soubor/pulz. Alokovat jednou při init pevné bounded buffery / pooly; sdílet jen pro výlučné režimy s jasným vlastníkem. Zpracovat audit BSS/internal heap/PSRAM/stack/fragmentace.
- Původní `raw_timer.c` a `raw_capture.c` mají každá 65536B statickou frontu, `edge_capture.c` 8192 událostí a `raw_pulse.c` 4096 pulzů: nová architektura **nesmí nekontrolovaně přičítat další obdobné buffery**. Redukovat duplicitní backendy a vyčíslit celkový footprint.
- Jediný vlastník RMT TX, jediný vlastník RX/capture, jediný vlastník SD otevřených souborů; explicitní shutdown protokol, žádné race mezi stop/pause/SD removal/OLED. Přenos mezi úlohami via bounded SPSC ring/queue a notifikace. ISR jen IRAM-safe minimum; žádný FS, `printf`, `malloc`, I²C ani blokování.
- Navržené pořadí: capture/play realtime > SD streaming > UI. Pinning jader až po měření; každou prioritu a zámek zdůvodnit. UI nesmí držet mutex, který potřebuje realtime fronta.
- Backpressure a vodní hladiny: vyplňované i vyčítané fronty, stop při trvalém overrun, **nikdy potichu nepřeskočit hranu**. SD latenci měřit; zavést prefill, sektory a asynchronní finalize. Během dlouhého 1:4 leaderu a rychlé navazující hlavičky nesmí AutoName ani SD spotřebovat capture ring.
- U průběžného přehrávání bezspárových pulzů nesmí mezi dvěma DMA přenosy vzniknout nechtěný idle/glitch. Ověřit analyzátorem; není-li možné zaručit, použít kontinuální RMT encoder/stream nebo vhodný jiný časovač.
- UI SSD1306: dirty-pages/diff refresh, I²C transakce krátké, nejvýše požadovaná frekvence, časově omezené, žádný redraw při každé hraně; kompatibilní se stávajícím SSD1306.

### 5.3 Navržené rozvržení zdrojů

```text
src/
  app/            transport_state, session, events, motor_sense, app_main
  hal/            board_pins, cmt_tx_rmt, cmt_rx_capture, sd_spi, oled, buttons
  audio/          waveform, pulse_profile, wav_sample_converter, edge_codec
  formats/        file_format, mzf, mzt, quickdisk, tap, wav, lep_l16,
                  mz_loader_profiles, mz_tape_decoder, mz_title, sidecars
  play/           play_engine, playback_controller, loader_handshake, mzt_selector
  record/         record_engine, mzf_decoder, record_autoname, record_writer
  storage/        sdcard, directory_browser, atomic_record_file, file_session
  ui/             browser, file_detail, play, record, menus, settings, status
  config/         nvs_settings, schema_migration
  diagnostics/    counters (pouze účelné; nezahlcovat sériový výstup)
tests/            host golden waveform/format/roundtrip, fuzz, state-machines
 docs/            PARITY_MATRIX.md, HARDWARE.md, TIMING_PORT.md, TEST_REPORT.md
```

Struktura je návrh, ne povinná jména souborů. **Jeden zdroj pravdy** pro formáty, profily, hranice rekordů a konverzi jednotek; neudržovat dvě nezávislé a rozcházející se tabulky timingů.

## 6. OLED a pět tlačítek – funkční ekvivalent LCD menu, lepší ergonomie

SSD1306 128×64 umožňuje přibližně **21 znaků × 8 řádků** ve fontu 6×8. Zachovat existující pětitlačítkový hardware a všechny akce LCD Keypad Shieldu mapovat na `PLAY, STOP, NEXT, PREV, REC` pomocí krátkého/dlouhého stisku a kontextu, bez nového tlačítka. Vytvořit explicitní keymap tabulku se všemi obrazovkami; žádné konflikty s emergency STOP. Ověřit debounce/repeat/long-press a chování při držení tlačítka během přechodu obrazovek.

Obrazovky:

1. **Browser:** 5–6 souborů/složek, posuv, cesta, index, typ, indikátor sidecar `I`, přehled obsahu MZT/QD a názvů rekordů; ukazovat SD chyby.
2. **Detail souboru / selector:** MZT a QD index, titul, datová délka, nastavený/sidercar profil, odhad doby; vyber konkrétní rekord a přehrávej právě ten. Per-record status `MTI`.
3. **PLAY:** název/rekord, stav PLAY/PAUSE MOTOR/PAUSE USER/WAIT MOTOR/FINISHED, režim, timing profil, postup/bar, zbývající či uplynulý čas, fronta/underrun a SD stav.
4. **RECORD:** formát, mód MOTOR/AUTO/MANUAL, čas, fronta, live zjištěný titul, rychlost/profil (MZ/IC/TC/CPM/Sinclair), průběh dekódování MZF a chyby. Zobrazit i probíhající finalize.
5. **Menu PLAY:** NORMAL 1:1–1:4, MZ700, IC/TC, UL/UL8/UL7, AUTO, invert, motor/autoplay a TAP speed.
6. **Menu RECORD:** WAV 22.05/44.1, LEP, L16, MZF; MOTOR/AUTO/MANUAL, AutoName, související nastavení.
7. **SYSTEM:** HW status / verze / velikosti pamětí / SD ready/retry / nastavení; NVS persist.

Prvotní cíl není barevná ani graficky složitá animace; grafika a ikonky jen tam, kde pomáhají rozpoznání stavu. Priorita je rychlá reakce a čitelná stavová signalizace.

## 7. Převzetí parserů/algoritmů – upozornění na hraniční případy

### MZF/MZT/M12

- Hlavička 128 B, metadata jméno/adresy/délka, plné oddělení dat/bloků, kontrola zkrácení a přetečení. V MZT nepředpokládat, že zdroj může být slepě přehrán jako jeden MZF.
- MZT mini-browser musí podporovat **samostatnou volbu profilu pro každý rekord**, kombinace UL a normálních rekordů, reset časovače/odpočtu po výběru, konzistentní chování STOP/RETURN.
- M12: zachovat skutečnou podporu zdroje a loaderů z reference, ne pouze přidat koncovku `.M12`; `M2I` má totožnou sémantiku jako MFI.

### QuickDisk

- Detekce podle obsahu/struktury, ne jen přípony `.QD`. Podporovat MZQ logical, QDF CRC a rozpoznávané fyzické/HxC/FlashFloppy varianty přesně podle referenčního parseru. Nepřidávat možnost zápisu do image.
- Pro velký obraz zpracování streamovat, nepřidělovat celé image do RAM. QD directory/metadata a náhled lze držet v PSRAM/poolu. Správně ošetřit nevalidní CRC/offset/cyklické ukazatele/soubory 0 B/cancel analýzy.

### LEP/L16

- Zachovat přesný *on-disk* encoding znaménkových 8bit intervalů, samostatné 0 extension, placeholder/tail režim a délky souborů. Ověřit kompatibilitu se soubory vytvořenými SD2CMT2 a MZTools oběma směry. Pozor na souběžné seek-back patchování placeholderů: zapisovat v cache/staging, ne provádět `fseek/fputc` z ISR či pro každou hranu.

### AutoName a dekódování

- Zkopírovat zejména rozlišování short+long mark, 1:3 vs 1:4, rychlý prvotní lock, ochranu lead tone bufferu a kontrolu dekódovaných headerů. V referenci `record_autoname.cpp` používá detekci MZ, pomocné Sinclair/CPM a profilové mapování, včetně následného přiřazení názvu.
- Vždy **oddělit přijetí všech hran od CPU dekódovací zátěže**: dekódování má omezený budget za servisní cyklus; při rychlém leaderu se nezahltí queue. Prioritní zápis surového zdroje, AutoName sekundární; drop AutoName stavu je přípustný jen pokud se otevřeně signalizuje, ale ztráta samotného záznamu nikoliv.
- Plné nahrávání do MZF vyžaduje validní data/checksum a bezpečné řešení duplicitních datových bloků a přerušení. Při neplatné pásce nesmí vzniknout falešně validní MZF bez chyby; raw WAV/LEP/L16 jsou fallback pro archivaci.
- V referenci `mz_tape_decoder.cpp` existuje `DECODE_DUPLICATE_GAP` a recovery; nesmí se portem ztratit.

### SD a názvy souborů

- `/RECORDINGS` pouze jednou vytvářet/ověřovat, bezpečně sekvenční RECxxxx, dlouhé jméno AutoName dle FAT pravidel, kolize a přejmenování sidecaru MFI.
- Nedokončené soubory odstranit po Cancel a po prokazatelném write error; po korektním STOP finalizovat WAV header, flush/sync/truncate, uzavřít FS, až poté zobrazit FINISHED. Při beznapěťovém výpadku pokud možno zachovat obnovitelný temp/pending stav; neprezentovat to jako zaručenou transakci FAT.
- SD removal v libovolném stavu musí zastavit fyzickou CMT cestu, oznámit chybu, invalidovat handly, dovolit pozdější reinit bez restartu. Otestovat zejména **po dokončení záznamu → vstup do browseru**, dříve problematický `dir fail / EMPTY / REC ERROR MKDIR FAIL`.

## 8. NVS / konfigurace / diagnostika

- NVS s magic/version, validací hodnot a migrací při změně schématu; nenahrazovat EEPROM raw adresováním z AVR. Výchozí hodnoty bezpečné, po resetu nastavení zachovat. Nedělat NVS write při každém scrollu či změně stavu, zapisovat pouze potvrzené hodnoty.
- Všechny problémy s DMA/FIFO/SD a stavovými přechody musí být zjistitelné, ale omezit diagnostiku na účelné měřitelné hodnoty: aktuální náplň/max obsazení front, underrun/overrun, počet write errorů, délka přerušení služby SD, heap/stack high-water. Nezavádět obrovské logovací buffery ani logování z ISR.
- Po neúspěšné inicializaci RMT DMA zkusit ověřený fallback, ale **UI musí transparentně ukazovat režim** a firmware se nesmí tvářit, že běží DMA, když neběží. První stabilní vydání nesmí zůstat na fallbacku bez zdůvodnění.

## 9. Plán implementace (bez vynechání konečného rozsahu)

**Krok A – inventura a návrh:** vytvoř `PARITY_MATRIX.md`, porovnej všechny `.cpp/.h` reference s cílovým projektem, identifikuj přesné on-disk formáty, HW chování, všechny menu a testové vektory; vyznač, které díly se zachovají/přepíšou. Potvrď zapojení, časy a zdroje ESP32. Zmraz pinmap. Žádné změny do referenčních repozitářů.

**Krok B – HAL a realtime engine:** vyčleň board HAL; implementuj/otestuj RMT TX DMA v nekonečném návazném streamu, RX HW timestamp backend, sdílený transport state machine a latence. Zruš no-op RMT obálky; zachovej pouze deklarovaný fallback. Ověř na syntetických pulzech i bez SD/UI.

**Krok C – kompatibilní přehrávání:** portuj profily a přesné tick konverze, MZF/MZT/M12 a individuální record selector, MFI/M2I/MTI, MZ700/IC/TC/UL, QD formaty, LEP/L16, WAV, TAP. U každého proveď host waveform test před pokračováním.

**Krok D – kompletní záznam:** WAV22/44, LEP/L16 bez residualu, MZF dekódování, AutoName a klasifikace. Jednotná přerušení/paused/MOTOR/AUTO. Před buffer/AutoName optimalizací zajisti RAW capture bez ztrát při dlouhém leaderu.

**Krok E – filesystem, settings a OLED:** přenes všechna menu do pěti tlačítek/SSD1306, robustní SD reinit, názvy/sidecars/kolize, NVS; optimalizuj runtime paměť a latence UI; odstraň mrtvý/starý kód.

**Krok F – integrace, regresní testy, HW protokol:** kompletní matice parity; žádné "bude doděláno". Každý menší přenositelný celek samostatný commit s odkazem na ověřovací výstup. Seřaď commity tak, aby se daly zkoušet. Pokud něco vyžaduje nový HW, vyznač `BLOCKED` a neskrývej za placeholder.

Pokud kontext/code budget neumožní realizaci celé parity v jednom běhu Codexu, dokonči bezpečně zvolený krok a zanech přesný `HANDOFF.md` s rozpracovanými soubory, otevřenými problémy, next steps a checklistem. **Neoznačuj dílčí build či funkční MZF playback za „hotový port“.**

## 10. Povinné testy a akceptační kritéria

### Host/unit tests bez skutečné desky

1. **Golden vektory** pulzů pro všechny 17 `loader_mode_t` včetně AUTO řešení a UL parametrů; hodnoty vychází z reference, kontrola polarity, leader/mark/data/gap/checksum, Z80/AVR ticks → nový časovač, drift. Toleranci volit dle zvoleného HW ticku, nezakrývat průběžný drift.
2. Round-trip **LEP→edge→LEP a L16→edge→L16** s pulzy typickými pro 1:1, 1:2, 1:3, 1:4, IC, TC, dlouhými idle a hraničním 127/extension; neopravovat každou druhou hranu přeneseným residualem. Vstupní realistické pulzy 3:1/4:1 nesmějí po 1 a více generacích RECORD→PLAY systematicky změnit HIGH/LOW.
3. WAV 44.1k/22.05k PCM konverze, fyzické přechody, správný RIFF; nahrávací phase accumulator nedělá overflow ani sample drift.
4. MZF header/data/checksum, neplatná délka, duplicitní blok, prázdné/chybné soubory, validní dekódování do MZF; bez nepozorované false positive.
5. MZT rekordy s různými profily, `.MTI` individuálně, M12 `.M2I`, MZF `.MFI`, chybné `.MFI/.M2I/.MTI`, fallback NORMAL 1:1, správný `I` ukazatel.
6. QuickDisk MZQ/QDF/QD analýza a výběr, záměrně poškozené CRC, extrémní offsets, limit počtu rekordů, přerušení analýzy.
7. ZX TAP flag/header/pilot/sync/bit/gap a speed options; neplatné délky a bezpečný EOF.
8. UI/state machine: pět tlačítek s long/short/repeat, MOTOR HIGH/LOW, user pause má prioritu, STOP cancel vs save, INSERT/REMOVE, replay a reinit.
9. Reentrant/event tests: krátké opakované PLAY/STOP/REC, odstranění SD během streamu, reset v každém stavovém uzlu; bez dangling pointer, double-free, starého stale file pointeru.
10. Fuzz parserů (MZF/MZT/QuickDisk/sidecars/TAP/WAV) pod sanitizéry host buildů a měření peak RAM; testy musí být malé a oddělené od velkých přímých `#include` produkčních souborů.

### HW testy – ESP32-S3 + skutečný Sharp MZ-800, případně MZ-700

- Osciloskop/logic analyzer na GPIO4/5/6/7: short/long HIGH/LOW, rozdíl výstup při současném SD zápisu a OLED refresh, jitter, rušivé výpadky u RMT DMA refill, leader i dlouhý idle.
- Přehraj NORMAL 1:1–1:4, IC 1:1–1:4, TC 1:1–1:3, MZ700 1:1 a FAST3, všechny UL varianty, MZT s mixem loaderů a jednotlivý QD record; testuj load do reálného počítače, ne jen na osciloskopu.
- Záznam ze skutečné CMT/Sharpu do WAV/LEP/L16/MZF, AutoName pro všechny referenčně podporované kombinace, následné vícečetné RECORD→PLAY→RECORD, **zejména problematická 3:1 a 4:1**.
- Při dlouhém leaderu 1:4 zkontrolovat peak RX queue – plná capture fronta před rozpoznáním hlavičky je regresní chyba. Měřit SD zápis s pomalou kartou, max latenci DMA refill a hladiny front.
- MOTOR pauza a obnovení při hraně, ruční STOP a dlouhý STOP, vytažení a návrat SD po záznamu, úspěšné obnovení adresáře, žádné `EMPTY`/`MKDIR FAIL`/falešné úspěchy.
- UL handshake s různým stavem MZ a timeoutem; po každém neúspěchu obnovit SENSE/READ do bezpečného idle bez resetu.
- Praktické testy 8MB PSRAM / 16MB flash a bez PSRAM jen jako diagnostický scénář, pokud je cílová deska vyžaduje; po hodinovém běhu a opakovaných stop/start žádný rostoucí heap leak/fragmentace.

**Akceptace:** full parity matrix = DONE + odpovídající test; pro časově kritické funkce také HW-TESTED. Zero silent lost edges, zero silent FIFO overruns, bez náhodných glitchů na READ, bez změny aktivního CMT protokolu, plná zpětná kompatibilita souborů a bezpečné ovládání. **Build ani unit test sám neprokazuje HW kompatibilitu.**

## 11. Co Codex musí odevzdat

- Změněné, buildovatelné a dokumentované zdroje MZCMTEmu na oddělené větvi / PR (nikoli push přímo do hlavní větve bez kontroly).
- `docs/PARITY_MATRIX.md` – řádková 1:1 matice vč. odkazů na testy a otevřených HW limitů.
- `docs/HARDWARE.md` – schéma pinmap, 3,3/5V ochrany, stavy READ/WRITE/MOTOR/SENSE/SD detect/monitor, uživatelské ovládání.
- `docs/TIMING_PORT.md` – převod referenčních timer ticks, validace RMT/MCPWM, kvantizace, DMA a zachování polarity; dokumentovat odchylky vůči referenci.
- `docs/TEST_REPORT.md` – přesné build/test příkazy, výsledky, verze nástrojů/ESP-IDF, časy/odchylky/peak buffer, neprovedené HW testy oddělit od provedených.
- Testové vektory, malé host-only testy a jejich spustitelné příkazy; žádné rozsáhlé binární fixture přidávané bez odůvodnění.
- Čistý `README.md`, aktuální reálný seznam funkcí, usage a menu keymap, omezení.
- Souhrn změn, rozhodnutí zachovat/přepsat modul, seznam odstraněného mrtvého kódu a důvody.

### Konkrétní pořadí čtení před změnami

**Referenční portovat:**
- `src/play/mzf_playback.cpp`, `mzf_loader.cpp`, `play_controller.cpp`, `play_engine.cpp`, `edge_playback.cpp`, `tap_playback.cpp`, `tap_speed.cpp`
- `src/record/record_engine.cpp`, `mzf_record_engine.cpp`, `edge_record_driver.cpp`, `edge_record_engine.cpp`, `wav_record_engine.cpp`, `record_autoname.cpp`
- `src/formats/file_format.cpp`, `mz_tape_decoder.cpp`, `mz_tape_profiles.cpp`, `mz_loader_profiles.cpp`, `mz_title.cpp`, `mzi_sidecar.cpp`, `wav_reader.cpp`
- `src/drivers/sdcard.cpp`, `mzio.cpp`, `monitor_mute.cpp`, `calibration_store.cpp`; všechny `src/ui/*.cpp`
- `guide/CMT_TIMING_REFERENCE.md`, `guide/CMT_INTERCOPY_TURBOCOPY_TIMING_REFERENCE.md`, `guide/MFI_M2I_MTI_FORMAT.md`, `guide/USER_HANDBOOK.md`

**Cílové review a nahrazení/integrace:**
- `src/main.c`, `src/file_list.c`, `src/audio/mzf_stream.c`, `player.c`, `recorder.c`, `edge_capture.c`, `edge_capture_rmt.c`, `raw_pulse.c`, `raw_timer.c`, `raw_capture.c`, `edge_writer.c`, `edge_stream.c`, `wav_reader.c`, `wav_writer.c`, `src/drivers/sdcard.c`, `oled.c`, `i2c.c`, `buttons.c`, `src/ui/ui.c`
- `platformio.ini`, `sdkconfig.defaults`, `src/CMakeLists.txt` pro verzi ESP-IDF, component deps, IRAM/PSRAM/DMA a oddělení host-test sestavení.

### Zdroje o možnostech periferií ESP32-S3

- ESP-IDF RMT – https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/rmt.html
- ESP-IDF MCPWM capture – https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/mcpwm.html
- `soc_caps.h` pro ESP32-S3 – https://github.com/espressif/esp-idf/blob/master/components/soc/esp32s3/include/soc/soc_caps.h

**Výsledná priorita:** kompatibilita se Sharpem a spolehlivé časování > funkční parita a bezpečné nahrávání > robustní SD > čitelná OLED > kosmetika. V první cílové verzi ale musí být splněna celá matice parity; optimalizace nesmí být výmluvou pro chybějící funkce.
