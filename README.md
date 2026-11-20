> Aktuální r40 (20. 11. 2026): LEP/L16 mají blokovou analýzu délky s procenty dokončení, bez časového limitu. ▶ přeskočí do PLAY s procenty, ■ zruší start; dokončená analýza zobrazí aktuální/celkový čas. Uplynulý čas odpovídá fyzicky odvysílanému signálu. Sedm host sad s ASan/UBSan i cílový build PASS. [Souhrn](docs/validation/edge-time-summary.json). Bez commitu a nahrání; HW ověření zbývá.

> Aktuální r33 (20. 11. 2026): [zjednodušený sidecar editor](docs/SIDECAR_EDITOR.md). Jeden řádek LOADER s rychlostí; SAVE CHANGES rovnou spouští bezpečnou transakci bez přehledu a CREATE/UPDATE dialogu. Potvrzení profilu zachováno. Tři host sady a cílový build PASS; HW ověření zbývá.

> Aktuální r31 (20. 11. 2026): [náhled názvu rekordu v editoru](docs/SIDECAR_EDITOR.md). Při listování se zobrazuje název kandidáta bez změny profilu, CHOOSE LOADER bez rychlosti. ABOUT bez spodní navigační nápovědy. Oprava zásobníku sidecar_find zachována; čtyři host sady a cílový build PASS, HW ověření zbývá.

> Aktuální r30 (20. 11. 2026): [jedno potvrzení profilu editoru](docs/SIDECAR_EDITOR.md). LOADER → SPEED → CONFIRM PROFILE; UL bez kroku rychlosti, SAVE nezávislý. INTERNAL i EXTERNAL CMT mají hlášku na 1,2 s. Tři host sady a cílový build PASS; QD/MOTOR návrat do browseru zatím v diagnostice, bez změny PLAY. HW ověření zbývá.

> Aktuální r29 (20. 11. 2026): [editor sidecarů a S1](docs/SIDECAR_EDITOR.md). MFI/M2I/MTI a nové QDI, potvrzení každého pole a samostatný SAVE, transakce s obnovou. S1 opět aktivní: LOW=EXTERNAL, HIGH=INTERNAL; EXTERNAL blokuje pouze PLAY a dovoluje RECORD i editor. HW ověření a nahrání zbývají. Následující starší záznamy jsou historie.

> Aktuální r28 (14. 11. 2026): [oprava předplnění WAV](docs/WAV_PREFILL_FIX.md). Reprodukováno předčasné RMT ukončení při tichém úvodu; před startem WAV nyní minimálně 256 intervalů vedle časové zásoby. Regrese 44,1/48/96 kHz, tři host sady a cílový build PASS; bez nahrání a HW ověření. Ikona RECORD a MIN z r27 zachovány. Následující starší záznamy jsou historie.

 Aktuální r15 (14. 11. 2026): [UL, UL700 a UL800 podle SD2CMT2](docs/UL_FAMILY_PERFORMANCE.md). Odstraněné 10ms čekání po každé krátké dávce UL, vypnutý detailní trace, účinné -O2. Všechny tři režimy prošly host přenosem 44 033 bajtů; Z80 přijímače LOW/HIGH a 100Hz plánování PASS, cílový build PASS. Uživatel potvrdil LOAD v r14 UL800 s nízkou rychlostí; r15 a UL700 zatím bez HW potvrzení. S1 bypass zůstává zapnutý. Následující starší záznamy jsou historie.

# MZCMTEmu

