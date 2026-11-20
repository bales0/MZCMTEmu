# Editor sidecarů a přepínač CMT

Firmware umožňuje upravovat profily MFI, M2I, MTI a QDI bez změny zdrojového programu nebo obrazu. OK na posledním kroku výběru přijme profil do pracovní RAM; zápis na SD se spouští přímo volbou SAVE CHANGES. Změny platí pro přehrávání v režimu AUTO. Manuální volba loaderu má dál přednost.

## Vstup a ovládání

V browseru otevře dlouhý PLAY stávající PLAY menu. Položka EDIT SIDECAR se objeví pouze pro MZF, M12, MZT, MZQ, QDF a QD. Z browseru editor začíná rekordem 1. Dlouhý PLAY v selektoru MZT/QD otevře právě vybraný rekord; po zavření editoru se obnoví původní pozice selektoru.

NEXT/PREV pohybuje kurzorem. PLAY na RECORD otevře volbu čísla; potvrzení vybírá rekord bez změny profilu. PLAY na LOADER otevře výběr rodiny. Další PLAY přejde přímo na volbu SPEED, pokud ji rodina používá. OK ve volbě rychlosti rovnou přijme celý profil do RAM, bez obrazovky porovnání nebo ACCEPT IN RAM. Na hlavní obrazovce je jediný volitelný řádek LOADER s aktuální rychlostí; samostatný řádek SPEED není. STOP v kterémkoli kroku návrh zahodí. U UL je rychlost N/A a OK na loaderu rovnou přijme profil bez kroku SPEED. SAVE CHANGES je samostatný vědomý příkaz k zápisu na SD.

Přijaté změny se uchovávají při procházení rekordů. PLAY na SAVE CHANGES rovnou spustí transakční zápis všech přijatých změn; přehled změn ani další potvrzení CREATE/UPDATE se nezobrazují. SAVE bez změn nic nezapisuje. CANCEL při neuložených změnách vyžaduje DISCARD. Navigace, STOP ani čekání nikdy automaticky neukládají. Během probíhající SD transakce se zobrazuje SAVING… a další tlačítka se neprovádějí. Úspěch vrací přímo hlavní obrazovku editoru bez SAVED a bez potvrzení; chyby zůstávají zobrazené.

SAVE a CANCEL sdílejí jedinou výběrovou položku hned pod LOADER. NEXT/PREV cyklí RECORD, LOADER a SAVE/CANCEL; všechny položky mají stejný kurzor. Ikony PLAY SAVE a STOP CANCEL jsou viditelné trvale, nezávisle na kurzoru. Akční nápověda dialogů zabírá jediný poslední řádek: ikona přehrávání znamená OK, čtverec zrušení/návrat. Nápovědy včetně KEYMAP používají jednobuněčné ikony tlačítek PLAY, STOP, RECORD, PREV a NEXT ve stejném čitelném fontu 6×8. Funkční mapování v KEYMAP zůstává zachováno; ANALYZING má zrušení na posledním řádku.

Dlouhé názvy zdroje a sidecaru se posouvají. Stav NO PROFILE je odlišen od NEW a EXISTING; neplatný nebo nepodporovaný soubor otevře chybovou obrazovku a neumožní editaci.

## Názvy a podporované profily

| Zdroj | Sidecar |
|---|---|
| GAMES.MZF | GAMES.MFI |
| GAMES.M12 | GAMES.M2I |
| GAMES.MZT | GAMES.MTI |
| GAMES.MZQ | GAMES.MZQ.QDI |
| GAMES.QDF | GAMES.QDF.QDI |
| GAMES.QD | GAMES.QD.QDI |

QDI je místní rozšíření projektu, nikoli standard SD2CMT2. Přípona se připojuje za celé jméno obrazu, takže různé obrazové formáty se stejným stemem nesdílejí sidecar. Vyhledávání ignoruje velikost písmen a zachovává skutečné jméno existujícího souboru. Více odpovídajících jmen na case-sensitive médiu znamená chybu, nikoli výběr jednoho z nich.

