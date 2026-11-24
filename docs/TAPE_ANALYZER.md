# TAPE ANALYZER a běžný PLAY — r48

Implementace podle zadání z 21. 11. 2026, výchozí commit
`4ea885ef68b1aa8392ed17b9b86d13f983bed11a`.
Výsledky měření a sestavení jsou v [ověřovacím protokolu](validation/tape-analyzer-summary.json).

## Běžný PLAY: pouze délka

Běžný PLAY nezapíná obsahovou analýzu ani hledání programů. LEP/L16 před
startem pouze sečte délky intervalů a uloží řídké časové checkpointy pro
případný posun. Obrazovka uvádí COUNTING a LENGTH ONLY, bez názvů programů,
LOAD/EXEC nebo počtu kandidátů. PLAY může délkový průchod přeskočit, STOP
zruší celý start. WAV vezme délku přímo z RIFF/PCM hlavičky, bez skenu vzorků.
Přeskočená analýza délky se při navigaci automaticky neobnovuje.
Průběh ukazuje procenta a jediný řádek SIZE v KB/MB (jednotky 1024 B),
bez dvojice přečtené/celkové bajty. Délkový průchod LEP/L16 také levně
uchová dlouhé úseky bez změny úrovně; nezapíná tím obsahové dekodéry.

Předběžná obsahová metadata se při běžném PLAY nezobrazují. Živá metadata
nadále vznikají z přehrávaného signálu a publikují se až podle fyzicky
dokončeného výstupu. Celý obsahový index vzniká jen po výslovném spuštění
TAPE ANALYZER v PLAY menu.

## Použití

1. V browseru vyberte LEP, L16 nebo WAV, dlouhým PLAY otevřete PLAY menu
   a zvolte **TAPE ANALYZER**. EDIT SIDECAR zůstává samostatnou položkou pro
   podporované Sharp kontejnery.
2. Analýza ukazuje procenta, ověřené a nejisté kandidáty a aktuální název.
   STOP ruší celý start. Analýza nemá automatický timeout. Při zásahu do S1
   nebo ztrátě SD skončí; změna generace média zneplatní cache.
3. **TAPE CONTENTS** obsahuje PLAY WHOLE TAPE a rozpoznané položky. PLAY
   otevře detail s názvem, typem, loaderem/rychlostí, velikostí, LOAD/EXEC,
   absolutním START a neutrálním nadpisem PROGRAM N/N. Seznam ukazuje pět
   položek; stavové texty indexu a jeho cache nezabírají řádek obrazovky.
4. U ověřené položky další PLAY spustí známý načítací řetězec od jeho
   skutečné pozice, včetně potřebného IC/TC loaderu. U nejisté nebo poškozené
   položky s dostupným časovým začátkem PLAY spustí původní nahrávku od
   START ~čas do konce souboru. Chybějící ověření řetězce neblokuje časový
   start a nezobrazuje rušivou hlášku ani další potvrzení. Bez známé pozice
   zůstane START --; začátek se nevymýšlí. Přehrání celé nahrávky se vybírá zvlášť
   přes PLAY WHOLE TAPE v seznamu, bez tichého přepnutí výběru. Neznámé
   adresy a profily nejsou odhadovány. Interní neúplnost, validita a limity
   zůstávají zachované pro rozhodování o startu; nezobrazují se jako varovné
   nadpisy. Bez kandidátů zůstává volba PLAY WHOLE TAPE.
5. STOP během přenosu vrací seznam bez opakování analýzy. Opětovné otevření
   platného RAM indexu otevře rovnou seznam, bez falešného skenu a CACHE HIT.

### Přetáčení

Dlouhý NEXT/PREV (700 ms) při PLAY nejprve automaticky přepne přenos do
uživatelské pauzy a zapne posun přímo na PLAY obrazovce. Název a metadata
zůstanou zobrazené, hlavička ukazuje PAUSE FF/REW a dole je navrhovaná
absolutní pozice. Funguje v běžném PLAY LEP/L16/WAV i uvnitř
TAPE ANALYZER; lze jej použít také z uživatelské pauzy nebo FINISHED.
Držení postupně používá kroky 5, 15 a 60 sekund
(hranice držení 2 a 6 sekund). Pozice se omezuje na rozsah celé nahrávky.
Uvolnění zůstává v PAUSE SEEK na stejné obrazovce. Krátké směrové stisky v tomto režimu
posouvají o 5 sekund až při uvolnění.

