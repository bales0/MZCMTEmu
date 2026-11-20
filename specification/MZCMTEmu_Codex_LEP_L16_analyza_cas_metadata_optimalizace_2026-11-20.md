# Zadání pro Codex: MZCMTEmu – rychlá analýza LEP/L16, přesný čas a metadata před PLAY

**Datum zadání:** 20. 11. 2026  
**Repozitář:** https://github.com/bales0/MZCMTEmu  
**Výchozí kontrolovaný commit:** `3664dab50e114083c3b593bdcba2eab0fe7dc474` (`ui improvement, time analysis before play`, 20. 11. 2026)  
**Charakter:** návrh a implementace *výhradně po postupném výslovném schválení uživatelem*.  
**Důležitá instrukce:** Před zahájením ověř HEAD; pokud se liší, znovu posuď dopady a předlož rozdíly. Nesmíš implicitně aplikovat zadání na jiný stav zdrojů.

## 0. ZÁVAZNÝ REŽIM SCHVALOVÁNÍ – NEPROVÁDĚT ZMĚNY BEZ SOUHLASU

1. **Nejprve jen read-only audit.** Seznam konkrétních souborů, místo změny, problém, způsob řešení, dopad na časování, CPU, ISR, RAM/PSRAM, SD a UI.
2. **Před každou jednotlivou změnou** (i opravou testu, úpravou konfiguračního souboru, dokumentace, nové implementační jednotky či úpravou build skriptu) předlož uživateli konkrétní návrh/diff nebo přesný popis změny a vyžádej **výslovný souhlas**. Souhlas s jedním krokem **není** souhlas s dalšími kroky.
3. Žádné automatické commity, branche, push, flash zařízení ani změny HW. Sestavení a testy, které vytvářejí nebo mění soubory v projektu, rovněž předem schval. Rozlišuj příkazy čistě čtecí a příkazy s vedlejšími efekty.
4. Při zamítnutí nebo nejasném souhlasu nic neměň; předlož jiné řešení. Každou fázi uzavři samostatným přehledem a výsledky.
5. Toto omezení platí pro **práci Codexu na repozitáři**. Není požadavkem přidávat nové potvrzovací dialogy do přehrávače či zpomalovat PLAY.

## 1. CÍL

Zkrátit dobu čekání na obrazovce `ANALYZING LEP / L16 DURATION` a při **téže analýze** pokud možno zjistit a už před zahájením PLAY zobrazit **metadata skutečného programu** (jméno, typ, velikost, LOAD/EXEC, rozpoznaný loader/profil, stav ověření). Prioritou je *bezeztrátové, přesné* přehrávání a zachování dosavadních formátů a významu jejich bitů/pulzů. V žádném případě metadata nesmějí být odvozena jen z názvu souboru nebo ze zaváděcího programu namísto skutečného payloadu.

Přesný čas musí nadále odpovídat součtu všech LEP/L16 intervalů včetně dosavadního 300 ms preroll a 500 ms tail. Při přeskočení analýzy musí zůstat čas neznámý, PLAY musí spustit plný původní waveform, STOP analýzu přerušit bezpečně bez výstupních pulzů.

## 2. OVĚŘENÝ VÝCHOZÍ STAV – NEROZBÍT

