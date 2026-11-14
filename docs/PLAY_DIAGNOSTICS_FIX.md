> Historický stav před opravou referenčního ovládání: RECORD SETUP v kořenovém menu a výpočet délky při první návštěvě byly nahrazeny samostatným dlouhým REC a odloženým výpočtem při PLAY. Aktuálně viz [PINOUT_REFERENCE_AUDIT](PINOUT_REFERENCE_AUDIT.md).

# Oprava spuštění PLAY a diagnostika — 13. 11. 2026

Výchozí main f5cda0fa04566e4008ef40ab0c9c49480316ddac. Uživatel na desce hlásil MOTOR OFF, okamžitý PLAY error, pomalé opakované QD analýzy a chybějící RECORD menu. Změny jsou pracovní, bez stagingu/commitu/větve/push. Uživatelské změny .gitignore a odstranění minulého zadání zůstávají zachované.

## Nalezené příčiny a výsledné chování

- RMT TX vznikalo s gpio_num=-1. Skutečné ESP-IDF6.0.1 esp_driver_rmt/src/rmt_tx.c kontroluje GPIO_IS_VALID_OUTPUT_GPIO a vrátí ESP_ERR_INVALID_ARG už při vytvoření kanálu. Host mock tuto podmínku dříve nekontroloval. Kanál nyní vzniká s platným GPIO4, po vytvoření se pin vrátí na safe idle; před PLAY se znovu připojí RMT a STOP jej předá GPIO/UL. Test vyžaduje stejný platný pin jako SDK a kontroluje chyby channel/encoder/callback i reset chyby po úspěšném retry.
- Každý selector select uzavíral Sharp handle; další položka znovu parsovala a validovala celý QD a počítala průběh. READY nyní drží jediný validovaný read-only handle/index a připravený kodek. Změna rekordu používá již ověřený index, osm omezených duration položek šetří návraty k navštíveným rekordům/profilům. READY → PLAY startuje tentýž připravený zdroj. STOP, chyba či vyjmutí SD session i cache ruší; žádná cache přes remount. První otevření obrazu stále plně validuje formát/CRC a může trvat podle SD/velikosti. První návštěva jiného rekordu může dopočítat jeho délku. Dry-run násobí shodné leader/mark cykly místo vytváření každé půlperiody; přesné golden waveform testy všech17 režimů zůstávají kontrolou součtu a časů.
- PLAY z READY vybírá cestu připraveného kontejneru, nikoli případně posunutý browser. player_start bezpečně kopíruje i cestu, která odkazuje na jeho vlastní current_path. WAIT MOTOR nad READY zachovává jeho titul/metadatový kontext.
- RECORD SETUP je samostatná položka hlavního menu a samostatná stránka s FORMAT, CTRL (MOTOR/MANUAL/AUTO) a AUTONAME. REC v otevřeném menu je přímá zkratka. STOP vrací z RECORD SETUP do hlavního menu, další STOP uloží nastavení a zavře menu. PLAY CTRL je výslovně MOTOR nebo MANUAL, aby se nepletlo s nezávislým RECORD AUTO.

## MOTOR

**MOTOR ON = HIGH**, dle poslední opravy uživatele. GPIO6 je pouze vstup s pull-up; žádná hardwarová nebo firmware inverze nebyla přidána. PLAY MANUAL používá interně povolený motor bez ohledu na GPIO6, RECORD má vlastní volbu řízení. Příčinu případného zbývajícího MOTOR OFF na desce musí rozlišit nový výpis skutečné úrovně a nastavení, ne odhad polarity. Předchozí zpráva LOW byla uživatelem opravena na HIGH.

## Sériový monitor COM6

    pio device monitor --port COM6 --baud 115200

USB Serial/JTAG konzole zůstává na původních USB pinech. Nový boot banner začíná MZCMTEmu PLAY diagnostics 2026-11-13 a uvádí IDF a HIGH polaritu. printf není závislé na vypnutém SDK log level. Záznamy obsahují:

- settings: obnovené PLAY_CTRL/REC_CTRL/loader/formát a load_error;
- diag: MOTOR6=0/OFF nebo1/ON, PLAY_CTRL, WAIT, S1_21/EXT, SD, PLAY/PAUSE/REC stavy;
- diag: TX running/paused/FIFO/reserve/underrun, konkrétní error a internal heap/min;
- player: požadovanou cestu a profil, výsledek selectu a elapsed_ms, source_reused/duration_cached;
- tx: konkrétní operaci RMT CHANNEL/ENCODER/CALLBACK/GPIO/ENABLE/TRANSMIT/DISABLE a číselný SDK návratový kód.

Diagnostika běží ve foreground, nikdy ISR; heartbeat po2 s za transportu/čekání a10 s v idle, změnové snapshoty omezené na nejvýše10/s. Tlačítka se vypisují při události. Chyba PLAY se zobrazí konkrétně také na OLED. Při chybě inicializace se původní příčina nezamění za NO PULSES.

Krátký12s odposlech COM6 bez resetu se podařilo otevřít, ze stávajícího firmwaru nepřišel text. Tento zásah desku neflashoval; nové diagnostické zprávy budou dostupné po nahrání nového buildu. Pro další reprodukci zachovat výpis od startu přes volbu PLAY CTRL, otevření MZF/QD, výběr rekordu a PLAY. Neobsahuje dump dat nahrávky.

## Ověření a omezení

Nový test_menu kontroluje dosažitelnost RECORD SETUP, jeho tři položky, návrat, wrap a prázdné řádky. test_player_sharp navíc ověřuje dvourekordový MZQ, opakované přepnutí s cache, READY → PLAY bez analýzy, invalidaci po STOP, stejnou celkovou délku a bezpečnou aliasovanou cestu. test_raw_rmt kontroluje skutečnou SDK podmínku GPIO4 a konkrétní chyby inicializace. Plné výsledky24 sad/ASan/UBSan, target build a footprint se ukládají do validation/play-fix-*.

Host testy a úspěšný build neopravňují tvrdit, že Sharp LOAD/SAVE na desce prošel. Zůstává skutečný test nového obrazu na COM6, kontrola MOTOR úrovní a všech RMT/MCPWM/SD časů za zátěže. Dřívější UI/PCNT logy zůstávají historickým dokladem předchozí verze.

## Finální výsledek

**24 sad PASS s ASan/UBSan (111,17 s), target ESP-IDF6.0.1 build PASS.** Log [host](validation/play-fix-host-linux.txt), [build](validation/play-fix-build.txt), [image_info](validation/play-fix-image.txt), [symboly](validation/play-fix-symbols.txt), [souhrn/hash](validation/play-fix-summary.json). Statická RAM155548 B/327680 B, aplikace464553 B/1048576 B, image464976 B; proti předchozímu zdokumentovanému buildu +1200 B RAM a +3040 B aplikace. Nový pevný open_path1024 B a8malých duration položek, bez per-record heap alokací a bez druhého indexu či FIFO. Všech92 fixture má původní SHA256. Skutečná rychlost SD, runtime heap/stack minima a nové chování na MZ nejsou tímto host výsledkem změřené.

Firmware: .pio/build/MZCMTEmu/firmware.bin, SHA256 9e6a57c4b6b6d0510884f1799e5c64a23bdb699135a72084d3f59960489cdb5e. Žádný flash, staging, commit, větev, push ani PR v tomto zásahu.