STOP návrh zahodí a ponechá původní přenos pozastavený. Jediný PLAY
spustí původní waveform od zvoleného času; není další potvrzovací ani
samostatná obrazovka posunu ani hlášky UNVERIFIED START/CHAIN.
Pozastavení může přerušit LOAD na
Sharpu; uprostřed bloku se nemusí znovu synchronizovat. Zdroj se přestavuje
vždy až po pozastavení TX; zrychlené pulzy se nevysílají. MOTOR pauza seek
sama nezapíná. Posun přes konec skončí jako EOF
bez spuštění prázdné DMA transakce.

Čas i procenta vždy používají začátek původní nahrávky. Celková délka se
po posunu ani při výběru programu nemění; neobsahuje přidané úvodní a
koncové ticho TX. Například posun na 01:30 v pětiminutovém záznamu ukáže
01:30/05:00. Po skončení samostatného programu uprostřed souboru zůstane
jeho absolutní koncová pozice, nikoliv 100 % celé nahrávky. Čas zůstává
zmrazený v pauze, po skončení i po STOP; čerpá z dokončeného TX, ne předplnění.

Při náhledu posunu se metadata vyhledají podle cílové pozice v obsahovém
indexu, bez vazby na původně vybraný program. Jednoznačný ověřený rozsah
ukáže cílový program s označením INDEX PREVIEW. Mezery, nejisté či překrývající
se rozsahy a běžný PLAY bez obsahového indexu zobrazí jen název nahrávky,
bez přenesených údajů původního programu. STOP náhled zruší a obnoví původní
metadata. Potvrzený časový start resetuje živý dekodér; předběžná metadata
z indexu se nevydávají za fyzicky přehrané údaje nového místa.

Při neznámé délce otevře dlouhý NEXT/PREV skutečný procentní FF/REW.
Držení mění cílovou bajtovou pozici v krocích 1/5/10 % (po 2/6 s), rovnou
viditelnou ve spodním řádku vedle BUF. Časový řádek SEEK se nezobrazuje.
PLAY provede přímý seek na zvolený offset. Nenulový signed bajt sám určuje
úroveň; u pokračovací nuly se zpětně dohledá předchozí nenulový bajt.
Nesčítají se znovu délky od začátku. Dlouhá řada nul může vyžadovat delší,
stále zrušitelný lookback pro obnovu úrovně. STOP vrátí původní pozici.
Přesah za EOF se omezí na skutečný konec bez prázdné DMA transakce.
Bez známého absolutního času cílové pozice zůstane PLAY v procentech;
čas dočteného zbytku se nesmí vydávat za celkovou délku nahrávky.

Běžný browser a menu zachovávají původní okamžité NEXT/PREV a repeat
400/120 ms. Při PLAY LEP/L16/WAV se krátký NEXT provede při uvolnění;
PREV rozlišuje jednotlivý a dvojitý klik v okně 350 ms. Dlouhý stisk nikdy
nejdřív neposune browser.
Kontext seeku odděluje krátký stisk, dlouhý
stisk, repeat a uvolnění. Změna kontextu potlačí držené směrové klávesy do
uvolnění; STOP má prioritu. V utilitě navigace neposouvá browser na pozadí.

### Skoky přes mezery

Mezera je souvislý úsek bez změny digitální úrovně dlouhý alespoň 500 ms,
v libovolné polaritě. LEP/L16 zahrnuje pokračovací nuly přes čtecí bloky;
WAV používá totožnou hysterezi jako PLAY, včetně deadbandu. Nález mezery
není důkazem logické hranice programu; mohou existovat i uvnitř načítání.
U WAV tedy nejde o test nízké amplitudy ani o jediný práh 128: LOW vznikne
při hodnotě ≤ 0x40, HIGH při ≥ 0xC0 a mezilehlé hodnoty drží stav. Test
zašuměného úseku opakovaně překračuje střední hodnotu v obou výchozích
úrovních, aniž by vytvořil falešné pulzy nebo přerušil detekovanou mezeru.
Nepřidává se detektor série po sobě jdoucích pulzů ani další dekódování.
Špičky překračující oba hysterezní prahy tedy stále mohou mezeru rozdělit.

Krátký NEXT při přenosu nebo uživatelské pauze vyhledá následující cíl
200 ms před koncem mezery. Cíle nepřetékají cyklicky. Pro skok se používají
jen mezery ukončené následujícím signálem; samotné
koncové ticho nevytvoří cíl těsně před EOF. Při původně běžícím PLAY
se přenos po přestavení automaticky obnoví za stávajících podmínek MOTOR/S1;
uživatelská pauza zůstane pauzou. MOTOR pauza ani čekání na MOTOR tyto akce
neaktivují. Bez nálezu se pozice nemění a krátce se ukáže NO NEXT/PREV GAP.
Jednoklik PREV vrátí začátek aktuálního úseku. První program se vrací na
offset/čas 0, i když nahrávka začíná tichem. Dvojklik PREV do 350 ms vrátí
předchozí začátek; v prvním úseku zůstane pozice a zobrazí se NO PREV GAP.
Začátky se dál určují z fyzických mezer, bez tvrzení o nezávislosti loaderu.