- `src/audio/edge_stream.c`: `scan_duration()` čte LEP/L16 soubor po **1024B blocích**, sčítá jednotky všech bajtů, vrací `seek(0)`; `edge_stream_open()` blokuje do konce skenu. Je zde callback `EDGE_SCAN_CONTINUE/SKIP/CANCEL`. Nula reprezentuje 127 jednotek, `-128` 128 jednotek, ostatní hodnoty absolutní hodnotu; jednotka LEP = 50 µs, L16 = 16 µs.
- `src/main.c`: `scan_edge_analysis()` ukazuje procenta, PLAY = SKIP, STOP = CANCEL, kontroluje odebrání SD / mechanický S1, 100ms UI redraw a periodické `vTaskDelay(1)`; žádný automatický timeout.
- `src/audio/player.c`: při PLAY LEP/L16 volá `edge_stream_open()`; až během generování pulzů přes `edge_stream_next_pulse()` posílá intervaly do `tape_metadata_feed()`; UI má publikaci metadat časově svázanou s **fyzicky dokončeným** výstupem přes frontu a `metadata_confirm()`. Toto časové rozlišení je důležité při prefill/pause/STOP a nesmí zmizet.
- `src/formats/tape_metadata.c` využívá stávající AutoName rozpoznávání přes `src/record/record_autoname.c` a `src/formats/mz_tape_decoder.c`. Pro IC umí kanonickou hlavičku rekonstruovat; pro TC je nutné nejdřív ověřit **první loader payload**. Metadata mají provenienci/validitu v `program_metadata_t`.
- `src/storage/fs.c`: existuje **1024B cache na file handle**, operace SD provádí storage task; samotné zvětšení lokálního `block[1024]` na `block[16384]` nemusí snížit skutečný počet HW read a může přetéct stack. Před větší blokovou optimalizací změř celý řetězec a navrhni bezpečný buffer/čtecí API.
- MZF již používá rychlejší výpočet přes tabulku délek bajtů; QD/MZT mají vlastní připravené profily/cache. Tyto cesty **neměnit**, pokud nejsou prokazatelně dotčeny.
- Dokumentace posledního stavu: `docs/validation/edge-time-summary.json`, `docs/validation/edge-time-host.txt`, `docs/TEST_REPORT.md`: host GCC14 ASan/UBSan + target build PASS; **HW akceptace dosud nedoložena**.

## 3. FÁZE A – READ-ONLY MĚŘENÍ A ANALÝZA ÚZKÝCH MÍST (SAMOSTATNÉ SCHVÁLENÍ PRO ZMĚNY)

Před implementací navrhni metodiku časových měření na reálném ESP32-S3:

- Odděl **čas přístupu na SD / počet fyzických `read()`**, kopie z 1024B cache, CPU smyčku součtu intervalů, callback a OLED/I²C redraw, čekání na storage task a přesuny mezi jádry.
- Změř scénáře: malý LEP/L16, střední, velký (řádově desítky MB), rozsáhlé ticho, mnoho hran, pomalá/fragmentovaná SD; cold/warm cache, první a druhé přehrání.
- Připrav přesná instrumentační místa, běhové náklady a způsob vypnutí; žádný log per edge/bajt, žádné logování z ISR. Nezhoršovat capture ani RMT.
- Než upravíš čtecí API, porovnej alternativy (stávající 1024B vs 4/8/16KiB, burst read, vlastní reader/prefetch, efektivnější FS cesta, případně cache). Vyber až podle měření.
- Není-li reálný HW dostupný, odděl host benchmark od odhadu a **neprezentuj rychlostní přínos jako naměřený na HW**.

**Výstup fáze:** identifikace skutečného bottlenecku, tabulka původních metrik a návrh jednoho minimálního zásahu. Vyžádej souhlas.

## 4. FÁZE B – JEDINÝ ANALYTICKÝ PRŮCHOD: DÉLKA + METADATA

Navrhni společný analyzátor při `edge_stream_open()` (případně samostatné kontextové API), který v jediné sekvenční iteraci:

1. Přečte LEP/L16 po efektivně velkých blocích.
2. **Přesně** sečte všechna trvání bez zaokrouhlování jednotlivých intervalů a bez 32bitového přetečení; zachová hodnoty 0 a -128 a preroll/tail.
3. Jednotlivé události převede do **stejné fyzické posloupnosti úrovní a časů**, jakou používá PLAY. Pozor na `0`: představuje pokračování dosavadní úrovně, nikoli fyzický přechod; dlouhý interval rozdělený do segmentů se musí pro dekodér **sloučit**, nikoli emitovat jako umělá hrana. Nesmí dojít k chybě polarity, k umělé hraně mezi chunk boundaries, ani k započtení syntetického preroll/tail do dekódování.
4. Pasivně přivede normalizované intervaly stávajícímu `tape_metadata`/AutoName dekodéru (bez vlastní konkurenční implementace). Podporovat Sharp normal a turbo, IC, TC, pokud možno Sinclair/CP/M jako v AutoName; respektovat jistotu a limity dekodéru.
5. Při rozpoznání korektních metadat zpřístupní snapshot UI **už během skenu**, nezávisle na finálním čase. Je-li nadále potřeba pokračovat kvůli přesné délce, OLED může zobrazit metadata současně s procentem skenu.
6. Na konci vrátí délku, validitu a index nalezených programů (viz další fáze; pro první iteraci stačí aktuální program + počet) a přetočí zdroj bezpečně na začátek.

