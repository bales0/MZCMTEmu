# Zadání pro Codex — MZCMTEmu: OLED UI, browser, RECORD capture, PLAY buffer a metadata programu

**Datum:** 13. 11. 2026  
**Cílový repozitář:** https://github.com/bales0/MZCMTEmu (`main`)  
**Výchozí analyzovaný commit:** `d3f67c4a978a38dad23e49c918b145182046452b` („backup“)  
**Referenční funkční projekt (pouze ke čtení):** https://github.com/bales0/MZ-SD2CMT2-Reborn , commit `600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb`  
**Cílový hardware:** ESP32-S3-WROOM **N16R8** (16 MB flash, 8 MB octal PSRAM), SSD1306 OLED 128×64 I²C, SD SPI, Sharp MZ CMT.

## 0. Zásadní pravidla práce

1. Nejprve proveď vlastní **read-only audit aktuálního HEAD** a porovnej s tímto commitem; pokud HEAD pokročil, zachovej mezitím implementované opravy. Zdokumentuj zjištěné problémy a plán. Následně **implementuj uvedený rozsah pouze v `MZCMTEmu`**, ideálně v samostatné větvi/PR. Neměň referenční SD2CMT2-Reborn, MZTools ani jiné projekty.
2. Udrž plnou kompatibilitu stávajících formátů a funkcí; žádné změny pinoutu, zapojení, polarity bez prokazatelného důvodu a schválení. Nezaváděj nové HW požadavky na desku.
3. Preferuj malé přehledné moduly a jeden sdílený model metadat; žádný další monolit v `main.c`. Stávající testy zachovej, doplň cílené nové. Nevyžaduj nová obrovská binární fixture v repozitáři — vytvářej krátké syntetické průběhy a soubory z definovaných testovacích vektorů.
4. Nespouštěj `MZTools` build/testy, nesahej na jeho zdroje. Build a host testy **cílového** `MZCMTEmu` po implementaci proveď, jsou-li k dispozici; HW úspěch bez skutečné desky neprohlašuj.
5. Vyhodnoť paměť, využití CPU a dopad nového UI/metadata analyzátoru na nejrychlejší CMT pulzy, RMT DMA a SD. Nekompromituj spolehlivost transportu kvůli grafice nebo analýze metadat.

## 1. DŮLEŽITÉ: skutečný hardware a S1 — NENÍ TAM ŽÁDNÉ RELÉ!

**Mechanický přepínač S1 (ve schématu vícepólový S1A–S1D) sám fyzicky volí cestu interního emulátoru versus externího CMT.** ESP32 **pouze snímá jeho polohu přes GPIO21**; nemá relé, ovládaný multiplexer ani výstup pro přepínání trasy. Software NIKDY „nepřepíná CMT“ elektricky.

| Signál | GPIO | Pevná role |
|---|---:|---|
| READ | 4 | Výstup do MZ; klidový stav LOW |
| WRITE | 5 | Vstup z MZ přes existující neinvertující převodníky |
| MOTOR | 6 | Vstup |
| SENSE | 7 | Výstup; zachovat handshake UL a současnou logiku |
| PLAY, STOP, NEXT, PREV, REC | 8, 9, 10, 11, 42 | Pět tlačítek proti GND |
| SD CS/SCK/MOSI/MISO | 13/14/15/16 | SPI2 SD, DMA |
| OLED SDA/SCL | 17/18 | SSD1306 128×64, adresa 0x3C, 400 kHz |
| SWITCH | 21 | **JEN VSTUP** detekce S1, žádné ovládání relé |
| MONITOR | 41 | Stávající HW mute/enable monitoru |
| SD DETECT | 47 | Vstup, vložená karta LOW |

- Zachovej stávající debounce (nyní ~40 ms), bezpečné výstupní úrovně a pravidla, že **v poloze EXTERNAL** nelze interní PLAY/REC, aktivní interní transport se ukončí, READ/monitor/SENSE zůstanou v bezpečném stavu. Reagovat při změně S1 bez restartu zařízení. S1 hardwarově přepíná okamžitě; při přechodových stavech nespoléhej na software jako na fyzické odpojení vodičů. Dokumentuj a ověř chování při mechanickém přepnutí během PLAY, RECORD a UL. Nekonstruuj „software switch“ ani nastavení, kterým se S1 ovládá; menu může jen zobrazovat polohu a instrukci „USE SWITCH S1“.
- Současný `src/app/route_switch.c` považuje GPIO21 LOW za EXTERNAL. Potvrď podle skutečného schematu a HW, nikoli odhadem. U1/U2 jsou podle dosavadního HW potvrzení neinvertující; nepřidávat slepou inverzi signálů.
- Při probíhajícím záznamu zajisti korektní dokončení/zrušení a nepředstírej úspěšný záznam po změně polohy. Při odpojení SD během finalizace nikdy nezobraz `SAVED` bez potvrzeného uzavření souboru.

## 2. OLED: přejít od pouhé textové tabulky k využití grafiky 128×64

Dnes: `src/drivers/oled.c` framebuffer 1024 B, 6×8 font, 8 řádků; `src/ui/ui.c` pouze porovnání textových řádků a aktualizace změněných SSD1306 pages. To je dobrý základ — **zachovat dirty-page/bounded I²C**, ale rozšířit o pixelové vykreslení (text, miniaturní/dvojnásobný font, inverze výběru, vodorovné bary, oddělovače, stavové ikony). Nepotřebujeme velkou grafickou knihovnu ani další framebuffer.

