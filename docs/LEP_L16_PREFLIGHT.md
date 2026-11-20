# Analýza LEP a L16 před přehráváním

Ve verzi r41 se délka a metadata LEP/L16 získávají v jednom sekvenčním průchodu. Opakovaný PLAY může využít RAM session cache. První sken navíc dekóduje protokol a nemusí být rychlejší než původní výpočet samotného času. Přesnost signálu a oddělení náhledu od fyzicky přehrávaných metadat ověřují host testy. Rychlost SD a RTOS high-water na ESP32-S3 zůstávají k fyzickému ověření.

Základ je přesně `3664dab50e114083c3b593bdcba2eab0fe7dc474`. Rozsah byl po auditu schválen jako celek. Bez commitu, nové větve, push a flash. Uživatelské změny zadání byly ponechány beze změny.

## Měření a zvolená optimalizace

Host benchmark storage vrstvy kompiluje produkční `storage/fs.c` s modelovanou frontou/taskem a skutečnými host VFS čteními. Počet požadavků je měřený, nepředstavuje počet fyzických transakcí SD. Větší buffer volajícího 4/8/16 KiB nesnížil počet 1024B doplnění cache. Čtecí API, cache 1024 B, pool osmi deskriptorů, storage task a jeho priority proto zůstaly zachovány.

| Data | Blok volajícího B | VFS read | RPC | Native read ms | Kopie ms |
|---|---:|---:|---:|---:|---:|
| 1 MiB | 1024 | 1024 | 1024 | 171.74 | 0.53 |
| 1 MiB | 4096 | 1024 | 1024 | 155.21 | 0.49 |
| 1 MiB | 8192 | 1024 | 1024 | 111.32 | 0.44 |
| 1 MiB | 16384 | 1024 | 1024 | 102.94 | 0.35 |
| 10 MiB | 1024 | 10240 | 10240 | 1596.26 | 5.32 |
| 10 MiB | 4096 | 10240 | 10240 | 1289.33 | 4.29 |
| 10 MiB | 8192 | 10240 | 10240 | 995.83 | 2.86 |
| 10 MiB | 16384 | 10240 | 10240 | 1530.20 | 4.38 |

Srovnání původního readeru ze základního commitu s novým skenem používá shodné generované soubory. GCC O2 bez sanitizeru, medián tří opakování, WSL a host filesystem. Pořadí je baseline → nový sken → RAM cache; OS cache se nevyprazdňuje a zátěž hosta není izolovaná. Nejde o cold/warm benchmark fyzické SD ani důkaz stejného přínosu na MCU.

| Data | Původní délka ms | Nová délka a metadata ms | Cache hit ms |
|---|---:|---:|---:|
| 1 MiB ticho | 207.54 | 157.24 | 2.33 |
| 10 MiB ticho | 1816.62 | 1654.75 | 2.09 |
| 10 MiB mnoho hran | 1275.88 | 1369.89 | 1.65 |

Host model ukazuje významnou cenu VFS požadavků; skutečný bottleneck ESP32-S3 zatím není určen. U mnoha hran stojí dekódování další CPU čas. Zvolený minimální zásah do I/O je vyhnout se opakovanému plnému skenu pomocí RAM cache, nikoli naslepo zvětšit SD blok nebo zavést nový prefetch reader. Nový první sken počítá fingerprint vzorků v témže průchodu, bez dodatečného čtení hlavy/středu/konce. Tři malé kontrolní čtení proběhnou jen při ověřování existujícího kandidáta cache.

Reprodukce host benchmarku: `python3 tests/host/benchmark_edge_preflight.py`. Test používá izolovanou složku `.tmp`, nemění zdroje baseline a nepřistupuje k zařízení ani fyzické SD. [Naměřená data](validation/edge-preflight-comparison-host.txt), [FS metriky](validation/edge-preflight-fs-final-host.txt).

## Přesný společný průchod