Jednoklik čeká 350 ms. Druhý klik v tomto intervalu provede jediný seek na
předchozí začátek; neprovedou se dva přenosy za sebou. Cíl se počítá z pozice
a režimu zachyceného při prvním kliku, i když PLAY mezitím pokračuje.
Přesná rovnost s aktuálním začátkem stále znamená aktuální úsek, nikoliv
předchozí. Není už použit historický filtr posledního úspěšného skoku.

Čekání ruší STOP (včetně držení), NEXT, PLAY, dlouhé stisky, změna kontextu,
MOTOR pauza nebo čekání na MOTOR. Jednoklik ani dvojklik se neuplatňují
v browseru, menu ani seznamu analyzátoru. Běžná akce STOP, vstupní debounce
a ovládání FF/REW zůstávají stejné. Uživatelská pauza se po seeku zachová.

Obsahový sken a délkový LEP/L16 průchod uchovají nejvýše 256 mezer. U WAV
bez gap indexu první krátký skok spustí samostatný gap-only průchod, se
SEARCH GAP procenty a STOP pro zrušení. Nevolá obsahové dekodéry a normální
start WAV nezpomaluje. Výsledek se uchová v RAM; při zaplnění tabulky nebo
nedostatku paměti se hledá zrušitelným průchodem, takže pozdější mezery
nejsou mylně vydávány za konec nahrávky. STOP hledání ukončí i přenos.
Obsluha tlačítka STOP zůstává beze změny.

U LEP/L16 s přeskočenou délkou navigace nejprve ověří pouze identitu zdroje
a výchozí checkpoint, bez čtení celé nahrávky. Mezery mají bajtové hranice
a cachovaný cílový offset v původním zdroji. NEXT hledá do prvního vhodného
cíle; PREV podle potřeby dokončí mapování úseku před aktuálním offsetem.
Rozpracovaný průchod uchová checkpoint i stav právě sledovaného intervalu;
další požadavek pokračuje odtud. Již nalezené cíle se použijí bez dalšího
čtení waveformu. Částečný průchod se nevydává za hotový index mezer.
Pokud další mezera neexistuje, NEXT samozřejmě musí dojít až k EOF.
Při průchodu od známého časového počátku až k EOF se zjistí skutečná délka;
po přímém skoku na neznámý čas ji nelze odvodit ze zbytku. Přesný celkový
čas se neodhaduje z poměru bajtů a času. Procenta po seeku zahrnují zdrojový offset i fyzicky
dokončené bajty, nikoliv předplněný buffer. Pauza a STOP je zmrazí.
Živý dekodér se při změně pozice resetuje, aby se nepřenesla stará metadata.
Pro start uprostřed dat se počáteční stav ukáže jako UNKNOWN / FILENAME;
nejde o nový obsahový sken a nápis DETECTING se neopakuje. Skutečně přehraná
hlavička může později metadata normálně potvrdit.

## Waveform, metadata a vlastnictví

- `tape_analyzer.c` vlastní obsahový sken a jeho RAM index, odděleně od
  lehké časové cache běžného PLAY. Časová cache nevolá obsahové dekodéry.
  Kurzor je samostatná instance se svým souborem, 1024B bufferem, úrovní,
  hysterezí a časem. Žádný soubor nahrávky se neotevírá pro zápis; nejde
  o konverzi ani extrakci. Převod LEP→WAV v host testu pouze vytváří fixture.
- LEP/L16 sečte signed intervaly jedním průchodem: nula pokračuje v úrovni
  se 127 jednotkami, -128 má 128 jednotek, jednotka je 50/16 µs. Stejná
  úroveň se pro dekodér spojuje přes I/O hranice. Dlouhý plochý interval
  zůstává plochý i přes pracovní řezy, nestává se falešným leaderem.
- WAV používá společnou hysterezi PLAY (LOW ≤ 0x40, HIGH ≥ 0xC0) a převod
  absolutního čísla vzorku na čas. Podpora zůstává PCM 1, mono, 8 bitů,
  1–1 000 000 Hz, block_align 1 a byte_rate = sample_rate. RIFF kontroluje
  velikosti, rozsahy a zarovnání; data nemusí být na pevné pozici ani za fmt.
  Hlavička poskytuje délku, waveform se skenuje kvůli obsahu a checkpointům.