Požadavky:

- Malé, jasné nadpisy/ikony stavů `PLAY`, `REC`, `PAUSE USER`, `PAUSE MOTOR`, `WAIT MOTOR`, `SAVING`, `ERROR`, `EXTERNAL`, `NO SD`.
- Vybraný řádek browseru jako **inverzní zvýrazněný obdélník**, ne pouze `}` před názvem; při 128×64 zachovat dobře čitelnou strukturu a vhodný počet položek (cílit na 5–6 + hlavičku/stav, případně přepínat kompaktní 7položkový režim, pokud je přínosný). Neztratit indikaci složky, čísla položky a polohy v seznamu.
- Opravdové pixelové progress bary: procento PLAY, data progress, RX/TX buffer. Rozlišit „pozici v souboru“ od „obsazení bufferu“; bar bufferu nesmí nahrazovat průběh souboru.
- Grafické rozvržení optimalizované pro 128×64; neškálovat např. 320×320 layout z jiného projektu. Žádné dlouhé blokující animace; průběžné překreslování jen při změně. Bez paměťových alokací ve vykreslovací smyčce.
- **Opravit chybějící vykreslování overlay:** v `src/main.c` existují `ui_overlay_status` a `ui_overlay_until`, ale proměnné se jen zapisují/mažou a nikde se nečtou do renderu. Zprovoznit skutečné zobrazení např. `MOTOR`, `EXTERNAL CMT`, `IMAGE ERROR`, `CANCELLED`, `SETTINGS ERROR`, `BUFFER LOW`, `EDGE LOSS`, s prioritami a expirací (trvalá hlášení `duration_ms==0`), nevymazat důležitý ERROR pouhou obnovou obrazovky. Nepotlačit browser/record/play status nečekaně.
- Opravit KEYMAP překlep: `STOP: cmt_file_t BROWSER` -> smysluplný text.
- Posouvání dlouhých názvů na vybraném řádku musí **začínat od začátku názvu po změně výběru/obrazovky**, ne podle absolutního uptime (`ui_get_scroll_offset` používá `xTaskGetTickCount()` od bootu). Pozastavení například ~700 ms, krok cca 180 ms, možná delší klid na návratu. Nevybrané názvy zůstanou statické.
- Zvláštní pozornost: původní kód aktualizuje jen změněné textové stránky. Po přechodu na pixelový renderer znovu vyhodnocovat dirty bitmapu správně; při chybě I²C ponechat failed pages dirty, při návratu OLED/SD obnovit celý snímek podle potřeby. Bez celostránkového I²C překreslení 20× za sekundu, pokud nic nebylo změněno.
- Nastavení jasu z NVS zachovat, včetně chyby komunikace; žádné přidání SPI OLED, není v HW.

### Orientační obsah obrazovek (ne závazný počet pixelů)

**PLAY Sharp:** `▶ PLAY   MZT 2/5`, `DIZZY 5`, `TYPE: OBJ  LOADER: IC 1:4`, `SIZE: 32768`, `LOAD: 1200  EXEC: 1200`, `progress ███░░ 56%`, `TX RESERVE 82%`, `MOTOR AUTO`.

**RECORD AutoName:** `● REC   WAV 44k`, `DIZZY 5`, `TYPE: OBJ   IC 1:4`, `SIZE: 32768`, `LOAD: 1200 EXEC: 1200`, `DATA: 36%`, `RX FREE: 91%`, `00:38`.

**TAP:** `▶ TAP 1800Bd`, `MANIC MINER`, `ZX CODE` / `ZX BASIC`, `SIZE`, `LOAD` nebo `AUTOSTART LINE`, `EXEC: --` (pokud není součástí ověřených metadat), progress + TX buffer.

Nemusí být vše současně na jedné obrazovce: použij promyšlené kompaktní rozvržení, přepínané status/info stránky jen tehdy, pokud nenaruší dosavadní klávesovou mapu. Preferovat trvale viditelný název, formát, rozpoznaný loader, hlavní parametry a buffer. Text `--`/`?` pro neznámé informace; nikdy nevymýšlet hodnoty.

## 3. RX indikátor a RECORD engine

### 3.1 Skutečný bezpečný RX buffer

Dnes `recorder_get_buffer_ok_percent()` v `src/audio/recorder.c:581–589` vrací volnou kapacitu FIFO MCPWM událostí (8192 slotů, 8191 použitelných). To je správný význam `BUF`, ale UI v `src/main.c:1076–1097` vyhlazuje klesající volnou rezervu na max. **−8 procentních bodů každých 500 ms**. Tím může v kritickém okamžiku zobrazit 92 %, i když ve skutečnosti zbývá jen 20 %!