`edge_stream_open()` má výhradní foreground vlastnictví; rekurzivní či souběžný open selže před změnou otevřeného zdroje. Pracovní blok 1024 B je statický, nikoli na app stacku. Nevzniká nová alokace per soubor nebo interval.

Délka je přesný uint64 součet absolutních hodnot bajtů: nula = 127 jednotek, -128 = 128, LEP = 50 µs/jednotku, L16 = 16 µs/jednotku. Připočítává se 300 ms preroll a 500 ms tail. Do dekodéru tyto syntetické úseky nevstupují.

Metadata dostávají fyzické úrovně: kladný bajt HIGH, záporný LOW, nula pokračuje v předchozí úrovni. Run se drží přes hranice bloků; sousední intervaly stejné úrovně se spojí. Dlouhé běhy zachovají plnou délku, ale dekodér je podle původních pravidel vyhodnotí jako přerušení signálu. Nevytvářejí se umělé hrany.

## Oddělené dekodéry a provenience

Existující algoritmy `mz_tape_decoder`, `record_autoname` a `tape_metadata` byly beze změny přesunuty do společných `_impl.inc`. Původní `.c` zůstávají kompatibilními vstupními jednotkami pro živý kontext. Tři `preflight_*.c` kompilují tentýž zdroj do druhé instance s vlastním soukromým stavem a jmény. Nejde o druhý algoritmus; není zde přepínání globálního ukazatele, TLS ani půjčování živého AutoName stavu. Shoda normalizovaných zdrojů se základním commitem je ověřena SHA-256 v souhrnu. Cenou je druhá statická instance a další flash kód.

IC rekonstruuje kanonickou programovou hlavičku původním rozpoznávačem. TC čeká na validovaný první loader payload; krátký nebo vadný loader nevystaví cílové LOAD/EXEC. Normal, IC a TC používají původní validitu a provenienci. Sinclair/CP-M využívají stejný AUX dekodér včetně detekovaného profilu a checksumů. Neznámé adresy se zobrazují jako `--`. Rozpoznávání má limity původních dekodérů; neprovádí se odhad runtime adres neznámých či samorozbalovacích loaderů.

HEADER_VALID znamená ověřenou hlavičku, PAYLOAD_VALID checksum deklarovaného bloku, nikoli prokázané spuštění programu. LOADER_ONLY, UNKNOWN a INVALID zůstávají rozlišené. První rozpoznaný program se aktualizuje do ověření payloadu; potom se jeho náhled zmrazí. Pozdější jiný nebo poškozený loader jej nepřepíše ani nezneplatní.

Počet `PROGRAMS >= N` je dolní mez z rozpoznaných logických programů původního dekodéru, nikoli úplný index. Stejné opakované hlavičky mohou být deduplikovány. `discovered_us` je okamžik pozorování při skenu, s rozlišením bloku; není to čas/offset pro bezpečný seek. CONTENTS, výběr programu a skákání se neimplementovaly.

## RAM cache a invalidace

Cache má jednu položku a neprovádí žádný zápis databáze na SD. Klíč zahrnuje přesnou cestu (firmware používá úplné `/sd/...` cesty), délku, mtime/ctime, dostupné device/inode, jednotku 16/50 µs, generaci média/zápisů, verzi algoritmu 2 a fingerprint hlavy/středu/konce. Výsledkem je kopie délky a prvního metadatového snapshotu bez živých ukazatelů.

Generace se mění při mount/reinit/removal a při zápisu, fyzickém flush zápisu, zavření writeru, remove/rename i startu RECORD. Změna identity/fingerprintu je INVALID. Jen dokončený stabilní sken se uloží. SKIP, CANCEL a chyba čtení se za kompletní výsledek nevydávají; výsledek po změně zdroje během skenu se odmítne. Diagnostika rozlišuje HIT/MISS/INVALID.

Vzorkovaný fingerprint není kontrola integrity celého souboru. Neohlášená externí změna mimo vzorky při shodných metadatech nemusí být detekována. Správnost session cache předpokládá výhradní FS vlastnictví firmware a invalidaci při změně/výměně SD. Interní zápisy jsou pokryté generací.