- Obsahový TAPE ANALYZER používá soukromou instanci společných dekodérů, oddělenou od
  RECORD/PLAY. Pasivní observer dodává strukturální hranice. IC metadata se
  normalizují, TC přijme cílová metadata až po validaci prvního loader
  payloadu. AUX Sinclair/CP/M metadata se uchovají jako nejistá, pokud chybí
  důkaz celého samostatného načítacího řetězce a jeho začátku.
- Platná Sharp hlavička sama nezpřístupní PLAY PROGRAM. Potřebuje platný
  payload, známý začátek, podporovaný řetězec a strojový vstup uvnitř
  nahrávaného obrazu. BASIC/modulové předchůdce neprohlašuje za nezávislé
  hry. Rozpoznaný nejasný předchůdce zneplatní předpoklad nezávislosti
  následujícího nativního modulu. Opakované kopie nevytvářejí falešně ověřené
  programy. Index rozlišuje počet kandidátů, počet ověřených a úplnost.
- Bloky jsou hrubé strukturální rozsahy: leader, hlavička/payload včetně
  checksumu, mezera a neklasifikovaný úsek. Nejsou tabulkou jednotlivých hran.
  Intervaly mezi ověřenými částmi se při PLAY nijak nefiltrují; přenáší se
  celý původní zdrojový rozsah. Samostatný PLAY končí za ověřeným payloadem
  a jeho poslední půlvlnou, nepokračuje do následující hry. Pro celou původní
  sekvenci včetně dalších kopií/bloků slouží PLAY WHOLE TAPE.
- Storage task nadále vlastní nativní SD deskriptory. Velikost FS cache,
  priority, core affinity, ISR a GPIO se nemění. S1 EXTERNAL blokuje pouze
  PLAY; pravidla RECORD a sidecar editoru zůstávají zachována.

## Čas a transport

Index a checkpointy používají původní waveform bez syntetického prerollu
300 ms a tailu 500 ms LEP/L16. Tyto úseky se nepředávají dekodéru; při PLAY
zůstávají součástí fyzického času a mají vypnutý odposlech. START a SEEK
ukazují absolutní čas pásky. Spodní řádek přenosu ukazuje absolutní čas
od začátku / celkový čas celé původní nahrávky a BUF, nebo klasická procenta,
pokud celková délka dosud není známá.

Checkpoint obsahuje zdrojový offset, úroveň, stav hystereze, absolutní
vzorek, přesný čas v 16MHz tickách a případný zbytek intervalu. Programové
kotvy jsou připnuté; běžné časové checkpointy se po naplnění zhušťují.
Časový checkpoint sám není potvrzený začátek programu. `time_only`
rozlišuje délkový index od obsahového; nemá programovou ani blokovou tabulku.

WAV při běžném PLAY nevyžaduje předběžné časové checkpointy. Až při seeku
se přesná úroveň a hystereze zjistí zpětným hledáním posledního prahového
vzorku. Dlouhé pásmo hodnot mezi prahy může vyžadovat delší čtení dozadu;
zobrazí se SEEKING s průběhem a STOP jej zruší. Nepoužívá se odhad úrovně ani
obsahový dekodér. Pokud délka LEP/L16 chybí, připraví navigace pouze
identitu a počáteční checkpoint. Přirozené dočtení zdroje zjistí celkový čas
jen tehdy, pokud má průchod známý časový počátek. Byte seek tuto podmínku
obecně nesplňuje a nepřejde po dočtení zbytku na falešný časový údaj.

Přestavení má pořadí STOP TX → vyřazení pending/FIFO/DMA → obnova zdroje →
reset metadatové timeline a dokončeného času → prefill → USER_PAUSED.
TX spustí povolený PLAY nebo explicitní krátký skok z již běžícího přenosu;
MOTOR nesmí sám obnovit návrh ručního FF/REW.
SENSE je před spuštěním aktivní LOW, MOTOR zůstává HIGH=ON. Indexové
metadata jsou preview, živé potvrzení dál čeká na fyzicky dokončený TX.
PAUSE/STOP/EOF používají dokončený čas, nikoli čtecí pozici nebo prefetch.
GPTimer zaokrouhluje přímo absolutní vzorkový čas, bez dvojího zaokrouhlení.

## Paměť a cache

Velikosti ověřené cílovým Xtensa překladačem:

