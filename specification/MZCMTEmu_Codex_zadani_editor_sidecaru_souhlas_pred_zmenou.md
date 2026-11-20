# MZCMTEmu — zadání pro Codex: editor sidecarů (MFI, M2I, MTI, QDI)

**Datum:** 20. 11. 2026  
**Repozitář:** https://github.com/bales0/MZCMTEmu  
**Výchozí ověřený HEAD:** `1dfdeffb2cc4ea506b706b376f7dfe4a82fb542e` (14. 11. 2026, „backup, UI finetune“). Než začneš, zkontroluj aktuální HEAD a rozdíly oproti tomuto bodu.  
**Charakter úkolu:** Nejprve **analýza a návrh bez zápisu**. Implementace až po explicitním schválení každé jednotlivé změny.

## 0. ABSOLUTNÍ PRAVIDLO: PŘED KAŽDOU ZMĚNOU VYŽÁDAT SOUHLAS

**Nesmíš bez výslovného předchozího souhlasu uživatele změnit jediný projektový soubor.** Platí pro zdrojové kódy, konfiguraci, build skripty, testy, dokumentaci, generované soubory v repozitáři, přesuny, mazání, formátování, přejmenování i commity. Výjimka neexistuje pro „malou“, „samozřejmou“ nebo „nutnou“ opravu. Uživatelem schválený návrh není automatické schválení celé implementace.

Před **každou jednotlivou zamýšlenou změnou** předlož:

1. Přesnou cestu souboru/souborů a jejich současný stav.
2. Co konkrétně se změní (ideálně diff nebo pseudodiff), proč a jaké jsou alternativy.
3. Dopad na kompatibilitu, SD data, UI, RAM/PSRAM, ISR/FreeRTOS, HW a testy.
4. Způsob ověření a návrat zpět.
5. Explicitní otázku **„Souhlasíš s touto konkrétní změnou?“** a vyčkej na odpověď.

Schválení se vztahuje **jen na jmenovanou změnu**, ne na další úpravy v rámci stejného souboru. Pokud se ukáže potřeba další editace, znovu požádej. Před commitem, push, mazáním nebo nahrazením souboru požádej zvlášť. Pokud uživatel odpoví „ne“, změnu neprováděj. Samotné čtení a analýza jsou povoleny. Jestli nástroj spouštějící testy vytváří výstupní soubory, oznam to a požádej o schválení před spuštěním.

**Stejný bezpečnostní princip pro firmware editor:** před změnou **každé hodnoty profilu** musí UI explicitně potvrdit navrženou starou a novou hodnotu; žádný editovaný stav se nesmí ani v RAM změnit pouze navigací bez potvrzení. Před každým zápisem na SD musí být druhé, nezávislé potvrzení SAVE s přehledem všech odsouhlasených změn. Pouhé přepnutí rekordu/odchod z menu neukládá. Storno zachová původní metadata. Potvrzení úpravy RAM není souhlas se zápisem na SD.

## 1. Cíl

Doplnit do existujícího OLED UI editor sidecar metadat pro Sharp MZ soubory a QuickDisk image bez modifikace samotných obrazů, s výběrem **loader family** a **rychlosti** po jednotlivých rekordech. Zůstanou zachovány všechny funkce PLAY, RECORD, AutoName, metadata parserů, nezávislé MCPWM RX, RMT TX a obsluha SD.

Rozsah:

| Datový formát | Sidecar | Rozsah | Stav v aktuálním FW |
|---|---|---|---|
| `.MZF` | `.MFI` | jeden rekord | čtení existuje, omezený zápis MFI existuje |
| `.M12` | `.M2I` | jeden rekord | čtení existuje, editor/zápis není |
| `.MZT` | `.MTI` | rekordy 1..N | čtení sekcí `RECORD=` existuje, editor/zápis není |
| `.MZQ`, `.QDF`, `.QD` | **nově `.QDI`** | rekordy 1..N | QD parser existuje; QDI neexistuje |

Přípona `.QDI` je **nový projektový návrh, ne standard**. Navrhuje se **připojit za celý název image**: např. `GAMES.MZQ.QDI`, `GAMES.QDF.QDI`, `GAMES.QD.QDI`, aby nedošlo ke kolizím souborů se stejným stem. U ostatních podporovaných formátů zachovat dosavadní nahrazení přípony (`GAMES.MZT` → `GAMES.MTI`). Finální konvenci nejdříve předložit ke schválení, před implementací ověřit FAT/lowercase a kolize.

## 2. Ověřené části aktuální implementace — podklady k revizi