- **Pokles volné kapacity zobrazovat okamžitě**; případně vyhlazovat jen zotavování (růst) a jen vizuální bar. Kritický alert se vyhodnocuje výhradně z reálné hodnoty, nikoli ze zpožděného display filtru.
- `RX FREE 100%` = prázdné FIFO; `0%` = téměř plné. Vypsat správně zdroj metriky; neprezentovat ji jako celkovou volnou RAM nebo SD buffer.
- Doplnit minimální hodnotu volné rezervy od začátku záznamu (`RX MIN` / volitelně v INFO) a sticky příznak `RX OVERRUN` / `EDGE LOSS`. Hranice např. `LOW` pod 25 %, `CRIT` pod 10 %; dávkování dynamiky nezakrývá alarm.
- Měřit samostatně `WAV staging` / storage queue tam, kde je to důležité, ale jejich procenta nenazývat `RX FIFO`; jiné buffery nezaměňovat za stejný ukazatel. Pokud se na OLED nevejdou, uveď podrobnosti v debug/test statistikách nebo INFO, bez nadměrných trvalých diagnostických obrazovek.
- Při výpadku ISR záznamu či přeplnění FIFO nevydávat „RECORD SAVED“ jako potvrzení dobrého signálu. Zvolit konzistentní chybu (např. `EDGE LOSS` vs `RX OVERRUN`) a bezpečné ukončení/recovery v souladu se stávajícími pravidly pro chybné/nedokončené záznamy.

### 3.2 Nezávislé zjišťování ztráty hran — PCNT vs MCPWM

**Zachovat MCPWM Capture jako výchozí přesný RX backend.** Neodstraňovat jej kvůli RMT RX; na RX straně RMT existuje problém dlouhých úrovní a restartu, který není 1:1 vyřešen tím, že TX umí dlouhou úroveň rozdělit do více symbolů.

Doplnit **volitelně zapínanou nezávislou HW kontrolu hran pomocí PCNT** na stejném GPIO5 (interní GPIO matrix; žádný další vodič). PCNT počítá náběžné i sestupné hrany a počet se porovnává s počtem MCPWM eventů **úspěšně převzatých do FIFO**. Ztracené události v HW latch se nemusí projevit v `overrun` softwarové FIFO a PCNT je může odhalit. Výkonová režie musí být minimální (žádný PCNT ISR při každé hraně).

Implementační požadavky:

1. Před implementací ověřit pro **ESP32-S3 + ESP-IDF 6.0.1** reálnou podporu PCNT, interní routování téhož GPIO, filtry, rozsah čítače a `accum_count`; vyhnout se nepodloženým API. PCNT HW šířka je omezená a souběžné přetečení nesmí dát falešný souhlas počtů.
2. Počet `mcpwm_enqueued_edges` inkrementovat až po úspěšném vložení události. Oddělit počet eventů od počtu **časových intervalů**: při PAUSE/STOP se do FIFO může přidat „tail“ interval bez fyzické hrany; ten se nesmí počítat. Synchronizovat start/stop/reset/pause/resume obou periferií s jasně definovanými měřicími epochami a startovní úrovní.
3. Upozornit, že nesynchronní čtení PCNT během záznamu může krátce převýšit počet MCPWM ISR kvůli čekajícímu přerušení; průběžné porovnání je orientační, závěrečné porovnání provést po zastavení/odfiltrování hran z přechodového okamžiku a po dokončení legitimně čekajících ISR.
4. Použít shodné filtrování zákmitů, nebo filtry vypnout a výsledky posoudit; PCNT není automatický důkaz absolutní bezchybnosti pulzních délek. Ztráta jedné a dvou hran, včetně slepé ztráty sudého počtu, musí být testovatelná.
5. Pokud nelze přesně a bez závodu synchronizovat měřicí epochy na obou periferiích, **neimplementovat falešnou garanci**: zaznamenat omezení, zavést diagnostickou „suspected edge loss“ a u zásadní nejednoznačnosti výsledku fail-safe. Nenechat falešné alarmy běžně mazat záznam. Samotný PCNT **neobnoví** ztracené timestampy.
6. Podržet ISR IRAM-safe, bez malloc/FreeRTOS blocking, bez SPI/I²C/FS. Nezavádět kritické sekce, které zbytečně prodlouží latenci na druhém jádře. Prověřit `CONFIG_MCPWM_ISR_CACHE_SAFE`, prioritu IRQ, změny v `edge_capture_pause()`, `edge_capture_resume()`, `edge_capture_stop()`, případné přečtení latch, 32bitové wrapy a dlouhé pauzy.
7. Při neshodě, která byla **spolehlivě potvrzena**, zobrazit `EDGE LOSS`, sticky error v RECORD stavu a zajistit bezpečný výsledek souboru. Není žádoucí tichá korupce dat.

### 3.3 Záznam a finalizace

- Zachovat `WAV 22.05/44.1k, LEP, L16, MZF`, MOTOR/MANUAL/AUTO, AutoName, duplicitní kopie, `SAVING RECORD`, nedestruktivní AutoName rename + MFI sidecar. Do ISR nesmí vstupovat filesystem či dekodér.
- Zachovat nezávislou kvantizaci každé HIGH/LOW délky pro LEP/L16 (bez reziduální korekce mezi pulzy); fázový akumulátor pro WAV vzorkování je naopak legitimní.
- Počátek/končení záznamu, MOTOR PAUSE a USER PAUSE nesmějí vytvářet falešné hrany nebo započítat dobu pauzy do intervalu. Testovat krátké pulzy okolo hranice PAUSE a dlouhé intervaly nad 53,7 s (wrap 32bit/80MHz timestampu).
- Oddělit aktualizaci/UI metadat od real-time příjmu; fronty, stavové přechody a chybové hlášky musí být deterministické.

