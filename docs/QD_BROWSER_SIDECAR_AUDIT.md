> Aktuální doplnění [r20](QD_STOP_HEADER_FIX.md) zachovává index také při transportním STOP. Počítadlo je nově vpravo a cesty používají ../. Níže je původní záznam r19; audit sidecar zůstává platný.

# QD návaznost, browser a kontrola sidecar – r19

14. 11. 2026. Uživatel potvrdil odstranění HW závad včetně poškozeného zesilovače. Schválil tři konkrétní změny: automatické pokračování QD bez zbytečné analýzy, počítadlo vlevo v hlavičce a sedmiřádkové listování s výběrem uprostřed. Následně požádal o kontrolu sidecar proti SD2CMT2. Změny sidecar parseru/zapisovače nebyly schválené ani provedené.

Výchozí HEAD: b76a9e017092e187a9b351a7d60ef4ee4de5d11b. Reference MZ-SD2CMT2-Reborn 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb byla pouze čtená.

## QD

V kontrolované referenci automaticky pokračuje MZT; QD po vybraném záznamu končí a vrací selektor (mzf_playback.cpp, mzf_advance_after_boundary; play_controller.cpp, play_controller_service). Automatické pokračování QD je tedy uživatelem schválené rozšíření oproti této verzi SD2CMT2.

Nativní pokračování nyní přijímá MZT i QD. U QD vybere další hlavičku a payload z již validovaného indexu quickdisk_select(), bez zavření obrazu nebo nového quickdisk_open(). Zachovává individuální profily rekordů a obnovuje metadata pro aktuální záznam. Běžné meziblokové čekání používá stejná pravidla jako dosavadní MZT; ruční pauza a MOTOR řízení zůstávají součástí transportu. U QD lze navázat i UL → UL, zatímco původní omezení MZT pro tuto kombinaci zůstává.

Po posledním QD záznamu se bezpečně ukončí TX a zobrazí selektor posledního záznamu. Index a otevřený zdroj zůstávají platné pro tuto relaci, takže návrat nevyvolává ANALYZING. Seznam se automaticky nepřetáčí na první soubor. Explicitní STOP, chyba, změna zdroje nebo odebrání SD relaci dál ukončují a index zneplatní. První otevření a příprava dosud nepřipraveného PLAY mohou analýzu potřebovat; odstraněna je zbytečná analýza během automatické návaznosti a návratu po dokončení.

## Browser

Počítadlo aktuální/celkový počet je vlevo na prvním řádku, napravo cesta. U dlouhé cesty se zobrazuje konec s výpustkou. Počet zachovává původní význam položek browseru včetně adresářů a případného ..; nemění se filtr ani řazení souborů. Údaj EXTERNAL je při externí trase zkrácen na E vpravo v hlavičce.

Odstraněná patička uvolnila sedmý řádek položek. Výběr se drží na čtvrtém řádku. U začátku a konce seznamu je okno omezené na platný rozsah a výběr dojde až na první nebo sedmý řádek. Stejné pravidlo platí v obou směrech, při návratu do adresáře i po obnovení výběru. Dosavadní přetočení NEXT/PREV přes konec zůstává. [Vizuálně zkontrolované náhledy](validation/qd-browser-preview.png) ukazují běžný seznam, střed, konec a dlouhou cestu.

## Sidecar proti SD2CMT2

| Zdroj | Sidecar | Stav čtení |
|---|---|---|
| MZF | MFI | Podporováno v AUTO |
| M12 | M2I | Podporováno v AUTO |
| MZT | MTI, sekce RECORD=1 až 65535 | Podporováno po jednotlivých záznamech v AUTO |
| QD/MZQ/QDF | žádný definovaný | Nepodporuje ani kontrolovaná reference |

Přehrávání podporuje všech 16 konkrétních profilů NORMAL, IC, TC, MZ700, UL, UL_MZ800 a UL_MZ700 v povolených rychlostech. Ruční profil má přednost před sidecarem. Chybějící/neplatný profil v AUTO vede na NORMAL 1:1. Hodnoty TYPE jsou bez ohledu na velikost písmen, klíče TYPE/SPEED/RECORD mají stejně jako v referenci přesný zápis. Staré soubory MZI se nepoužívají. Sidecary nejsou v browseru mezi přehratelnými soubory.

[Samostatný audit](validation/sidecar-reference-audit.txt) vygeneroval vstupy přímo ze všech 16 skutečných textů v referenčním mzi_sidecar.cpp. Produkční parser přečetl každou variantu jako MFI, M2I a MTI záznam 2: 48 vektorů PASS s ASan/UBSan. Navíc ověřena ruční priorita a odmítnutí QD sidecar. [Přesné referenční vektory](validation/sidecar-reference-vectors.json). Stávající test_sharp_stream ověřuje také chybné MTI hranice, fallback a ignorování MZI.

Zjištěné rozdíly v zápisu, ponechané beze změny: sidecar_write_mzf() umí 12 RECORD profilů NORMAL/IC/TC/MZ700 1:1; neumí zapsat UL, UL_MZ800, UL_MZ700 a MZ700 1:3, které referenční univerzální zapisovač umí. Jejich čtení při PLAY funguje. Nativní zapisovač chrání existující MFI před přepsáním; reference svůj MFI zapisovač otevírá s přepsáním. Rozšíření nebo změna těchto pravidel vyžaduje nový souhlas uživatele.

## Ověření a obraz

[Sedm host sad](validation/qd-browser-host.txt), GCC14.2 ASan/UBSan, PASS: test_oled_ui, test_quickdisk, test_player_sharp, test_sharp_stream, test_player_rmt, test_file_list, test_player_tap. QD testy ověřují tři navazující záznamy, individuální profily včetně UL → UL, žádné další volání analýzy mezi nimi, návrat na poslední záznam bez smyčky a zachování metadata. Integrace skutečného playeru a RMT encoderu navíc ověřuje součet fyzických délek přenášených waveformů a pauzu. QuickDisk parser dál prochází testy logického MZQ, QDF CRC, HxC všech 16 MFM fází a FlashFloppy. Browser prošel prázdným/krátkým seznamem, oběma směry, hranami, přetočením i adresářem s 10 000 soubory. OLED náhledy byly vizuálně prohlédnuty.

[Cílové sestavení](validation/qd-browser-build.txt) PASS: RAM 155 292 B, aplikace 460 407 B. Obraz .pio/build/MZCMTEmu/firmware.bin: 460832 B; SHA256 d5cf6bb51109050fef83cf681119b14ec08800a9368fc4bf5ba59cef53d5cbf9.

Boot banner: MZCMTEmu TX-TRACE r19 QD AUTO BROWSER7 S1 BYPASS INTERNAL 2026-11-14.

Skutečné přehrání více QD programů novým obrazem na Sharpu ještě není potvrzené. S1 bypass a polarity signálů nebyly změněny. Žádný flash, staging, commit, větev ani push nebyl proveden.