- `src/formats/sidecar.c`, `.h`: parsování `TYPE`, `SPEED`, pro MTI též `RECORD`; `sidecar_resolve()` využívá sidecar pouze při `LOADER_MODE_AUTO`. `sidecar_write_mzf()` nepřepisuje již existující MFI. `info_path()` nyní mapuje pouze MZF/M12/MZT, ne QD.
- `src/formats/file_format.c`: QD, QDF, MZQ mapuje na interní `FILE_FORMAT_MZQ`; `file_format_is_record_container()` platí pro MZT a MZQ.
- `src/formats/quickdisk.c`, `.h`: podporuje logický MZQ, QDF a fyzický QD/MFM; maximálně **50 rekordů**, rozhraní `quickdisk_count()` a `quickdisk_select(record, header, length)`.
- `src/audio/sharp_stream.c`: volá `sidecar_resolve(path, details.record, requested, &details.sidecar)` také pro QD; po doplnění QDI tak nebude nutné vymýšlet další mechanismus volby loaderu.
- Aktuální OLED: SSD1306 128×64, 8 textových řádků; pět tlačítek PLAY, STOP, NEXT, PREV, REC; krátké/dlouhé stisky už mají různé významy podle kontextu. V README r28 je dlouhý PLAY v browseru vstupem do PLAY menu. V selektoru MZT/QD je dlouhý PLAY nyní bez akce; krátké NEXT/PREV přepíná rekordy.
- Současné nastavení při přehrávání využívá loader profily v `src/play/loader_mode.h`. Ověřit platnost každé kombinace podle reálných parserů a playback enginu, nepřidávat neimplementované režimy.

**Před implementací ověř v aktuálním HEAD** všechny uvedené body. Pokud se stav liší, popiš změnu a přizpůsob návrh po schválení.

## 3. Vstup do editoru a navigace

### 3.1 Browser (všechny podporované soubory)

1. NEXT/PREV vybírá soubor.
2. **Dlouhý PLAY** otevře stávající PLAY menu, nedělej z něho přímý editor.
3. V PLAY menu přidej položku **EDIT SIDECAR** pouze pro MZF, M12, MZT, MZQ, QDF, QD.
4. Krátký PLAY na této položce otevře editor pro rekord 1, případně poslední rekord vybraný v selektoru daného souboru (navrhni jedno konzistentní pravidlo a požádej o schválení).
5. STOP se vrátí o úroveň zpět; žádné automatické uložení.

### 3.2 Selektor MZT / QD

1. NEXT/PREV standardně přepíná jednotlivé rekordy (beze změny dosavadního chování).
2. **Dlouhý PLAY** nově otevře editor **přímo pro právě vybraný rekord**.
3. Krátký PLAY zachová spuštění rekordu; STOP návrat do browseru.
4. Načítání metadat/preview rekordu neblokuje audio ani neovládá mechanický přepínač.

### 3.3 Editor

Orientace na 8 řádcích; příklad:

```text
SIDECAR EDIT
GAMES.MZQ
RECORD: 03 / 12
NAME: FLAPPY
>LOADER: IC
 SPEED:  1:4
 SAVE CHANGES
 CANCEL
```

- NEXT/PREV standardně pohyb kurzoru mezi řádky.
- Na řádku `RECORD` krátký PLAY aktivuje volbu čísla; NEXT/PREV volí číslo; PLAY potvrzuje přechod na rekord. Samotná změna výběru není změna dat.
- Na `LOADER`/`SPEED` PLAY otevře volbu hodnoty; NEXT/PREV mění **návrh hodnoty**, ale dosud nemění skutečný pracovní profil. PLAY zobrazí potvrzení `OLD -> NEW?`; teprve další potvrzovací PLAY uloží změnu do pracovní RAM. STOP při potvrzení vrací beze změny.
- `SAVE CHANGES` vyvolá přehled změn a **výslovné potvrzení zápisu na SD**, které je oddělené od potvrzení změn jednotlivých hodnot. Teprve potom provést transakční zápis.
- `CANCEL` vyžádá potvrzení zahození jen v případě existujících odsouhlasených neuložených úprav; jinak okamžitý návrat bez změn.
- Při přepnutí mezi rekordy se **již odsouhlasené pracovní změny uchovávají v RAM** a teprve společný SAVE je zapíše; změny v jiném rekordu se nesmějí ztratit.
- Nabídka loaderů musí zobrazovat dostupné kombinace, např. TC pouze 1:1, 1:2, 1:3; podrobně ověřit UL/MZ700. Pro UL režimy rychlost může být nepoužitelná a UI ji má zřetelně označit.
- Zobraz `EXISTING`, `NEW`, `INVALID` nebo `NO PROFILE` na základě skutečného stavu; neinterpretovat neexistující sidecar jako potvrzený profil NORMAL.
- V případě existujících neznámých řádků, duplicitních `RECORD`, nepodporované syntaxe a příliš dlouhého sidecaru volit **odmítnutí editace se srozumitelnou chybou**, nebo bezeztrátové zachování — ne tiché mazání nepodporovaných dat.