## Obrazovka a časová publikace

ANALYZING ukazuje procenta a dostupný první titul, typ, profil, SIZE, LOAD/EXEC a stav ověření ještě během skenu. Model má osm řádků 6×8, omezené řetězce a jedinou spodní ikonovou nápovědu. Překreslení zůstává omezené na 100 ms; běží běžný yield a žádný automatický timeout.

PLAY přeskočí analýzu a přehraje celý původní soubor od začátku. Čas zůstane neznámý, takže se použijí procenta. Rozpoznaná metadata mohou být použita jen jako označený náhled. STOP zruší celý start bez nového RMT TX; zůstává kontrola odebrání SD a mechanického S1. Obsluha probíhá mezi bloky a nepřerušuje právě běžící VFS read.

Po startu vrací `player_preflight_metadata()` samostatný immutable náhled označený PREVIEW METADATA. `player_metadata()` a fronta `metadata_collect()`/`metadata_confirm()` zůstávají pro živou časovou osu. Jakmile dorazí fyzicky potvrzená metadata, náhled ustoupí a nevrací se při pozdějším prefetchi. Test dvou programů ověřuje FIRST a následně SECOND až podle dokončeného RMT výstupu. WAV nemá nový preflight; jeho dosavadní pasivní metadata zůstala zachována.

[Náhledy produkčního rendereru se syntetickými testovacími hodnotami](validation/edge-preflight-preview.png).

## Vypnutelná diagnostika

Ve výchozím sestavení jsou `CMT_STORAGE_DIAGNOSTICS=0` a `CMT_EDGE_DIAGNOSTICS=0`. Neprobíhají profilovací volání hodin per byte/edge, printf ani SD práce v ISR. Při diagnostickém sestavení je nutné příslušné `-D...=1` připojit k existujícím build flags, nikoli je přepsat. Normální firmware nebyl kvůli měření přepnutý do jiné konfigurace.

| Metrika | Zdroj a význam |
|---|---|
| read_calls/read_bytes | `cmt_storage_read_metrics()`; skutečné VFS read operace a bajty na storage tasku |
| native_us | Čas přímo kolem read, bez aktualizace účetních čítačů |
| submit_us | Celý synchronní READ požadavek, včetně native_us; rozdíl zahrnuje frontu, scheduling, přechody mezi tasky a diagnostickou režii |
| copy_bytes/copy_us | Kopírování z 1024B cache, čas bez aktualizace čítače |
| read_us | Blokové čtení analytického průchodu; cache validační čtení jsou vidět ve FS metrikách |
| decode_us | Součet, fingerprint, normalizace a pasivní dekódování v CPU části bloku |
| callback_us | Celkový čas ovládacího callbacku |
| ui_us/yield_us | Samostatně ohraničené ui_render a vTaskDelay; obsaženy také v callback_us |

Snapshot FS čítačů je chráněný; reset a porovnání dělat v klidové fázi. Čítače jsou společné pro všechny deskriptory, takže souběžná cizí aktivita se může do souhrnu promítnout. Diagnostické hodiny a krátké kritické sekce mají režii, proto jsou standardně vypnuté. Cache status se vypisuje jednou před startem TX, bez per-edge trace. [Ověření kompilace obou diagnostických jednotek cílovým překladačem](validation/edge-preflight-diagnostics-build.txt) není HW měřením ani samostatným link testem celého diagnostického firmwaru.

## Výsledky ověření

Celá host sada: 30 sad GCC14 s ASan/UBSan PASS. Po závěrečných úpravách byly přímo dotčené sady znovu spuštěny. Ověřeno všech 256 bajtů, přesný uint64 čas, nula a -128, SKIP/CANCEL v 0/50/90/100 %, rewind, chyba čtení, invalidace, shodný RMT symbol hash po analýze i SKIP, pause/STOP/EOF a oddělená publikace více programů.