## 4. PLAY buffer — zobrazit opravdu použitelnou rezervu, ne pouze progress souboru

V `src/audio/raw_pulse_rmt.c` jsou `raw_pulse_level()`, `raw_pulse_free()`, `raw_pulse_is_underrun()`, `raw_pulse_is_running()`; `src/audio/player.c` používá pulse FIFO + RMT TX DMA a vlastní úlohu na CPU0. Aktuální OLED hlásí jen `PLAY BUF!` až při skutečném underrunu; chybí průběžný stav TX fronty.

- Doplň čitelný indikátor **TX BUFFER** s jasnou definicí metriky. Měř, co skutečně hrozí underrunem: hladinu SW pulse FIFO, a pokud je to pro kontinuální RMT DMA zásadní, také zohledni, že HW RMT DMA má již zakódované symboly. **Pouhé procento počtu položek FIFO nemusí odpovídat časové rezervě**, protože jedna položka může představovat 20 µs nebo mnoho sekund. Preferuj primární metriku `TX RESERVE` v µs/ms předbufferovaného signálu, pokud ji lze správně, levně a konzistentně počítat; sekundárně zobraz `% FIFO`. Není-li přesné časové vyčíslení možné, nepředstírej jej a jasně označ procento SW FIFO.
- Zajistit nerozporuplné stavy `PREFILL`, `PLAY`, `PAUSED USER`, `PAUSED MOTOR`, `BETWEEN BLOCKS`, `UL HANDSHAKE` (v UL handshake může být RMT SW FIFO nulové zcela správně), `EOF`, `UNDERRUN`, `ERROR`.
- **UL a dlouhá mezera mezi bloky**: prázdná FIFO nesmí spouštět falešný kritický alarm, jestliže právě běží platný stav bez pulzního TX. Při běžném RMT přenosu zachovat kontinuální bezproblémový DMA přísun.
- Žádné čtení nekonzistentního indexu/head-tail, vyhnout se drahému procházení celé FIFO každý UI frame; případné časové součty spravovat inkrementálně nebo snapshotem. Žádný přímý zásah UI do RMT hodin či přenosových dat.
- Zobrazený průběh PLAY souboru (`progress`) zůstane oddělený a bude vycházet ze skutečně odehraného/potvrzeného času nebo počtu bajtů, ne z načtených dopředu. Bez regresí přesnosti 16MHz RMT ticků.
- Výslovně **nepřepisovat současnou RMT PAUSE/RESUME architekturu jen v rámci UI**; doložit testem, že zobrazení bufferu ji neovlivňuje. Samotná HW retence symbolu po stopu hodin skupiny RMT zůstává samostatným akceptačním rizikem.

## 5. Metadata: jednotný model pro PLAY i RECORD AutoName

**Povinné pro všechny formáty**, kde lze skutečně rozpoznat program: `MZF`, `MZT`, `M12`, `MZQ/QDF/QD`, **`WAV`, `LEP`, `L16`, `TAP`**. Rozlišit možnost číst strukturovanou hlavičku přímo versus dekódovat pulzní signál. Vše musí mít bezpečné `UNKNOWN` a správnou indikaci zdroje a validity.

Navrhni malý sdílený model, např. `program_metadata_t` v novém `src/formats/program_metadata.[ch]`, nikoli jednu sadu UI proměnných pro každý formát. Minimální pole:

- formát zdrojového souboru; detekovaný protokol (`SHARP MZ`, `ZX SPECTRUM`, další potvrzený), zdroj metadat (`MZF HEADER`, `TAPE DECODE`, `TAP HEADER`, `LOADER EMBEDDED`, `FILENAME FALLBACK`), stav důvěry (`WAITING`, `LOADER ONLY`, `VALIDATED HEADER`, `VALIDATED PAYLOAD`, `UNKNOWN`, `INVALID`);
- programový `title`, skutečný `file_type` + textový popis / raw value, `declared_payload_size` (skutečná **velikost programu**, ne délka úvodního loaderu), `load_address`, `exec_address`, případně BASIC autostart line, record/block index a celkový počet, čas/bytes progress;
- **zvlášť** `transfer_loader_mode` (profil zvolený pro přehrávání), `recorded_loader_mode` (profil zjištěný z nahrávky), typ loader chain/stage, a skutečný cílový program. Nikdy nezaměňovat „přehrávám IC 1:4“ s „originální soubor je IC 1:4“.
- Každé volitelné pole má `valid` bit; **neznámé položky nevyplňovat nulou označenou jako skutečná adresa**. Např. `EXEC --`, u ZX BASIC `AUTO LINE` místo strojového `EXEC`.
- Snapshot modelu je synchronizovaný mezi player/recorder a UI; aktualizace pouze při skutečných událostech (header accepted, loader resolved, block changed, EOF/error), bez dynamických string alokací v real-time cestě. Název ze Sharp charset převádět existující funkcí `mz_title_decode_display()`.

### 5.1 KRITICKÝ BOD: některé loadery mají metadata programu v PRVNÍM PAYLOADU

**Nesprávné by bylo vždy vzít metadata z první 128B hlavičky a hned je zobrazit jako metadata cílového programu.** První hlavička i první datový blok mohou patřit pouze pomocnému loaderu (a reálný program s názvem/adresou/délkou je buď zakódován uvnitř tohoto bloku, nebo následuje až v dalších blocích). To platí zejména pro určité IC/TC a rychlé/turbo/UL varianty, kopírovací utility a kombinované nahrávky. Zpracuj vícevrstvou strukturu, nikoli naivní „první hlavička = program“.