| TYPE | SPEED |
|---|---|
| NORMAL, IC | 1:1, 1:2, 1:3, 1:4 |
| TC | 1:1, 1:2, 1:3 |
| MZ700 | 1:1, 1:3 |
| UL, UL_MZ800, UL_MZ700 | neukládá se |

Jednore­kordové soubory obsahují TYPE/SPEED. MTI/QDI používají sekce RECORD=1..N. Chybějící sekce se nedoplňují automaticky. AUTO bez platného profilu zachovává dosavadní bezpečný fallback. Platný zvolený profil nezruší omezení konkrétního loaderu: nepodporovaný program, překryv loaderu nebo překročení 64 KiB může při PLAY dál vést na 1:1.

Editor odmítá neznámé řádky včetně komentářů, duplicity polí/rekordů, neúplné profily, neplatné kombinace a příliš dlouhá data. Tím zabrání tichému odstranění nepodporovaných údajů. Výstup má kanonické pořadí RECORD/TYPE/SPEED a LF konce řádků; původní formátování se neuchovává.

Limity: 16 KiB textu, řádek nejvýše 63 bajtů, 256 existujících či editovaných profilů. QD parser nadále dovoluje 50 rekordů. MZT může mít více rekordů; pracovní tabulka je řídká a omezuje počet profilů, nikoli samotné číslování. Cesty používají nejvýše 1024 bajtů včetně terminátoru, FAT komponenty nejvýše 255 bajtů. Editor navíc potřebuje čtyři znaky pro transakční přípony a při nedostatku místa zápis odmítne.

Host test ověřuje zachování UTF-8 názvů. Cílový FAT je ovšem nastaven na CP437/OEM; firmware kódování ani názvy nepřevádí. Podporu konkrétních znaků na fyzické SD je nutné ověřit v tomto nastavení, host test ji pro FAT nedokazuje.

## Zápis a obnova SD

Načítání a SAVE/RECOVERY probíhají jako asynchronní úlohy existujícího storage tasku. Editor je během PLAY, pauzy PLAY, čekání na motor nebo aktivního RECORD nepřístupný; nic automaticky neukončuje. Žádné I/O, OLED ani alokace neběží z ISR. Backend používá pevně omezené tabulky a pracovní buffers bez opakovaných heap alokací.

Před SAVE se porovná existence, velikost a mtime zdroje, skutečné jméno a původní obsah sidecaru (FNV-1a a délka). Změněný či nejednoznačný soubor se nepřepíše. Celá serializace zachová profily ostatních rekordů.

Ke skutečnému jménu sidecaru se používají pomocné přípony:

- SCJ: žurnál s příznakem původního souboru a kontrolními hodnotami starého/nového obsahu.
- SCN: exkluzivně vytvořený nový soubor.
- SCB: záloha původního souboru.

Žurnál se zapíše, flushne, zavře a zpětně ověří před vytvářením SCN. Nový soubor projde flush/close a zpětným parsováním se srovnáním všech profilů. Původní sidecar se poté přejmenuje na SCB a SCN na uvolněné původní jméno. Finální soubor se znovu ověří a změněné rekordy se načtou přes sidecar_read(). Až pak se odstraní záloha a žurnál a editor se vrátí do hlavního menu. Všechny návraty open/write/flush/close/rename/read-back se kontrolují.

ESP-IDF VFS v instalované verzi volá FatFs f_rename(). Existující cílové jméno vrací FR_EXIST/EEXIST; nahrazení existujícího souboru pomocí POSIX rename se proto nepředpokládá. Přejmenování celé transakce není atomické. cmt_fflush() ve firmwaru volá fsync; ani to nezaručuje zotavení z libovolného poškození FAT či výpadku uvnitř operace karty.