Skutečný Sharp waveform převedený do LEP i L16 prošel pro NORMAL a IC 1:1–1:4 a TC 1:1–1:3. Testy zahrnují kanonické adresy, vadný checksum, krátký TC loader, nesouvisející pozdější chybný loader a živý dekodér pozastavený mezi hlavičkou a daty. Sinclair a CP-M mají souborové testy čtyř rychlostí a odmítnutí vadných checksumů. Původní Sharp/QD/MZT/UL/TAP/WAV/RECORD a ISR/transport sady prošly.

PlatformIO `pio run -e MZCMTEmu -j 1`: PASS. RAM 171868 B, proti základu +3456 B; program 487003 B, +11128 B. Nová analýza přidává statický interní stav, žádné nové explicitní heap/PSRAM alokace. Celková aktivita allocatoru na HW nebyla měřená. Zůstává původní varování o nepoužité `ui_copy_scroll_text`.

Jednotlivé rámce finálního ELF, nikoli součet maximální hloubky ani RTOS high-water:

| Funkce | Rámec |
|---|---:|
| `app_task` | 1216 B |
| `player_start` | 1088 B |
| `edge_stream_open` | 272 B |
| `scan_collect` | 128 B |
| `scan_edge_analysis` | 224 B |
| `cmt_fread` | 112 B |
| `submit` | 48 B |
| `ui_build_edge_analysis` | 64 B |

## Fáze a změněné soubory

| Fáze | Změny a výsledek |
|---|---|
| A | `storage/fs.c/.h`, storage test a benchmark: vypnutelná měření, epoch, host porovnání 1024/4K/8K/16K. Bez změny cache či čtecího API. HW bottleneck neurčen. |
| B | Sdílené `_impl.inc`, původní kompatibilní `.c`, preflight namespace a tři instance: stejné algoritmy, oddělený stav. Izolace a metadata PASS. |
| C | `audio/edge_stream.c/.h`, FS invalidace a SD lifecycle v main: jeden průchod a jedna RAM cache. Přesný čas, identita a neúplné výsledky PASS. |
| D | `audio/player.c/.h`, `ui/view.c/.h`, main: metadata při skenu, samostatný označený preview a původní živá timeline. Pixelové a RMT testy PASS. |
| E | `tests/host`, CMake a dokumentace: 30 sad plus finální cílené regrese, default target build a diagnostické compile kontroly PASS. |

[Přesný textový diff kódu a dokumentace](validation/edge-preflight-source.patch), [úplný seznam a hashe změněných zdrojů](validation/edge-preflight-files.json), [souhrn včetně SHA-256 firmware](validation/edge-preflight-summary.json), [celá host regrese](validation/edge-preflight-all-host.txt), [konečná akceptace](validation/edge-preflight-acceptance-host.txt), [vadné vstupy](validation/edge-preflight-malformed-host.txt), [build](validation/edge-preflight-build.txt), [zásobníkové rámce](validation/edge-preflight-stack.txt).

## Zbývající fyzické ověření

Bez flash nelze na desce změřit nový firmware. HW akceptace, skutečné počty SD transakcí, latence core/task přechodů, heap peak a stack high-water zůstávají neověřené. Na ESP32-S3 je třeba zaznamenat malý/1 MiB/10 MiB/typický soubor, ticho i mnoho hran, první a druhý PLAY, cold/warm kartu a pomalou či fragmentovanou SD. Porovnat celý wall time, FS a scan metriky, OLED/yield a high-water app/storage tasků. VFS read nelze automaticky vydávat za fyzický sektorový přenos; ten vyžaduje měření nižší SD vrstvy nebo SPI trace.

Zároveň fyzicky ověřit STOP, PLAY SKIP, vyjmutí SD a S1 během analýzy, motorovou a uživatelskou pauzu a READ/SENSE výstupy. Žádné piny, polarity, ISR, RTOS priority, mechanická CMT trasa ani binární formáty se touto změnou nemění. Persistentní cache, CONTENTS/seek a automatický WAV preflight nebyly přidány.
