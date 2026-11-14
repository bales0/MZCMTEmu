# r21 – značka sidecar, kompaktní buffer a grafická animace

14. 11. 2026. Provedeno po výslovném schválení všech pěti bodů uživatelem. Navazuje na QD STOP a hlavičku [r20](QD_STOP_HEADER_FIX.md).

## Sidecar

V pravém počítadle browseru i transportu se při existenci odpovídajícího sidecaru mění oddělovač / na I, například 2I15 nebo 2I3. Je to značka přítomnosti podle referenčního SD2CMT2 (browser.cpp, browser_current_has_info_sidecar a browser_build_path_line; play_screen.cpp, build_play_line0), nikoli potvrzení, že se profil právě používá. Platí tedy také v ručním režimu a u existujícího souboru s neplatným obsahem.

Rozpoznává se MFI pro MZF, M2I pro M12 a MTI pro MZT. QD/MZQ/QDF sidecar nedefinuje ani kontrolovaná reference a značku I proto nedostává. sidecar_exists() kontroluje běžný soubor přes stat, nikoli rozbor jeho obsahu. Stejně jako čtečka zkouší velkou a malou příponu. Existenci aplikace ukládá pro vybranou cestu; při obnovení adresářového indexu ji znovu ověří. Neprovádí se nové SD hledání při každém překreslení OLED.

Společná tvorba názvu sidecaru byla sjednocena s dosavadní čtečkou. Po této změně znovu prošlo všech 16 referenčních textů jako MFI, M2I i MTI záznam 2, celkem [48 vektorů](validation/ui-sidecar-reference-audit.txt). Pravidla AUTO/ruční volby, fallback a zapisovač RECORD nejsou změněny.

## Buffer a průběh

Spodní celoplošný bufferový pruh je odstraněný. Při PLAY se vedle stavu zobrazuje BUF nn%, což je obsazení softwarového TX FIFO. Při UL bez používaného FIFO se zobrazuje BUF --. Časová rezerva TX v ms a hlášení LOW/CRIT/BAD zůstávají na samostatném textovém řádku; u časově platného TX vycházejí z časové rezervy, nikoli z počtu položek FIFO.

RECORD dál ukazuje RX FREE a MIN a případnou výstrahu; pouze nemá původní spodní velký graf. Jeho ukazatel dat D a procenta zůstávají zachované. Pro schválený průběh PLAY je použitý pruh přes celou šířku 128 px, bez P a bez číselného údaje procent. Nedochází ke změně výpočtu postupu ani samotného přenosu.

## Ikony a načítání

Původní PLAY šipka zasahovala do y=9, zatímco PAUSE končila v y=7. Nové obě ikony zabírají y=1 až 7 a mají společný střed y=4. Nezasahují do oddělovací čáry pod hlavičkou.

Původní font nemá glyfy pro | a zpětné lomítko používané při ANALYZING, takže dvě fáze působily prázdně. Nový režim ANALYSIS kreslí osm bodů kolem středu a otáčí zvýrazněný bod v osmi grafických fázích, nezávisle na fontu. Fáze se mění při dosavadním 100ms preflight refreshi; kvůli animaci se nezavádí další čekání. STOP pro zrušení přípravy a původní validační logika zůstávají.

[Vizuální náhledy](validation/ui-sidecar-spinner-preview.png) zobrazují značku I, kompaktní BUF, PLAY/RECORD a zarovnané ikony. [Všech osm fází](validation/ui-analysis-phases-preview.png) bylo prohlédnuto a žádná nechybí.

## Ověření a obraz

[Sedm host sad](validation/ui-sidecar-spinner-host.txt), GCC14.2 ASan/UBSan PASS: test_ui_policy, test_oled_ui, test_sharp_stream, test_player_sharp, test_player_rmt, test_player_tap a test_file_list. Nové kontroly pokrývají přítomnost neplatného i validního sidecaru, malou příponu, nepodporované formáty, I na přesných souřadnicích, BUF 73%, chybějící spodní graf, plnou šířku průběhu bez popisků, shodné meze PLAY/PAUSE a osm neprázdných odlišných snímků animace. Změna revize adresářového indexu je ověřená při reloadu; browser dál prochází 10 000 soubory a zachovává dva alokované PSRAM bloky. QD STOP/replay a automatická návaznost z předchozí opravy nadále procházejí.

[Cílové sestavení](validation/ui-sidecar-spinner-build.txt) PASS. RAM 156 300 B, aplikace 461 247 B. Obraz .pio/build/MZCMTEmu/firmware.bin má 461664 B; SHA256 ea277eb2c1f6470002a7c36abe29bb2229be9cc3816d5aedc9d6b070088e3dfd.

Boot banner: MZCMTEmu TX-TRACE r21 UI SIDECAR SPINNER S1 BYPASS INTERNAL 2026-11-14.

Nový obraz není nahraný ani ověřený na fyzickém OLED. GPIO, polarity, UL handshake a S1 bypass se nemění. Bez stagingu, commitu, větve a push.