**Důležité pro IC/TC a jiné vícestupňové loadery:**

- První zaznamenaná hlavička může patřit samotnému loaderu, ne cílové hře.
- IC: rekonstruovat programovou hlavičku podle skutečného IC layoutu (viz `mz_loader_profile_recognize_ic`), neuvádět adresy loaderu jako adresy programu.
- TC: nejdříve validovat následující loader payload a teprve pak použít původní programové údaje (`mz_loader_profile_detect_tc`, `mz_loader_profile_normalize_tc`); při neplatném payloadu nevystavovat domnělé EXEC/LOAD.
- Další atypické loadery: explicitně rozlišit `LOADER_ONLY`, `HEADER_VALID`, `PAYLOAD_VALID`, `UNKNOWN`, `INVALID` a provenienci; nic nefabrikovat. Zachovat případné více programů a nepřepsat validní první titul jiným nesouvisejícím leaderem bez pravidel pro změnu programu.
- Pozor na název nahrávky (soubor na SD) vs titul skutečného programu vs název loaderu.

**Architektura stavu:** Analýza při preflight nesmí závodit s aktivním PLAY/RECORD ani sdílet živý globální AutoName dekodér, pokud by jej druhý kontext mohl používat. Preferovat oddělený kontext, případně dokončit analýzu, vytvořit *immutable snapshot*, resetovat decoder a až potom spustit PLAY. Nespoléhat, že společná globální instance je bezpečná jen díky současnému pořadí volání. Žádné dekódování ani SD I/O v ISR.

**Zachování synchronizace UI:** Během preflight je možné zobrazit předběžně potvrzená metadata z analyzátoru. Po skutečném startu PLAY rozlišit `PREVIEW METADATA` a metadata průběžného programu; stávající mechanika publikace vůči skutečně odehranému signálu se nesmí rozbít. Prefetch nesmí přeskočit/posunout UI na pozdější program, který ještě fyzicky nezazněl.

## 5. FÁZE C – BEZPEČNÁ CACHE DÉLKY A METADAT

Navrhni nejprve **RAM session cache** s jednoznačným invalidátorem, bez nové povinné SD databáze. Záznam minimálně:

- identita plné cesty + délka souboru + mtime, přesné jednotky LEP/L16, přesná délka, stav analýzy (kompletní / SKIP / CANCEL / ERROR), verze algoritmu;
- metadata se stavem validace a vazbou na pořadí/timestamp programu; případně počet a polohy programů;
- volitelně doplňující fingerprint (vybrané datové bloky, CRC), pokud je nutný a přiměřený.

Invalidovat při odebrání/výměně SD, změně zdroje, změně délky/mtime, zapisování do záznamu, RESET/MOUNT a verzování formátu. Výsledek ze SKIP/CANCEL/ERROR se **nesmí vydávat za kompletní**. Stav cache musí být diagnostikovatelný (hit/miss/invalid), ale OLED nemusí obsahovat ladicí detaily.

Persistentní cache na SD **není automaticky schválena**; teprve po měření jejího přínosu samostatně navrhnout včetně robustního ověřování identity a dopadu na životnost SD. Velikost+mtime může přehlédnout změnu obsahu, nenahrazuje plnou integritu.

## 6. FÁZE D – UI A OVLÁDÁNÍ BĚHEM ANALÝZY