Při nalezení platného žurnálu nabídne editor RECOVERY a vyžádá samostatné potvrzení. Platný nový nebo původní cílový soubor ponechá; chybí-li původní jméno, obnoví ověřenou zálohu. Poté odstraní související dočasné soubory a znovu načte SD profily. Obnova rovněž kontroluje zdroj po opětovném vložení média. Nesouvisející, nejednoznačné či poškozené soubory a neplatný/částečný žurnál se automaticky nemažou; editor vrátí chybu a zachová podklady pro ruční obnovu. Chyba úklidu po ověřeném zápisu se hlásí jako SAVED / CLEANUP ERROR, nikoli běžné úspěšné SAVED.

## INTERNAL a EXTERNAL CMT

Dočasný bypass S1 je vypnutý. GPIO21 LOW znamená EXTERNAL, HIGH INTERNAL; změny mají debounce 40 ms. Obě polohy mají při přepnutí odpovídající hlášku na 1,2 sekundy, se stejnou prioritou. Trasu fyzicky přepínají kontakty S1. Firmware žádné relé ani mux neovládá.

Podle upřesnění uživatele EXTERNAL blokuje pouze přehrávání: nelze spustit ani vyzbrojit PLAY a přepnutí ukončí běžící nebo pozastavený PLAY. RECORD, jeho odposlech, menu, prohlížení rekordů a editor zůstávají dostupné. S1 záznam nepřeruší. Obrazovka RECORD ukazuje skutečný stav a EXT. V EXTERNAL zůstává READ LOW a SENSE HIGH; monitor je povolen při běžícím RECORD, nikoli pro TX. Piny, MCPWM, PCNT a RMT zapojení se nemění.

## Ověření

Testy pokrývají všechny formáty, 12rekordové MTI se změnou rekordů 3/7, 50rekordové QDI, zachování chybějících sekcí, potvrzení polí a SAVE, DISCARD po ztrátě média, blokaci během asynchronní práce, case lookup, mezní cesty, poškozené vstupy, AUTO/manuální prioritu a nezměněné zdrojové soubory. Skutečný player/RMT test ověřuje tři různé QDI profily během kontinuálního přehrávání.

Fault testy simulují chyby open/read-back, krátký zápis, flush, close a oba rename kroky. Deset případů přerušení transakce pokrývá CREATE i UPDATE v pěti bodech žurnál/nový soubor/záloha/instalace/úklid. Host simulace nedokazují fyzickou odolnost FAT při výpadku napájení.

[Náhled editoru](validation/sidecar-editor-preview.png) je vykreslen produkčním OLED písmem. Všech 27 host sad prošlo s GCC 14.2 a ASan/UBSan; cílový PlatformIO build PASS. RAM 167 292 B, proti výchozí verzi navíc 10 976 B; aplikace 475 067 B. Finální výsledky a identifikace obrazu jsou v [souhrnu](validation/sidecar-editor-summary.json).

HW ověření zbývá: skutečný S1 v obou polohách a při RECORD/editoru, záznam z externího CMT, SAVE na FAT, vyjmutí karty a vypnutí napájení v jednotlivých fázích. Firmware nebyl nahrán do desky. Bez commitu, větve a push.

## Zjednodušení editoru ve verzi r30

Schváleno 20. 11. 2026: odstraněna oddělená potvrzení rodiny a rychlosti. Cílový build a host testy test_sidecar_editor, test_oled_ui a test_route_switch s ASan/UBSan PASS. Doplňkové případy ověřují přechod z IC1:4 na TC a MZ700, povolené rychlosti, přeskočení SPEED u UL a jediné přijetí profilu před odděleným SAVE. [Aktuální náhled](validation/profile-confirm-preview.png) a [souhrn](validation/profile-confirm-summary.json).

Úplný doplněný výpis ukázal po MOTOR/PLAY QD v AUTO FreeRTOS assert xQueueSemaphoreTake a restart, nikoli návrat způsobený timeoutem. Popsaná oprava zásobníku níže míří na tento pád; přehrávací stavový automat se nemění.

