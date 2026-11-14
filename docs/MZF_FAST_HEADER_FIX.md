# r22 – rychlý start MZF, stejný font indexu a společná patička

14. 11. 2026. Uživatel schválil sjednocení fontu indexu s adresářovou cestou a start MZF bez úplného předvýpočtu waveformu. Během práce navíc výslovně schválil odstranění opakovaného PLAY na druhém řádku odspodu a umístění TX vlevo / BUF vpravo na společném spodním řádku.

## Start MZF a průběh

Produkční MZF PLAY nyní používá sharp_stream_preview(): ověří soubor, rozsah deklarovaných dat, hlavičku, případný sidecar a pravidla loaderu. Pro rozpoznání metadat čte nejvýše dosavadní 90B prefix payloadu. Neprovádí úplné předgenerování všech impulzů kvůli délce. Zvolený loader i samotný waveform zůstávají stejné. Samostatná měřicí cesta sharp_stream_open() zůstává dostupná pro ostatní formáty a codec validaci; pro MZF ji produkční PLAY nepoužívá.

U rychlého MZF není předem znám přesný celkový čas. Průběh odpovídá dokončeným zdrojovým datovým bajtům. Codec označí poslední fyzickou půlperiodu stop bitu datového bajtu; stejný příznak jako u ostatních zdrojů přenese do FIFO. Čítač GPTimer/RMT se zvýší až po dokončení odpovídajícího intervalu, nikoli při čtení souboru nebo kódování dopředu. Hlavičky, náběhy, generovaný loader a checksumy neposouvají datový průběh. Při pauze a STOP se průběh zastaví. Při běžném přehrávání je omezen na 99 %, dokud není dosaženo skutečné EOF; potom je 100 %.

Počítání se zachovává přes zastavení/reset RMT mezi bloky. U nativního MZF zahrnuje také odesílaný přílepek za deklarovaným payloadem; u prázdného payloadu je před dokončením 0 a při EOF 100. Zobrazený uplynulý čas dál vychází z dokončených intervalů. UL používá dosavadní vlastní počet potvrzených bajtů. MZT a QD ponechávají dosavadní měření a návaznost.

ANALYZING se tedy nevyvolává přípravou MZF PLAY. První načtení QD/adresáře nebo jiné existující přípravné operace tím nejsou odstraněny. Chybná hlavička nebo deklarovaný přesah souboru se odmítají před výstupem; případné pozdější I/O chyby se řeší při streamování.

## Font a patička

Browser používá pro cestu i pravé počítadlo shodný font 6×8 na stejném řádku. Pro index je vyhrazená jeho skutečná šířka, cesta se do zbývajícího místa zkracuje pomocí ../. QD hlavička už měla název i počítadlo shodným menším fontem a toto zarovnání zachovává. Značka sidecar I zůstává.

PLAY se neopakuje na druhém řádku odspodu. Stav je patrný z horní ikony; jiný stav jako PAUSE USER, WAIT MOTOR, SELECT RECORD či chyba zůstává textově čitelný. Spodní řádek sdílí TX vlevo a BUF % vpravo. Výstrahy časové rezervy mají vlastní místo, aby nepřepisovaly BUF. RECORD se touto změnou patičky nemění.

[Vizuálně zkontrolované náhledy](validation/mzf-fast-header-preview.png) ukazují shodný font, značku I, dlouhou cestu, PLAY bez opakovaného textu, společnou patičku a pauzu.

## Ověření

[Pět host sad](validation/mzf-fast-header-host.txt), GCC14.2 ASan/UBSan PASS: test_oled_ui, test_player_sharp, test_player_rmt, test_sharp_stream a test_player_tap. Dodatečné cílené kontroly RMT a patičky také PASS.

- U všech 17 MZF režimů je při skutečné přípravě playeru nula volání předanalýzy; výsledná aktivní délka waveformu odpovídá samostatně měřené codec cestě. Existující golden vektory nadále ověřují profily a délky jednotlivých impulzů.
- Velký 16KiB MZF na GPTimer a MZF s 1024B payloadem + 256B přílepkem na RMT ověřují datový průběh, nulový vliv fronty/hlavičky, přenos přes meziblokový reset, pozastavení a zmrazení po STOP. Prázdný MZF se dokončí bez dělení nulou, neúplný soubor se odmítne před výstupem.
- OLED test porovnává index jako font 6×8 na konkrétní pozici, TX a BUF na spodním řádku a prázdný druhý řádek odspodu při běžném PLAY. Dosavadní testy QD STOP/indexu, sidecar, pauzy, UL a WAV/TAP dále procházejí.

Jde o testy s modelovaným HW časem, nikoli měření hran na konektoru Sharpu.

[Cílové sestavení](validation/mzf-fast-header-build.txt) PASS. RAM 156 300 B, aplikace 461 863 B. Obraz .pio/build/MZCMTEmu/firmware.bin má 462288 B, SHA256 4e7ebcf83bdbef466d9cb66a6ea3aaeec6af7d51695ad30fdb9ea8aed42ccf88.

Boot banner: MZCMTEmu TX-TRACE r22 MZF FAST HEADER S1 BYPASS INTERNAL 2026-11-14.

Nový obraz není nahraný ani ověřený na HW. Polarity a S1 bypass se nemění. Bez stagingu, commitu, nové větve a push.