| Tabulka | Kapacita | Bajtů na položku | Celkem |
|---|---:|---:|---:|
| Programy | 256 | 152 | 38 912 |
| Bloky | 2048 | 56 | 114 688 |
| Checkpointy | 4096 | 40 | 163 840 |
| Mezery obsahového indexu | 256 | 40 | 10 240 |
| PSRAM pooly utility celkem | | | **327 680 (320 KiB)** |

Pooly se alokují jednou v PSRAM a znovu používají; per-edge cesta nealokuje.
Tyto programové/blokové pooly se kvůli běžnému PLAY nealokují. Samostatná
lehká časová cache LEP/L16 má nejvýše dalších 4096 × 40 B = 160 KiB PSRAM
a vlastní 10KiB tabulku mezer. WAV ji alokuje až při explicitním hledání mezery.
Pokud její alokace selže, délka se stále vypočte a časový seek může číst od
počátku zrušitelným průchodem. Přímý procentní byte seek checkpointový pool
nevyžaduje. WAV před běžným PLAY tento pool nepotřebuje.
Prvních 256 checkpointových míst rezervují kotvy programů. Přetečení
programové/blokové tabulky zachová nalezené položky a délku. Zakáže ověřený
samostatný řetězec, ale známá časová pozice dál umožňuje explicitní start.
Selhání alokace základních poolů je chyba
utility; běžné přehrávání z browseru zůstává samostatné.

RAM cache obsahového a časového indexu kontroluje cestu, formát, velikost, stat časy/identitu, generaci
storage a tři 64B vzorky zdroje. Malé fingerprintové sondy před/po skenu
nejsou dalším dekódovacím průchodem. Vzorkovaný fingerprint není důkaz
úplné identity: externí změnu mimo sondy se zachovanými stat údaji a bez
změny generace média nelze zaručeně zjistit. Remount/removal/write epochy
cache zneplatní; zdroj se ověřuje i před startem a obnovením.

Používá se pouze RAM cache. Persistentní IDX a jeho zápis se nezavádí.
Offsety/časy mají 64 bitů; soubory za rozsahem existujícího `long` seek API
(na cíli nad LONG_MAX) se odmítnou, nikoli přetečou.

## Ověření

Read-only reference `Export.l16` (2 589 411 B) má délku waveformu
343,029776 s a šest pauz nad 500 ms. Úvodní má 1,008240 s, dalších pět
2,016080 s. Časové začátky úseků pro PREV jsou 0; 23,771600; 109,045616;
177,404240; 233,115344; 289,607504 s. Z pozice 120 s vrátí jednoklik
109,045616 s, dvojklik 23,771600 s. Testy zahrnují původní časové i bajtové
offsety, cache a skutečné obnovení obou odlišných cílů přes simulovaný RMT.
SHA256: `aa7269685cbd156df411ee401a115932eec13c47d41dd7a2f26b356a4cc6886c`.

Negativní read-only reference `Barbarian2.l16` (2 577 411 B) má délku
333,240640 s a jedinou pauzu 345,072 ms na 6,359152–6,704224 s (bajty
24508–24854). Současný limit 500 ms ji vyřadí. Procentní seek a přesný
přenos suffixu zůstávají stejné. Žádný zdroj se neotevírá pro zápis.
SHA256: `71950dd0a4ef25343326fe1f9a0d081ab2eaa00a214fe0bd6420198d080c9f77`.
Host runner přijímá tento externí vzorek volbou `--l16-fixture PATH`.

Host testy zahrnují waveform všech signed hodnot LEP/L16, nulové intervaly,
WAV hysterezi a fázi na 44,1/48/96 kHz, legal/invalid RIFF, skutečně
generované NORMAL/IC/TC nahrávky, pět her v L16 i WAV, AUX, chybný checksum,
BASIC/modulovou závislost, falešné leadery, limit 257 kandidátů, cache,
rušení, změnu storage epochy a dlouhé nahrávky 1/10 MiB.

Test skutečného playeru a RMT proti simulovanému DMA porovnává hash celé
přehrané pásky, reset rozpracované půlvlny, prefill, čas, metadata, pauzy,
EOF, zrušení a bezpečné GPIO. GPTimer testuje i indexovaný WAV a časový
start na čtyřech frekvencích. OLED testy používají skutečný renderer a
ukládají šest [PBM snímků](validation/ui/tape-contents.pbm).

Host výsledky a target build nenahrazují fyzické ověření SD/RMT/Sharpu.
RTOS runtime high-water, skutečná VFS/storage latence a rychlost SD nebyly
na HW měřeny. Firmware nebyl nahrán, nebyl vytvořen commit ani větev.