## Oprava zásobníku při QD AUTO

Dne 20. 11. 2026 bylo schváleno a upraveno pouze vyhledávání sidecaru a jeho regresní testy. sidecar_find() nyní používá výstupní buffer místo tří lokálních polí canonical/directory/found. Při synchronním cmt_opendir() dočasně ukončí cestu za posledním lomítkem a ihned obnoví první znak názvu; root i relativní adresáře zůstávají podporované. Smíšená velikost přípony, skutečné jméno a detekce kolizí se zachovávají. Funkce neprovádí žádný zápis na SD.

Ve skutečně sestaveném ESP32-S3 ELF klesl rámec funkce z 2464 na 144 bajtů, úspora 2320 bajtů. Velikost app tasku zůstává 8192 bajtů; statická RAM 167292 bajtů. Součet vybraných rámců původní vnořené cesty dosahoval 7424 bajtů bez části knihoven/RTOS. Není to měření runtime high-water mark; společně s poškozeným backtrace je to silný podklad pro podezření na přetečení zásobníku. Přesný r29 ELF k původnímu pádu nebyl dostupný, proto je mapování jeho adres proti r30 pouze orientační.

Sedm host sad s GCC14 ASan/UBSan PASS: editor/cesty, fault injection, Sharp stream, player/RMT, player/Sharp, writer I/O a recorder. Kontroly zahrnují relativní a absolutní cesty, ./, malé a smíšené přípony, výstup na místě, přesnou kapacitu, chybějící adresáře a nezměněná vstupní/SD data. QDI AUTO po STOP a novém PLAY i záznam/AutoName zůstávají funkční v simulaci. Cílový build PASS. Žádná změna pinů, časování ani pravidel AUTO.

[Výsledky a SHA](validation/sidecar-stack-summary.json), [rámce](validation/sidecar-stack-frames.txt), [host log](validation/sidecar-stack-host.txt) a [build log](validation/sidecar-stack-build.txt). Boot banner zůstává r30. Přesný nový ELF je pro případný další backtrace uchován v .tmp/sidecar-stack-firmware.elf. Firmware nebyl nahrán do desky; odstranění konkrétního HW pádu je potřeba potvrdit opakováním PLAY uvnitř barbarian2.qd v AUTO.

## Název prohlíženého rekordu ve verzi r31

Při volbě RECORD se nyní název načítá pro právě prohlíženého kandidáta ještě před PLAY. Potvrzený rekord a pracovní profily přitom zůstávají beze změny; STOP obnoví původní výběr i název. Náhled využívá validovanou hlavičku přes player_prepare_record v klidovém stavu, nezasahuje do běžícího přenosu. Neúspěšný náhled zobrazí INVALID RECORD; starý název se nesmí vydávat za název nové položky. Dlouhé názvy se posouvají.

CHOOSE LOADER ukazuje pouze jméno rodiny. Rychlost se zobrazí až v CHOOSE SPEED a ve společném potvrzení profilu. SAVE je nadále nezávislý. ABOUT již nemá STOP BACK / NEXT MORE; tento řetězec není ani ve finálním firmware.bin. Spodní řádek obsahuje stav RX.

Čtyři host sady s GCC14 ASan/UBSan a cílový build PASS. Pixelové náhledy používají produkční OLED renderer. Rámec app_task zůstává 1216 B a opravený sidecar_find 144 B. [Náhled](validation/editor-preview.png), [souhrn](validation/editor-preview-summary.json), [host log](validation/editor-preview-host.txt), [build log](validation/editor-preview-build.txt). HW ověření a nahrání zbývají; bez commitu.

## Jeden řádek profilu a přímé uložení ve verzi r33