## 4. Datový formát a per-record chování

Jednore­kordový sidecar zůstává kompatibilní:

```ini
TYPE=IC
SPEED=1:4
```

Vícere­kordový MTI/QDI má sekce:

```ini
RECORD=1
TYPE=NORMAL
SPEED=1:1

RECORD=2
TYPE=IC
SPEED=1:4

RECORD=3
TYPE=TC
SPEED=1:3
```

- Číslování 1-based, odpovídá `quickdisk_select()` a MZT selektoru.
- Neexistující sekce nesmí vymazat profil jiného rekordu; při AUTO zachovat bezpečný fallback (podle dosavadního chování).
- Editor nesmí automaticky zaplnit chybějící sekce bez vědomého odsouhlasení uživatelem.
- Režim manuálně vynuceného loaderu se podle současného `sidecar_resolve()` neuplatňuje na sidecar; toto chování neměnit bez samostatného schválení. V UI uvést informaci `USED IN AUTO MODE`.
- Před prvním zápisem QDI ověřit, že reprezentace `UL` je kompatibilní se stávajícím parserem i bez SPEED, a totéž vyřešit pro UL_MZ700/UL_MZ800 a MZ700 režimy.
- Pro MZT/QD rekordy se sidecar vztahuje ke skutečnému obsahu aktuálního rekordu; metadata o programu (title, typ, load, exec, size) získávej z validované hlavičky, nikdy z fiktivně předpokládané payload struktury. Respektovat normalizaci IC/TC a další loadery s metadaty uvnitř prvního payloadu.
- QD zdrojová data zůstávají read-only; neopravovat CRC, neformátovat, nepřepisovat image.

## 5. Bezpečný zápis na SD a zotavení

Zápis provádět **pouze ve storage tasku** a mimo ISR, za předpokladu volného a stabilního SD media. Nevyžadovat běžící PLAY ani RECORD; neblokovat časově kritický přenos.

Po explicitně potvrzeném SAVE:

1. Zkontrolovat dostupnost SD, existenci zdrojového souboru, oprávnění a validitu všech pracovních profilů.
2. Připravit serializaci kompletního nového sidecaru mimo kritickou sekci, zachovat všechny ostatní rekordy.
3. Zapsat nový obsah do jedinečného dočasného souboru (nezničit starý).
4. Flush, close, read-back, parse a porovnat s očekávaným stavem. Ošetřit chybový návrat každého kroku.
5. Pro existující sidecar použít výměnu s bezpečnou obnovou/back-upem; nepředpokládat bez zkoušky atomickou výměnu FAT pomocí `rename` na existující jméno. Zdokumentovat konkrétní FAT/VFS chování a postup obnovy po výpadku napájení.
6. Po úspěchu znovu zavolat `sidecar_read()` pro změněné rekordy a zobrazit `SAVED`.
7. Při chybě zachovat původní sidecar a nepředstírat úspěch. Zbývající dočasné soubory řešit konzistentně při příštím startu či vstupu do editoru.

**Neprovádět overwrite souborů programu/image.** Při chybě záznamu ani při vyjmutí SD nevytvářet poškozený image. Pro existující `.mfi/.m2i/.mti/.qdi` řešit case-insensitive vyhledávání a zachování skutečného jména, nepřipustit paralelní dvojici `.MFI`/`.mfi` se spornou prioritou.

## 6. Volný chod firmware, HW a priority

- ESP32-S3 WROOM N16R8, SSD1306 128×64, SD SPI, RMT TX, MCPWM Capture RX, volitelný PCNT monitor. Žádná změna pinů.
- **S1 je fyzický čtyřpólový mechanický přepínač**, který přepíná skutečnou CMT trasu. GPIO21 pouze čte jeho stav s debounce; **není zde relé ani softwarově ovládaný mux**. Nezavádět žádné `relay_set()`, přepínání trasy přes GPIO, ani nové hardwarové předpoklady.
- Při externí trase musí zůstat příslušná blokace PLAY/REC. Editor nesmí automaticky spustit motor, přehrávání nebo záznam; u editoru při EXT upřesnit pravidlo jen po schválení.
- OLED, I2C, alokace ani I/O nikdy nespouštět z ISR; dlouhé operace na SD držet mimo obsluhu timingově kritických událostí.
- Nepřepisovat metadata cache přehrávače za běhu; pokud je aktivní PLAY/RECORD, editor vynuceně zakázat nebo explicitně požádat o ukončení (samotné ukončení také vyžaduje souhlas).
- Zachovat mapování tlačítek a obecné vlastnosti UI v r28, jeho pořadí menu a stávající latched chyby.
- Nerozšiřovat trvale RAM/PSRAM bez důkazu potřeby; pro 50 rekordů QD postačí malá pracovní tabulka. Vyhnout se fragmentaci opakovanými heap alokacemi.

