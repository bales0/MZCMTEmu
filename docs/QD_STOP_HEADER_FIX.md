# r20 – QD STOP bez opakované analýzy a nová hlavička

14. 11. 2026. Uživatel schválil čtyři úpravy: zachovat QD index i po PLAY → STOP, přesunout počítadlo doprava, zobrazovat otevřený QD soubor v horním řádku a zkracovat cesty pomocí ../ (nikoli /../).

## PLAY → STOP

r19 zachovával QD relaci po přirozeném dokončení, ale obsluha tlačítka STOP stále zavřela zdroj a znovu volala player_prepare_record(). Tím se zneplatnil index a spustila další analýza.

První STOP během QD PLAY/PAUSE nyní přes player_stop_to_selector() pod zámkem playeru zastaví RMT, uvolní případné UL vlastnictví GPIO a vrátí selektor téhož záznamu. READ je LOW, SENSE HIGH. Otevřený obraz, jeho validovaný index, cache délky i individuální profil zůstávají zachované. Návrat a nové PLAY stejného záznamu nevytvářejí novou analýzu. Ošetřen je i souběh STOP s přechodem do READY na konci přenosu.

Druhý STOP z již otevřeného selektoru obraz opouští a index zahodí. Stejně jej dál zneplatňuje chyba, odebrání SD, reinitializace nebo změna zdroje. MZT používá dosavadní STOP cestu. První otevření obrazu nebo nové otevření po jeho opuštění může analýzu potřebovat. Automatická QD návaznost z r19 zůstává.

## Hlavička a cesta

Počítadlo aktuální/celkový počet je v browseru i QD zobrazení zarovnané doprava k poslednímu sloupci. Browser vlevo zobrazuje adresářovou cestu; QD vlevo název otevřeného obrazu, například MULTI.QD, a vpravo 2/3. Název programu uvnitř obrazu zůstává na samostatném řádku. Název QD se zobrazuje v selektoru, během PLAY i pauzy. Stav SELECT RECORD/PLAY/PAUSE/WAIT je v QD zobrazení zachován nad spodním pruhem, případně spolu s časovou rezervou TX.

Zkrácení cesty používá ../. Přednost má celý konec cesty od hranice adresáře, například ../action/SHARP. Je-li samotný poslední název delší než dostupné pole, zachovává se jeho konec. Krátké cesty se nezkracují. Browser zachovává sedm řádků a centrování výběru z r19. [Náhledy](validation/qd-stop-header-preview.png) byly vizuálně zkontrolovány: browser, dlouhá cesta, QD selektor, PLAY, PAUSE a poslední položka.

## Ověření

[Pět host sad](validation/qd-stop-header-host.txt), GCC14.2 ASan/UBSan PASS: test_ui_policy, test_oled_ui, test_player_sharp, test_player_rmt a test_player_tap.

Regrese používají skutečný player, GPTimer/RMT backendy a QD zdroj se záznamy po 256 B, aby STOP skutečně zasáhl přenos a ne až konec krátkého UL payloadu. Ověřují STOP během hlavičky, pauzy, aktivních UL dat a pozastavených UL dat, žádný nový callback analýzy při STOP/replay, zachování rekordů a profilů, uvolnění GPIO a invalidaci při opuštění selektoru/odebrání SD. Nadále procházejí automatické návaznosti tří QD záznamů, všechny Sharp režimy, UL chyby a WAV/TAP transport.

OLED testy porovnávají text na konkrétních souřadnicích: pravé počítadlo browseru, ../ zkrácení i QD název a index během SELECT/PLAY/PAUSE. Samostatné testy ořezu pokrývají hranice adresářů, dlouhý poslední název, úzké pole, prázdný vstup a velikost bufferu.

[Cílové sestavení](validation/qd-stop-header-build.txt) PASS. RAM 155 292 B, aplikace 460 735 B. Obraz .pio/build/MZCMTEmu/firmware.bin: 461152 B, SHA256 f984488199636669166a7c77ac3cf9328d1e88c9b0b2d63542c9269117385cd5.

Boot banner: MZCMTEmu TX-TRACE r20 QD STOP HEADER S1 BYPASS INTERNAL 2026-11-14.

Nový obraz nebyl nahrán ani ověřen na Sharpu. Polarity, S1 bypass a sidecar parser/zapisovač se nemění. Žádný staging, commit, větev ani push nebyl proveden.