V již existujícím `src/formats/mz_loader_profiles.c` najdeš důležité skutečné vzory:

- **IC**: `mz_loader_profile_recognize_ic()` validuje signaturu a normalizuje hlavičku; kanonická hlavička kopíruje **6 B z `header[0x1A..0x1F]` do offsetu `0x12`** (délka programu, LOAD, EXEC). Rychlost se identifikuje z dalších polí. Číst přes existující helper, nevzít slepě délku/LOAD/EXEC úvodního loaderu.
- **TC**: první hlavička popisuje loader o délce **90 B** a adresách kolem `0xD400`; skutečná **metadata programu jsou vložena do PRVNÍHO DATOVÉHO BLOKU TC LOADERU**, který musí být nejdříve přečten a validován. `mz_loader_profile_detect_tc()` a `mz_loader_profile_normalize_tc()` je znají: `loader_data[0x4B]` identifikuje rychlost, `loader_data[0x4C]` typ programu a **`loader_data + 0x4D`** obsahuje přenášené údaje včetně LENGTH/LOAD/EXEC. Teprve z validovaného a normalizovaného programu smí OLED ukázat konečné informace.
- V `src/audio/mzf_writer.c` už existuje state machine s `tc_pending`, která první TC loader payload sbírá před zahájením zápisu skutečného programu; `src/record/record_autoname.c` má obdobnou detekci. **Sdílet/odvodit tuto ověřenou logiku**, nerozmnožovat nezávislé offsetové heuristiky po UI.
- U MZ souborů s dynamickým turbo/UL generovaným loaderem musí být primární metadata přečtena **před případným přepsáním hlavičky** (`mzf_loader_patch_loader_header()`) a oddělena původní data programu od vygenerovaného „transportního“ loaderu.
- U fyzického WAV/LEP/L16 záznamu musí analyzátor sledovat sled hlavička → loader data → případná další hlavička/data a **umět dodatečně změnit zobrazená metadata po přečtení prvního payloadu**. Při nalezení první hlavičky loaderu zobrazit např. `LOADER DETECTED / SEARCHING PROGRAM`; NE „SIZE: 90 B, EXEC D400“ jako program. U formátů, které skutečné metadata neobsahují nebo je nelze validovat, ukázat `UNKNOWN`, ne heuristicky vyrobenou jistotu.
- U multipart MZT, QD/QDF a WAV se sekvencí více programů správně přepínat informace **při změně skutečného cílového programu/rekordu** a nepřenášet starý titul do nového. Nerozdělovat falešně loader a jeho payload na dva samostatné uživatelské programy.
- Neměnit stávající plně funkční logiku IC/TC/UL záznamu/normalizace. Implementuj extrakci metadat nad ověřenými daty, zachovej všechny checksum/recovery stavy.

### 5.2 MZF / MZT / M12 / QuickDisk PLAY

- U standardní Sharp hlavičky číst `file_type` na offsetu 0; 17B titul od 1; délka na **0x12–0x13**, LOAD na **0x14–0x15**, EXEC na **0x16–0x17** (little endian), až po ověření příslušného formátu/hlavičky. Vypsat např. `TYPE 01`, nebo existující ověřený textový popis typu; nezobrazovat `OBJECT` pro neznámý typ.
- Rozšířit `sharp_stream_info_t` (`src/audio/sharp_stream.h`) o typ/LOAD/EXEC a valid bit nebo vystavit bezpečný snapshot `program_metadata_t`; struktura již dnes má `title`, `record/records`, `payload_bytes`, `requested/effective` a `fallback`. Doplnit aktualizaci při každé změně MZT/QD rekordu, po `sharp_stream_continue_mzt()` a po výběru jiného loaderu.
- QuickDisk prohlédnout přes jeho validovaný logický/RAW header; nesahat do původního read-only principu. U `M12` ověřit skutečnou reprezentaci hlavičky oproti běžnému MZF a nepředpokládat posun bez kontroly.
- Nejdříve zobrazit spolehlivá metadata z původní source hlavičky, až pak změnit playback profil; hlavně neukazovat transformovanou loader hlavičku jako originální program.

### 5.3 WAV PLAY — detekce signálu

- Přehrávání `WAV` v projektu je 8bit PCM mono, pulse streamer používá prahy hystereze (`0x40`/`0xC0`) a převod do intervalů. Metadata nelze vyčíst ze standardního RIFF hlavičkového pole — program je v **zakódovaném CMT signálu**.
- Přednostně dej **pasivní analyzátor nad již vygenerovanými fyzikálními intervaly**, které streamer předává do přehrávače. Reuse ověřený Sharp dekodér z `mz_tape_decoder`, IC/TC normalizaci, případně pomocný rozpoznávač Sinclair/CP/M z AutoName. Analyzátor pracuje mimo ISR a neprovádí vlastní filesystem seek. Potřebuje správnou skutečnou polaritu výstupních impulsů, nezaměňovat user volbu fyzické inverze READ a logické WRITE vstupní dekódování.
- Zachovat vzorkovací časovou osu bez zaokrouhlovacího driftu. Dlouhé ticho se interpretuje jako oddělení/mezera; reset parseru dle pravidel existujícího dekodéru, ne plošně při každém několikams pulzu.
- Pokud metadata nejsou nalezena včas, PLAY **normálně pokračuje**; zobrazit `DETECTING...` a pak `UNKNOWN`, nikdy nezablokovat přehrávání. Nepřidávat synchronní předběžné skenování celého WAV, které by trvalo dlouho nebo poškodilo pozici přehrávání. Volitelný omezený background preflight jen se samostatným bezpečným handle a měřitelným časovým rozpočtem.