## 7. Výslovná pravidla pro souhlas ve firmwaru

Rozlišit tři situace:

| Operace | Vyžadovaný souhlas |
|---|---|
| Výběr rekordu, prohlížení, pohyb kurzoru | Ne, nejde o změnu |
| Volba nové hodnoty `LOADER`/`SPEED` | Ano, explicitní potvrzení `OLD -> NEW` **před uložením do pracovní RAM** |
| Uložení přijatých změn do sidecaru | Ano, **samostatné potvrzení** seznamu změn **před zápisem na SD** |
| Zahození pracovních úprav | Ano, potvrzení DISCARD, pokud existují změny |
| Vytvoření nového QDI/MFI/M2I/MTI | Ano, potvrzení CREATE a zápisu |
| Přepsání existujícího sidecaru | Ano, potvrzení UPDATE včetně názvu a počtu změn |
| Přímá změna zdrojového MZT/QD/image | Zakázáno; mimo rozsah |

V ideálním případě jedna obrazovka `CONFIRM FIELD` pro jednu změnu hodnoty, pak souhrnná obrazovka `CONFIRM SAVE`. Když uživatel dlouho nereaguje, žádný auto-save.

## 8. Testy a akceptace (plán, neprovádět bez souhlasu)

Před jakoukoli změnou kódu předlož seznam host i HW testů. Pro spuštění testů vytvářejících artefakty nejprve vyžádej souhlas.

- MFI/M2I: vytvoření, čtení, změna, zrušení změny; neměnit datový soubor.
- MTI: alespoň 12 rekordů, editace 3 a 7 při zachování 1,2,4..12; přepínání rekordu se zachováním pracovní RAM.
- QDI: test `GAMES.MZQ.QDI`, `GAMES.QDF.QDI`, `GAMES.QD.QDI`, vyhnutí se kolizi, 1..50 rekordů, návrat do selektoru s původní pozicí.
- UL/IC/TC: správné kompatibilní profily, invalidní rychlost zamítnuta, fallback AUTO, manuální override zachován.
- Neexistující/poškozený sidecar, duplicitní rekord, příliš dlouhý řádek, diakritika v cestě, cesta max. 1024 B, case-insensitive přípona.
- Chyba SD při open/write/flush/read-back/rename; náhlé vyjmutí karty; obnova předchozího platného souboru.
- User confirmation: bez potvrzení nedojde ke změně RAM ani SD, každá jednotlivá změna vyžaduje souhlas, hromadné SAVE samostatný souhlas, STOP/timeout nikdy neukládá.
- Ověřit načtený loader pro každý rekord při PLAY AUTO a správné UI `sidecar present`.
- QD `.MZQ`, `.QDF`, `.QD` se stále přehrávají identicky jako před úpravou; READ-only image kontrolovat hash před/po.
- HW: S1 INTERNAL/EXTERNAL včetně změny polohy během editoru, zachování READ LOW/SENSE HIGH/MONITOR OFF v bezpečných stavech, žádné zásahy do MCPWM/RMT.
- `pio run -e MZCMTEmu -j 1`, host testy `python tests/host/run_tests.py --cc gcc`; výsledky zaznamenat pouze po schválení.

## 9. Výstupy požadované v první odpovědi Codexu (BEZ ZMĚN)

Vrať pouze:

1. Inventuru existujících parserů, UI a workflow pro sidecary, s cestami a konkrétními funkcemi.
2. Návrh editoru: obrazovky, ovládání, přepínání rekordů, potvrzovací dialogy, řešení změn v pracovní RAM.
3. Návrh QDI a serializace MTI, včetně kompatibility se stávajícími čtečkami.
4. Návrh bezpečného transakčního zápisu v podmínkách ESP-IDF/FAT.
5. Postup implementace **rozdělený na malé, samostatně schvalované změny** (jeden diff = jeden nový souhlas). Představ nejprve pouze **první konkrétní změnu** a vyžádej výslovné `ANO/NE`.

**Nepřepisuj zdroje, nespouštěj build ani testy, netvoř commit a nepokračuj k dalším změnám, dokud uživatel nedá samostatný výslovný souhlas.**