Na základě explicitního souhlasu z 20. 11. 2026 má editor čtyři volby: RECORD, LOADER s rychlostí, SAVE CHANGES a CANCEL. Samostatná volba SPEED, REVIEW CHANGES a CONFIRM CREATE/UPDATE byly odstraněny. Průchod LOADER → SPEED → jediné potvrzení profilu zůstává. Jediný PLAY na SAVE CHANGES zařadí asynchronní transakci; teprve po jejím úspěchu se ukáže SAVED. Během SD WORKING se další stisky ignorují. CANCEL/DISCARD, kontrola původních dat, žurnál, záloha a read-back zůstávají zachované.

Testy přímo ověřují jeden stisk SAVE pro vytvoření i aktualizaci, že před SAVE neproběhl zápis a že zrušení ponechá SD obsah. test_sidecar_editor, test_sidecar_faults a test_oled_ui s GCC14 ASan/UBSan PASS; cílový build PASS. [Náhled](validation/direct-save-preview.png), [souhrn](validation/direct-save-summary.json), [host log](validation/direct-save-host.txt), [build log](validation/direct-save-build.txt). HW ověření zbývá, bez commitu a nahrání.

## Přímé přijetí profilu a ikonové nápovědy ve verzi r34

LOADER → SPEED → OK nyní přijme profil přímo do pracovní RAM; UL se přijme OK už na loaderu. Porovnání OLD/NEW a ACCEPT IN RAM byly odstraněny. STOP během volby ponechá původní profil. Na SD se zapisuje pouze přes SAVE CHANGES; transakční backend a explicitní obnova se nemění. SAVE a CANCEL sdílejí jednu poslední řádku; po jejím vybrání se ukáže ikonová nápověda PLAY SAVE / STOP CANCEL. Kurzor cyklí pouze tři volby: RECORD, LOADER a SAVE / CANCEL.

Nápovědy používají pět jednobuněčných ikon tlačítek 6×8 včetně KEYMAP. Dialogy a ANALYZING mají akční nápovědu pouze na posledním řádku. Testy ověřují rozdílné glyfy, kruhový RECORD oproti čtvercovému STOP, obě textové cesty, zvětšení, ořez a délku patiček do 21 buněk. Dále ověřují přímé přijetí profilu, STOP bez změn, přímý SAVE, chybové a obnovovací dialogy a původní fault injection transakcí.

Čtyři sady test_menu, test_oled_ui, test_sidecar_editor a test_sidecar_faults s GCC14 ASan/UBSan PASS. [Náhled produkčního rendereru](validation/compact-editor-preview.png), [host log](validation/compact-editor-host.txt). Cílové sestavení je zaznamenané v souhrnu ověření; HW ověření zbývá. Bez commitu a nahrání.

Cílový build r34 PASS: RAM 167292 B, program 473527 B. [Souhrn a SHA-256](validation/compact-editor-summary.json), [build log](validation/compact-editor-build.txt). Zůstává dřívější varování o nepoužité ui_copy_scroll_text.

## Výběrová položka SAVE/CANCEL ve verzi r35

SAVE/CANCEL nyní navazuje hned pod LOADER jako třetí položka menu. Má stejný kurzor jako RECORD a LOADER, navíc trvale zobrazené ikony PLAY SAVE a STOP CANCEL. Poslední dva řádky hlavní obrazovky zůstávají volné. PLAY ukládá pouze při zvolené položce SAVE/CANCEL; STOP se chová jako dosud.

Při zápisu je SAVING…; úspěch vrací přímo hlavní obrazovku s aktualizovaným profilem, bez SAVED či dalšího potvrzení. Prázdný SAVE nic nezapisuje. Zpracování chyb, zahození neuložených změn a transakční backend zůstávají zachovány.

Tři relevantní host sady s GCC14 ASan/UBSan a cílový build PASS. [Náhled](validation/editor-save-preview.png), [souhrn](validation/editor-save-summary.json), [host log](validation/editor-save-host.txt), [build log](validation/editor-save-build.txt). Bez commitu a nahrání; HW ověření zbývá.