### 5.4 LEP / L16 PLAY — dekódovat přímo intervaly

- Tyto formáty explicitně ukládají délky fyzických HIGH/LOW intervalů (včetně speciální reprezentace dlouhých nulových úseků); nejde o MZF hlavičku. **Použít stejný pasivní sekvenční analyzátor jako WAV** nad intervaly `edge_stream_next_pulse()`.
- Dodržet 50 µs jednotky LEP a 16 µs jednotky L16; už existuje `edge_stream.c`, který při PLAY vytváří i syntetický předběh a doběh. **Syntetický preroll/tail nesmí být pokládán za reálný signál při analýze metadat.** Buď jej vynechat podle explicitní provenance flag, nebo analyzovat přímo reálné dekódované záznamové tokeny před přidáním syntetických segmentů.
- Správně interpretovat dlouhé intervaly, fyzické úrovně, prázdné/poškozené soubory; nevracet falešné informace o programu z náhodné datové sekvence.
- IC/TC „první loader payload“ řešit stejně jako ve WAV a RECORD, včetně pozdějšího doplnění TITLE/TYPE/SIZE/LOAD/EXEC.

### 5.5 TAP PLAY — ZX Spectrum, NE Sharp MZ

- `TAP` obsahuje strukturované bloky Sinclair. **Nespouštět nad TAP Sharp MZ dekodér.** Vytvořit striktní parser Spectrum header bloků s ověřením délky, flag byte a XOR checksum; napojit jej na existující validaci `tap_stream.c`, zachovat současné pilot/sync/data/gap timing a rozsah rychlostí.
- Standardní Spectrum header blok má flag `0x00`, následuje typ `PROGRAM/NUMBER ARRAY/CHAR ARRAY/CODE`, **10B jméno**, 16bit délku a 2 parametry podle typu; datový blok obvykle flag `0xFF`. U `CODE` uvést **LOAD**; u BASIC `PROGRAM` je parametr **autostart řádek** (pokud je meaningful), nikoli Z80 EXEC adresa. **TAP obecně neobsahuje spolehlivou EXEC adresu strojového kódu** — zobrazit `EXEC --` nebo `AUTO LINE`, ne vymyšlené číslo.
- Přímé čtení metadat TAP při `open`/preflight s validací, vyhnout se dalšímu neomezenému scan celého souboru. Podporovat více hlaviček/bloků; pro běžící blok zobrazovat odpovídající titul, jen pokud existuje správné párování header/data. Data-only TAP: `UNKNOWN` a počet/typ bloku, nikoli falešný název.
- Rychlost v baudech je nastavení **playback encoding** (`1400/1800/2100/2500`), nikoli metadatum uložené v TAP; nezaměňovat tyto pojmy.

### 5.6 RECORD AutoName + informace o programu

- Nyní `record_autoname_get_name()`, `record_autoname_get_detected_loader_mode()`, `record_autoname_get_detected_aux_profile()`, `record_autoname_get_payload_progress_percent()` poskytují jen část údajů. Rozšířit o **validovaný typ, velikost, LOAD, EXEC**, případně obsahový protokol a zdroj metadata; nevytvářet další neúspornou kopii decoderu.
- Sharp název nastavit až po validní hlavičce; u TC odložit zobrazení skutečné programové hlavičky až po validaci **prvního loader payloadu**; u IC použít normalizované vnořené pole; u běžné nahrávky zpracovat běžnou 128B hlavičku.
- Sledovat více logických bloků a oddělení programu; progres DAT ukazovat pro skutečnou velikost, ne pro zaváděcí loader. Jestliže je detekováno jen `LOADER`, indikovat `WAIT PROGRAM` místo podvrženého programu. Název souboru AutoName lze odlišit od originálního názvu v hlavičce.
- MZF RECORD zdroj už má `mzf_writer_header()` s výslednou kanonickou hlavičkou (v případě TC až po normalizaci); využij jej, ale **nepřepisuj informace o první skutečné nahrávce, pokud je hlavička zatím nevalidní**. Metadata z WAV/LEP/L16 dostupná nezávisle na tom, zda se AutoName podaří dokončit nebo přejmenovat soubor.
- U Sinclair/CP/M je `TITLE`/`SIZE`/`LOAD`/`EXEC` závislé na skutečně známé sémantice daného loaderu. Neukazovat Sharp offsety na nesharpových datech; rozpoznávač může znát jen jméno/rychlost, ostatní pole budou `--`.

## 6. Browser: opravy a odolnost

Kód `src/file_list.c` má vhodnou architekturu: **jednou** přidělený PSRAM index do 65 535 položek a 2 MiB arena názvů, bez malloc/free pro každou položku; řazení adresáře > soubory, skryté sidecary, návrat do rodiče. Toto zachovat.