Port kazetového rozhraní Sharp MZ na ESP32-S3 N16R8: 16 MB flash, 8 MB octal PSRAM, SD SPI a SSD1306 128×64. Funkční vzor je [SD2CMT2-Reborn 2.0.3](https://github.com/bales0/MZ-SD2CMT2-Reborn/tree/600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb).

Implementované části prošly 24 hostitelskými sadami (Debian GCC14.2, ASan/UBSan) a cílovým sestavením. **Nový opravný build dosud nemá HW akceptaci:** uživatel hlásil selhání předchozího firmwaru na desce a uvedl COM6. Úspěšný LOAD, měření analyzátorem ani hodinový test SD/PSRAM tohoto obrazu nejsou doloženy. Rozsah důkazů a otevřené kontroly jsou v [matici parity](docs/PARITY_MATRIX.md) a [test reportu](docs/TEST_REPORT.md).

Aktuální oprava první QD navigace, dlouhých REC/PLAY a úplná kontrola GPIO: [PINOUT_REFERENCE_AUDIT](docs/PINOUT_REFERENCE_AUDIT.md). Předchozí opravu inicializace RMT dokumentuje [PLAY_DIAGNOSTICS_FIX](docs/PLAY_DIAGNOSTICS_FIX.md). MOTOR ON je podle uživatele HIGH; nový obraz je nutné ověřit na desce.

## Funkce

- PLAY: MZF/MZT/M12, read-only QuickDisk MZQ/QDF/QD, WAV, LEP/L16 a ZX TAP 1400/1800/2100/2500 Bd. Sharp má všech 17 režimů, původní IC/TC/FAST3/UL loadery, MFI/M2I/MTI a AUTO s bezpečným fallbackem.
- MZT/QD selektor: titul, rekord, individuální profil; návaznost MZT podle reference včetně kombinací UL a běžných rekordů. Po UL→UL se přenos ukončí. STOP za běhu kontejneru vrací selektor, také u jediného rekordu. Po opětovném vložení SD se přerušený kontejner znovu validuje a může obnovit selektor; vyžaduje nové PLAY.
- RECORD: WAV 22,05/44,1 kHz, LEP, L16 a validovaný MZF. Nezávislé MOTOR/MANUAL/AUTO řízení, uživatelská pauza, AutoName a detekce Sharp/IC/TC/Sinclair/CP/M. MZF kontroluje checksum a normalizuje kopírovací loader; neúplný záznam smaže.
- Dokončení záznamu běží na úloze SD se stavem SAVING RECORD. AutoName používá _01…_99 při kolizi, nikdy nepřepíše existující soubor. MFI vzniká pod výsledným názvem; chybu metadat OLED ohlásí samostatně.
- Browser: řazení podle názvu/přípony, scroll, návrat k rodiči, skryté sidecary, zrušitelná analýza. Jednorázový PSRAM index nejvýše 65535 položek a 2 MiB arena jmen; překročení limitu je explicitní chyba. Cesty mají limit 1024 B.
- Nabídka: rodina loaderu a zapamatovaná rychlost každé rodiny, TAP speed, inverze, PLAY MOTOR, RECORD formát/řízení/AutoName, jas OLED, stav SD/retry, ABOUT a KEYMAP. NVS schéma v2 má CRC a migraci v1; změněné nastavení se ukládá při návratu z menu.

## Grafické UI a metadata

Browser má šest inverzně označených řádků a marquee od změny výběru. PLAY/RECORD zobrazují titul, typ, přenosový a rozpoznaný režim, velikost, LOAD/EXEC (ZX BASIC: AUTO LINE), čas a oddělené ukazatele průběhu a bufferu. Metadata mají provenienci a ověřovací stav; neznámá pole jsou --. TC čeká na validovaný první loader payload, IC používá kanonickou hlavičku; generovaný transportní loader nenahrazuje původní program. WAV/LEP/L16 používají pasivní decoder nad PLAY intervaly, TAP samostatný ZX/XOR parser. Název nahrávky zůstává oddělený od titulu pozdějšího programu.

RX ukazuje volné FIFO, minimum a sticky chybu: pokles okamžitě, zotavení pomalu. TX ukazuje přibližnou časovou rezervu včetně rozpracovaného RMT intervalu a zvlášť SW FIFO. UL/gap nevyvolávají falešný alarm. Prioritní overlay má vlastní životnost; STOP potvrzuje latched chybu. Neúspěšná navigace obnovuje původní seznam a výběr, CANCEL nevyvolává reinicializaci SD.

Volitelný CONFIG_MZ_RX_PCNT_GUARD je standardně vypnutý. Po zapnutí porovnává počty obou hran GPIO5 s MCPWM a rozlišuje potvrzenou ztrátu, nejistotu na hranici epochy a nespolehlivý čítač. Neprokazuje správnost timestampů ani absolutně bezeztrátový capture. [Ověření a omezení](docs/TEST_REPORT.md), [OLED náhledy](docs/validation/ui/record.png).

## Transport a paměť

Výchozí PLAY používá jeden kontinuální RMT DMA přenos s rozlišením 16 MHz. Sharp/TAP ticks zůstávají přesné, WAV vychází z absolutní sample osy; LEP/L16 používají uložené šířky. RX používá hardwarově zachycené MCPWM timestampy. ISR neprovádí FS, alokace ani I²C.

Jediná úloha storage vlastní SD deskriptory, adresáře a mount/unmount. Pevný pool osmi souborů má 1 KiB cache; opravy placeholderů uvnitř cache nevyvolávají seek na SD. Nativní souborová cesta nealokuje stdio FILE/buffery. RECORD finalize je asynchronní. PSRAM browser, pool rekordových profilů a MZF payload se alokují jednou při inicializaci.

RMT pauza pozastavuje zdrojové hodiny celé RMT skupiny; CMT musí být jejím jediným uživatelem. Retenci rozpracovaného symbolu je nutné ověřit na ESP32-S3. Diagnostické alternativy CONFIG_MZ_TX_GPTIMER_FALLBACK a CONFIG_MZ_CAPTURE_GPIO_FALLBACK jsou ve výchozím sestavení vypnuté. [Časování a footprint](docs/TIMING_PORT.md).

## Sestavení a testy

PlatformIO, pinovaná platforma espressif32@7.0.1, ESP-IDF 6.0.1:

    pio run -e MZCMTEmu -j 1
    python tests/host/run_tests.py --cc gcc

Volitelně přidejte --tap-fixtures tests/TAP --qd-fixtures tests/QD --qd-fixtures tests/MZQ. Sanitizery vyžadují jejich podporu hostitelským kompilátorem; v Debianu s GCC14.2 prošly; na MinGW5.1 dostupné nejsou. Testy vytvářejí dočasné výstupy v .tmp a uživatelské fixture pouze čtou.

N16R8 override nastavuje image/upload header i sdkconfig na 16 MB a octal PSRAM. PlatformIO úvodní popis generické desky stále uvádí N8; rozhoduje konfigurace a hlavička výsledného obrazu. App partition má 1 MB. [Výsledky sestavení](docs/validation/ui-metadata-build.txt).

## Ovládání pěti tlačítky

| Kontext | PLAY krátce / dlouze | REC krátce / dlouze | STOP krátce / dlouze | NEXT / PREV |
|---|---|---|---|---|
| Filebrowser | Otevřít/start / PLAY menu | Připravit RECORD / RECORD menu | Rodič / systémové nastavení | Výběr + repeat |
| MZT/QD selektor | Spustit rekord / bez akce | Profil rekordu / bez akce | Browser / ukončit | Rekord |
| PLAY | Pauza/pokračování / bez akce | Bez akce | Stop, kontejner → selektor / ukončit | Browser na pozadí bez seek |
| RECORD | Pauza/pokračování / bez akce | Bez akce | Uložit / zrušit a smazat | Bez akce |
| SAVING | Bez akce | Bez akce | Vyčkat / smazat po uzavření | Bez akce |
| Každé menu | Změnit volbu/info / bez akce | Bez akce | Uložit a browser (v info o úroveň zpět) | Volba; v KEYMAP stránka |

Odrušení 30 ms; STOP long 800 ms; repeat po 400 ms, dále po 120 ms. STOP má prioritu v kombinaci tlačítek a blokuje opakování držené navigační klávesy do další obrazovky. Krátké PLAY/REC/STOP se vyhodnotí při uvolnění. PLAY/REC long700 ms, STOP long800 ms; u dlouhého REC proto nevznikne předčasný start záznamu. Záznamy směřují do /RECORDINGS.

RECORD nastavení: ve filebrowseru podržet REC (700 ms). TYPE, CTRL a AUTONAME mění krátký PLAY; STOP uloží a vrátí přímo do browseru. RECORD položky v PLAY menu nejsou. V QD selektoru nejprve STOP do browseru. Dlouhé REC při chybějící kartě vyžádá SD retry. První otevření QD obraz validuje, první návštěvy dalších rekordů čtou jen hlavičku/prefix; výpočet úplné délky se odkládá na skutečné PLAY. Sériová diagnostika: `pio device monitor --port COM6 --baud 115200`.

S1 fyzicky přepíná CMT trasu. EXT (GPIO21 LOW, debounce 40 ms) zablokuje PLAY/REC, zastaví PLAY a zruší rozpracovaný RECORD včetně SAVING; návrat na INTERNAL nic automaticky nespouští, software nevytváří mux. Uživatel potvrdil neinvertující převodníky U1/U2; elektrické parametry a resetové stavy stále vyžadují měření. [Pinmap a HW kontroly](docs/HARDWARE.md).

Přenesené algoritmy, Z80 loadery a znakové tabulky zachovávají GPLv3 atribuci; AVR periferie se nepoužívají. [Licence](LICENSE), [inventura zdrojů](docs/SOURCE_INVENTORY.md), [předání](HANDOFF.md).
