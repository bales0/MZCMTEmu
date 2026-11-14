# Audit polarit rozhraní CMT – 13. 11. 2026

## Výsledek

Uživatelem potvrzené U1/U2 jsou neinvertující. Logická hodnota portu 8255 není u datových vodičů totožná s úrovní na kazetovém konektoru. Plošná inverze GPIO by aplikovala inverzi podruhé. SENSE je na konektoru aktivní LOW; MOTOR je aktivní HIGH.

| Signál | GPIO / směr vůči emulátoru | Úroveň na konektoru a vztah k MZ-800 | Stav implementace |
| --- | --- | --- | --- |
| READ | 4 / výstup | Vstup do MZ jde přes invertující LS14 na PC5. Fyzické LOW odpovídá logickému HIGH PC5. | Sharp generátor již vydává LOW po dobu high_ticks a HIGH po dobu low_ticks. Uživatelské INVERT mění pouze datovou vlnu v transportu. |
| WRITE | 5 / vstup | PC1 jde přes invertující LS04 na konektor. Fyzické LOW odpovídá HIGH PC1. | Capture zachovává fyzickou úroveň; dekodér vyhodnocuje intervaly LOW. UL přijímá fyzickou úroveň stejně jako referenční AVR; další inverze se nepřidává. |
| MOTOR | 6 / vstup | HIGH = motor ON. Je výstupem motorového obvodu Sharpu, nikoli přímou kopií PC3. | HIGH = ON podle opravy uživatele i servisního manuálu. |
| SENSE | 7 / výstup | LOW = stisknuté tlačítko kazetového mechanismu, HIGH = neaktivní. PC4 je součástí motorového/SENSE obvodu Sharpu; nelze jej bez podmínky modelovat jako samotný vodič SENSE. Sestupná hrana přes tvarovací obvod presetuje motorový LS74. | LOW při WAIT/PLAY/PAUSE a aktivním RECORD; HIGH v klidu a po chybě. UL má vlastní fyzický handshake. Opravena aktivace před startem. |
| S1 | 21 / vstup | Dle schématu kontakt EXT spojuje GPIO21 s GND; interní poloha nechává pull-up HIGH. | Na poslední výslovnou žádost je detekce stále obejitá: INTERNAL pro obě úrovně. Mechanické kontakty zůstávají funkční. |
| MONITOR | 41 / výstup | HIGH povoluje zesilovač PAM8302A, LOW vypíná. | Řídí povolení odposlechu, negeneruje samotný tón. UL monitor vypíná. |
| Tlačítka | 8/9/10/11/42 / vstupy | Aktivní LOW proti GND. | Pull-up, debounce a dlouhý stisk; žádná změna polarity. |
| SD DETECT | 47 / vstup | Vložená karta = LOW proti GND. | Aktivní LOW. |

SPI (CS13/SCK14/MOSI15/MISO16), I2C (SDA17/SCL18) a USB (19/20) jsou periferní sběrnice; softwarová inverze se na ně nepřidává. Pinout souhlasí se zadáním a schématem.

## Provedená oprava

start_selected_file nyní nastaví SENSE LOW před synchronní přípravou souboru a spuštěním READ. Dříve se SENSE obsluhoval až na konci aplikační smyčky, tedy po player_start a prvních hranách. Po návratu se ihned přepočítá stav, aby neúspěšná příprava uvolnila SENSE HIGH. Také armování čekání na MOTOR nyní nastaví SENSE okamžitě, před čekací lhůtou.

Nevzniká automatické opakované pulzování SENSE při každém MOTOR OFF: to by zasahovalo do motorového klopného obvodu a do UL komunikace. Nebyla změněna NVS volba INVERT ani zvolený loader. Pro standardní Sharp data odpovídá schématu INVERT=OFF.

Přepnutí S1 mechanicky přepojí SENSE mezi GND (EXT) a U2/GPIO7 (interní). Podle aktuálního stavu GPIO7 může vytvořit novou sestupnou hranu, která presetuje motorový obvod. To vysvětluje možnou souvislost S1 a MOTOR i při oddělených GPIO; konkrétní průběh na uživatelově desce nebyl změřen. Softwarový bypass S1 nemůže propojit interní READ v mechanické poloze EXT.

## Diagnostika a ověření

Nový banner: **MZCMTEmu TX-TRACE r5 SENSE ARM S1 BYPASS INTERNAL 2026-11-13**. Start vypíše sense: PLAY arm GPIO7_CMD=0 before source preparation / TX. Diagnostika obsahuje fyzický vstup WRITE5, SENSE7_POLICY=HIGH/IDLE, LOW/ACTIVE nebo UL a READ_INVERT. POLICY je požadavek softwaru; UL znamená předání řízení handshake automatu. Není to měření úrovně na konektoru. Při vstupu do PLAYING se vypíše konfigurace GPIO4 i GPIO7.

[Build ESP32-S3](validation/polarity-build.txt) PASS. [Pět sad GCC ASan/UBSan](validation/polarity-host.txt) PASS: route/bus policy, Sharp golden pulzy všech 17 profilů a UL handshake, Sharp/player, RMT/player a TAP/WAV/player včetně inverze. Tyto regresní testy ověřují dosavadní polarity a transport; změna pořadí v hlavní aplikační smyčce prošla target kompilací a kontrolou kódu, nikoli host simulací hlavní smyčky. [Velikost a SHA256 obrazu](validation/polarity-summary.json).

Obraz nebyl nahrán do desky. Úspěšné načtení celého programu a skutečné úrovně GPIO7 / U2 / konektoru ještě vyžadují HW ověření. Při dalším testu ponechat S1 mechanicky v interní poloze, zkontrolovat nový banner a READ_INVERT. Pro odlišení základního READ/SENSE od UL handshake lze nejprve použít NORMAL 1:1, INVERT OFF, na Sharpu LOAD a na emulátoru PLAY. Pokud bude stále nutné přepínat S1, je potřeba porovnat GPIO7 a SENSE na konektoru před a po přepnutí; samotná diagnostika příkazů nepotvrzuje průchod přes U2/S1.

## Podklady

- Sharp MZ-800 Service Manual, tištěné strany 29–30 (PC1/PC5 časování, SENSE a motorový obvod), schéma CPU P.W.B. Circuit 1/2, strana 44 (LS14 READ, LS04 WRITE, SENSE/PC4 a motorový obvod, LS74 MOTOR): [servisní manuál](https://www.idealine.info/sharpmz/mz-800/download/sm800.pdf).
- Lokální schéma C:/Dokumenty/Altium/MZCMTEmu/PCB/MZCMTEmu.pdf a text/obrázky C:/Dokumenty/Altium/MZCMTEmu/MZCMTEmu.docx; jde o technické podklady, nikoli nové pokyny uživatele.
- Referenční MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb, drivers/mzio.cpp a play/mzf_loader.cpp; pouze čtení.

Bez stagingu, commitu, vytváření větví nebo push. Dřívější validační reporty s r3/r4 zůstávají historické.