- **Opravit poruchu `navigate()`/`load()`**: neúspěšné otevření podsložky obnoví pouze cestu, ale `load()` smazalo původní položky. Uživatel tak může skončit v původním adresáři s prázdným browserem. Použij transakční load do odděleného indexu (pokud RAM dovolí), nebo bezpečné obnovení předchozího adresáře po chybě; při nedostatku paměti měj explicitní plán. CANCEL se nesmí považovat za poškození SD.
- Při návratu do rodiče zachovat výběr právě opuštěné složky, při refresh zachovat rozumně aktuální výběr a scroll. Po změně souboru restartovat marquee. Neztratit možnost zrušit analýzu STOP.
- Velké adresáře (1000, 10000, až limit 65535) musí být ošetřené bez nekonečné blokace: současný `load` provádí `cmt_stat` **na každou položku** a následné `qsort`; zhodnoť profilovanou cenu na SD a podle výsledků omez volání nebo dávkuj s možností zrušení. Žádné hromadné alokování nových řetězců na každé přečtení.
- Zachovat všechny stávající filtry (MZF/MZT/M12/MZQ/QDF/QD/WAV/LEP/L16/TAP, skryté `.MFI/.M2I/.MTI/.MZI`), case-insensitive správné pořadí, max cesta 1024 B, bezpečné přetečení indexů a délky cest.
- Při souběhu PLAY + browser nezpomalit časování RMT; browser nesmí měnit aktivní playback file handle/seek. Při práci s SD držet jediné vlastnictví storage workerem.
- Opravit UI při nenamontované/odpojené SD a po opětovném vložení; nevytvářet falešný `DIR FAIL` jen kvůli CANCEL/odstranění karty.

## 7. Rozhraní, architektura a očekávaný rozsah změn

Předpokládané oblasti (názvy nových modulů může Codex zvolit lépe):

- `src/ui/ui.[ch]`, `src/drivers/oled.[ch]`, `src/main.c`: grafický renderer/stránky, pravdivé buffer indikátory, překryvy, metadata, info/keyboard, scroll.
- `src/file_list.[ch]`: transakční navigace, chyba adresáře, reset scroll, dlouhé seznamy.
- `src/audio/edge_capture.[ch]`, `src/audio/recorder.[ch]`: MCPWM/PCNT count-check, stavové chyby, pravdivý RX headroom, minimální headroom, stop/pause epochy, ISR bezpečnost.
- `src/audio/raw_pulse_rmt.c`, `src/audio/player.[ch]`: bezpečný snapshot TX headroom/časové rezervy a výjimky UL/gap.
- `src/formats/program_metadata.[ch]` (nové), `src/audio/sharp_stream.[ch]`, `src/audio/wav_pulse_stream.c`, `src/audio/edge_stream.c`, `src/audio/tap_stream.c`, `src/record/record_autoname.[ch]`, `src/audio/mzf_writer.c`: sdílený parser metadat, ověřená provenience, loader chain a prvotní payload, nezávislý pasivní PLAY scan bez FS side effects.
- `src/app/route_switch.c`, `src/app/cmt_bus_policy.h`, `docs/HARDWARE.md`: zachovat a výslovně **zdokumentovat mechanický S1**; jen opravy reálných chyb, žádný fiktivní HW switch.

Rozvržení úloh / priorit: Player je na CPU0, UI/aplikace na CPU1, storage worker vlastní FS. MCPWM ISR IRQ priority 3 by měla být přidělena při inicializaci capture na CPU1; **ověř skutečný core binding**, IRAM-safe přístup a kritické sekce. Metadata / OLED na nižší prioritě nesmějí ohrozit zachytávání časových značek ani pulzní RMT DMA. Neprovádět synchronní nákladné preflight skeny při spuštění, pokud by přerušily reakci na STOP/MOTOR/SD/switch.

Paměť: Zachovat interní DMA-capable RAM pro hardwarově používané buffery a ISR, PSRAM pro dlouhodobá metadata/indexy, jednorázové statické / startup alokace pro parser. Po změnách vyhodnotit map, heap minima, stack high water marks; nezavádět trvalý 64KB buffer jen pro zobrazení informací. Metadata snapshot pro UI by měl být řádově desítky až nižší stovky bajtů.

## 8. Povinné testy (host a HW)

### Unit / host testy (automaticky doplnit)