- Zachovat 128×64 SSD1306 a dosavadní 6×8 font, bezpečné délky řetězců a neblokující aktualizace displeje.
- Na `ANALYZING LEP/L16` zobrazit průběh skenu a dostupné ověřené údaje: programový titul, typ, loader/rychlost, SIZE, LOAD/EXEC; po rozpoznání první validní hlavičky **nemuset čekat na 100 %**.
- Pokud se informace nevejdou na 8 řádků, sestav kompaktní pohled bez ztráty výstrahy a ovládání. Žádný dlouhý scrolling nebo I²C redraw per interval.
- Při SKIP přes PLAY: ihned spustit celý záznam od začátku; čas neznámý, případná rozpoznaná metadata pouze s pravdivým stavem a proveniencí. STOP = úplné zrušení, bez RMT startu; odstranit jakékoli chyby/overlay z přerušeného skenu.
- Pokud metadata nejsou spolehlivá, zobrazit `--`/`UNKNOWN`; neukazovat loaderovou adresu jako EXEC skutečného programu. Při více programech v jednom souboru nezaměnit `selected/preview` s `currently playing`.
- Zachovat prioritu UI (PLAY/RECORD, STOP, chybové overlay) a mechanické S1 chování; nikdy nevytvářet softwarové relé/mux.

## 7. FÁZE E – DALŠÍ FUNKCE AŽ PO SCHVÁLENÍ ZÁKLADU

**Volitelné a samostatně schvalované:**

1. Pasivní seznam nalezených programů v LEP/L16 (pořadí, zdrojový offset, čas začátku, ověřený název/typ/loader/adresy). Při limitu indexu neztratit schopnost dopočítat celkový čas ani přehrát soubor.
2. Zobrazení `CONTENTS` v OLED, pouze je-li spolehlivé a uživatelsky užitečné. Výběr a případné skákání na konkrétní program **neimplementovat automaticky**: vyžaduje definovat počáteční signál/leader/MOTOR a prokázat časovou bezpečnost.
3. Přesný benchmark SD, CPU/stack high-water, počet I/O operací, zisk RAM/PSRAM; volitelné vypnutelné statistikové počítadlo bez per-edge trace.
4. Prověřit, zda stejný analyzátor lze bezpečně použít i pro WAV – **není** součástí povinné první implementace, nevynucovat zbytečné překreslení WAV readeru.

## 8. TECHNICKÉ INVARIANTY – NEMĚNIT

- ESP32-S3 WROOM N16R8; READ GPIO4 / RMT TX DMA; WRITE GPIO5 / MCPWM capture pro RECORD; MOTOR GPIO6; SENSE GPIO7; S1 GPIO21 (detekce polohy mechanického přepínače), SD CS13/SCK14/MOSI15/MISO16, OLED SDA17/SCL18. **Žádné relé ani softwarově řízený přepínač.**
- S1 má fyzické INTERNAL/EXTERNAL, stav se pouze čte. Respektovat aktuální pravidla: EXTERNAL blokuje PLAY, ale nikoliv RECORD/editor; nepřepisovat logiku S1.
- Nepřidávat SD práci, alokace, printf, OLED ani dekodér do ISR. Nerozbít FreeRTOS priority, storage task CPU0, přehrávací task, kritické sekce a bezpečnost interrupt path.
- Žádná změna LEP/L16 binárního formátu, kvantování a polarit. Zachovat původní význam dlouhých intervalů a nulových bajtů.
- Zachovat RMT TX kontinuitu, buffer reserve, úvod/tail, pause/resume/motor/STOP/EOF, underrun, UL a další formáty (MZF, MZT, M12, QD/MZQ/QDF, TAP, WAV).
- Žádná nová fragmen­tující alokace per item/per interval; velký dočasný buffer nedávat automaticky na stack (u projektu již byla podezření na přetečení app stacku při synchronním FS volání). Ověřit high-water a reentranci.
- `program_metadata_t` má validitu a provenienci; metadata jsou nedůvěryhodný obsah souboru, striktně hlídat délky polí, indexy a přetečení.