1. RX zobrazení: 100 -> 20 % okamžitý pokles; 20 -> 80 % pomalé zotavení; 9 % = CRITICAL; 0 % = BAD. Sticky overrun zachován i když se FIFO následně uvolní.
2. RECORD MCPWM/PCNT: shoda počtů, ztráta jedné i dvou hran, chybná/čekající ISR, 32bit wrap, dlouhé ticho, PAUSE/RESUME bez fiktivních hran, ochrana proti falešnému mismatch na hranici epochy, overflow počítadla PCNT, AUTO/MANUAL/MOTOR.
3. PLAY TX buffer: vysoká/nízká rezerva, rychlé vyprázdnění, správný alarm a reset stavu, long pulse s velkou časovou rezervou vs mnoho krátkých pulzů, nulový buffer v `UL HANDSHAKE` / mezi bloky bez falešného alarmu, rozdíl přečtených vs skutečně odvysílaných bajtů.
4. OLED renderer: pixely/bary/inverze položky, dirty-page aktualizace, I²C selhání a retry, změna displejové stránky a obnovování, no-SD/external/motor overlays s časováním a prioritami, KEYMAP oprava.
5. Browser: 0/1/7/8/1000/10000 položek, STOP CANCEL, vstup do chybové podsložky **nezničí předchozí seznam**, návrat do rodiče zachová výběr, cursor repeat/wrap, názvy až 255 B, PSRAM allocation count stabilní.
6. MZF/MZT/M12/MZQ/QDF/QD: známý HEADER TYPE/TITLE/SIZE/LOAD/EXEC, různý rekord, sidecar override/fallback, UL/TC/IC, fyzické změny rekordů, raw vs transformovaný header.
7. **Loader first payload**: TC 90B loader s validní signaturou a skutečnými vnořenými metadaty na `0x4C/0x4D`, IC vnořený header na `0x1A`, obyčejná 128B hlavička, neznámý custom loader, poškozený loader payload/checksum, chybějící druhý blok. UI nesmí zobrazovat `SIZE=90, LOAD/EXEC=D400` jako program.
8. WAV/LEP/L16 PLAY: synteticky vygenerovaný Sharp 1:1 a IC 1:4/TC, obě fyzické úrovně, dlouhé pauzy, správné převody vzorků/kvantizace, falešné leader vzory, multiple records, prázdná data; offline analýza pouze nad identickými intervaly jako PLAY a bez ovlivnění jejich sekvence.
9. TAP: platný ZX BASIC/CODE/ARRAY header s XOR, autostart line pro BASIC, CODE load bez fiktivního EXEC, data-only, poškozené délky/checksum, více bloků a přepínání názvu, čtyři rychlosti výstupu; nenapojovat Sharp parser.
10. AutoName RECORD: standard/IC/TC (včetně prvního payloadu), audio AUX Sinclair a CP/M, shodné metadata pro shodný vstup v PLAY a RECORD, neznámá pole `--`, název nahrávky vs aktuální detekovaný titul, žádný přepis existujícího souboru.
11. S1: INPUT GPIO21 LOW/EXT, 40ms debounce, změna během PLAY/REC/UL, bezpečné výstupy, zákaz nového interního transportu v EXTERNAL, návrat na INTERNAL bez automatického restartování předchozího PLAY.
12. Regressions: původních 20 host sad + build ESP-IDF target N16R8, bez nových lint/test regressions. Zaznamenat přesný commit/parametry, z test reportu neodvozovat HW akceptaci.

### Skutečný HW ESP32-S3 + MZ (pokud není dostupný, jasně označit jako NEPROVEDENO)

- Logický analyzátor GPIO4 READ a GPIO5 WRITE: 1:1, 1:2, 1:3, 1:4 a TC/IC/UL, krátké a dlouhé pulzy, sekvenční LOAD/SAVE.
- Vstupní hrany: generátor přesného signálu a současná extrémní zátěž SD (zápis), OLED, CPU, stop/motor a switch. Měřit rozdíl MCPWM vs PCNT a **časové chyby**, ne pouze konečný součet.
- PLAY buffer: SD latency / vyčerpání FIFO / RMT DMA, žádné výpadky během OLED scroll/metadata. Změřit PAUSE/RESUME uprostřed symbolu (retence RMT group clock dosud nedoložena).
- Mechanický S1: přepnutí na EXTERNAL, ověřit bezpečnou izolaci přepínačem v celém zapojení, nedělat „relé“ předpoklady; monitor/motor/sense všech stavů.
- OLED: reálná čitelnost na SSD1306 128×64, metadata programů, grafické ikony, buffer bez maskovaného kritického poklesu. 30–60min běh s nízkým stack/heap jitterem.

## 9. Definice dokončení a výstupy Codexu

1. Funkční UI s opravou overlay, marquee, chybového návratu z adresáře, s přehledným PLAY/REC a správně definovaným RX/TX bufferem.
2. RECORD s ověřitelnou indikací ztrát hran (PCNT, pokud technicky konzistentní; jinak transparentně popsat omezení bez falešné garance), zachovaným MCPWM časováním a funkčním dokončením WAV/LEP/L16/MZF.
3. Metadata skutečného programu v OLED při PLAY a AutoName RECORD: **název, typ, loader, délka, LOAD, EXEC** tam, kde údaje existují; rozpoznávání i **WAV/LEP/L16/TAP**; typově správné `UNKNOWN` pro chybějící pole.
4. **Explicitně zvládnutá metadata vložená do prvního payloadu loaderu**, zejména TC a IC; žádné mylné „program je zaváděcí loader“.
5. Beze změny pinoutu a fyzického principu S1 mechanického přepínání; žádné relé, mux driver nebo nové GPIO.
6. Spuštěné, doložené testy a build (je-li prostředí k dispozici), výsledná tabulka co je implementované/testované, co zůstává na HW. Uveď použitý HEAD/SHA a změřený RAM/flash footprint.
7. Nová/aktualizovaná dokumentace (`README`, `docs/PARITY_MATRIX.md`, `docs/TEST_REPORT.md`, `docs/HARDWARE.md`) a jednoznačná tlačítková mapa.

**Nevytvářej jen návrh vzhledu. Implementuj změny se zachováním transportní spolehlivosti. Nepotvrzuj „bezeztrátový capture“, „bezvadný UI“ nebo „hotovo na HW“ pouze z host testů.**