## 9. TESTOVACÍ MATICE A AKCEPTAČNÍ KRITÉRIA

Všechny testy nejprve navrhnout; **spouštět až po schválení**, pokud generují/mažou soubory.

### Přesný čas a waveform

- Všechny možné vstupní hodnoty bajtu (-128..127), nula, změny polarity, spojování sousedních intervalů stejné úrovně, dělení velmi dlouhé pauzy, první/poslední interval; nulový/vadný soubor a maximální délky (`uint64_t`).
- Původní vs nový součet v µs **bitově/exaktně shodný** na stejných datech, včetně 300ms preroll/500ms tail. Po SKIP/CANCEL bez chybné obnovy FILE pozice.
- Hash/porovnání výstupních sekvencí na RMT: PLAY waveform identický před/po optimalizaci. STOP analýzy nepustí nic na READ.
- Zrušení v 0 %, 50 %, 90 %, 100 %; vyjmutí SD, chyba čtení, S1 INTERNAL→EXTERNAL, opakované PLAY, pauza/MOTOR, EOF, nový soubor.

### Metadata

- Standard Sharp, všechny supported NORMAL rychlosti, IC/TC (minimálně 1:1..1:4 tam, kde podporováno), loaderová hlavička, validační první payload, správná canonical header, chybný checksum/krátký payload, více souborů za sebou; fyzické znaménkové LEP/L16 intervaly a chunk boundaries.
- Vykreslení `--` pro neznámé LOAD/EXEC, nikoliv neplatných ad hoc hodnot. Oddělit `HEADER_VALID`, `PAYLOAD_VALID`, `LOADER_ONLY`, `INVALID` a provenienci. Meta-preflight nesmí přepsat aktuálně odehraný program.
- Současná AutoName pravidla pro RECORD (WAV/LEP/L16, Sharp/IC/TC/Sinclair/CPM) musí zůstat funkční a bez změny ISR.

### Výkonnost

- Porovnat reálné hodnoty v ms pro cold/warm SD, soubor 1 MiB / 10 MiB / typický uživatelský případ, 1024B cache vs schválená optimalizace, počet diskových read, počet bytes, doba CPU skenu a refresh UI.
- Uvést **měřený přínos** (nikoli předem slíbené procento), CPU/stack peak, RAM/PSRAM, počty alokací, zátěž jiných tasků. Cache hit musí přeskočit scan bezpečně.
- Host GCC ASan/UBSan + `pio run -e MZCMTEmu -j 1`; jasně oddělit host-simulation PASS, target-build PASS a fyzicky HW-TESTED. Testy z konverzace na skutečné desce nebyly v tomto zadání provedeny.

### Povinný výstup každé schválené etapy

- Seznam změněných souborů a přesné diffy, popis chování před/po, výsledky testů a neověřená rizika.
- Zapisovat diagnostiku/benchmark report do dokumentace pouze se samostatným schválením.
- Žádné automatické změny dalších formátů ani refaktoring mimo schválený rozsah.

## 10. POŽADOVANÁ PRVNÍ ODPOVĚĎ CODEXU

**Zatím neimplementuj nic.** V první odpovědi:

1. Ověř aktuální commit a vypiš přesné relevantní funkce, závislosti a skutečný bottleneck, který lze zatím jen odhadovat.
2. Předlož plán ve 3–5 malých, samostatně schvalovaných krocích, s explicitním popisem, který krok mění jaké soubory a jaká jsou rizika.
3. Navrhni bezpečnou architekturu společného skenu **duration + metadata** s přesnou reprezentací nulových bajtů a spojováním intervalů, s IC/TC prvním payloadem a zachováním časové publikace UI.
4. Uveď co lze skutečně ověřit bez HW a co vyžaduje měření na ESP32-S3.
5. **Zeptej se na souhlas pouze s prvním konkrétním zásahem.** Neprováděj další práci měnící projekt bez dalšího souhlasu.
