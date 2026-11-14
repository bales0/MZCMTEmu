# UL, UL700 a UL800 podle SD2CMT2 – r15

Stav 14. 11. 2026. Referenční projekt MZ-SD2CMT2-Reborn, commit 600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb, byl pouze čtený. Navazuje na [referenční sekvenci r14](UL_SD2CMT2_REFERENCE.md).

Uživatel potvrdil, že r14 v UL800 program nahraje, ale výrazně pomaleji než SD2CMT2. Přiložený log běžného UL s Tc122.mzf naopak končí po 359 z 7083 bajtů čekáním na WRITE HIGH. Pro UL700 není doložen skutečný HW pokus. Tyto výsledky se nesměšují s níže uvedenými host testy; r15 dosud nebyl nahrán ani ověřen na Sharpu.

## Oprava rychlosti

Firmware má CONFIG_FREERTOS_HZ=100: jeden tick je 10 ms. Původní player po každé krátké dávce UL usnul na jeden tick. Nyní po dokončených bajtech pokračuje bez tohoto čekání a po 100 ms práce přenechá jeden tick idle tasku. Vynucené předání nastává při SENSE HIGH, mimo rozpracovaný bajt. Start, pauza a ostatní režimy ponechávají obvyklé uspávání. Globální frekvence RTOS se nemění.

Podrobný ring trace a doplňkové GPIO čtení jsou v běžném obrazu vypnuté (CMT_UL_TRACE=0). Zůstává BEGIN, END, přesná fáze případné chyby a průběh přenosu. Pro podrobnou diagnostiku lze sestavit CMT_UL_TRACE=1; tato varianta zatěžuje handshake. Debug sestavení nyní výslovně používá -O2 a zachovává symboly. [Záznam skutečných voleb překladače](validation/ul-fast-compiler-flags.json) potvrzuje poslední účinné -O2 u čtyř souborů přenosové cesty; samotné build_flags dříve přepisovalo debug -Og.

## Ověřená posloupnost a polarity

Zdroj: referenční src/play/mzf_loader.cpp (send_byte_legacy, service_startup, mzf_loader_pump) a přechod hlavičky v src/play/mzf_playback.cpp. Nativní přijímací pole jsou shodná s referencí bajt po bajtu: [porovnání polí a SHA256](validation/ul-family-reference-arrays.json).

| Vlastnost | UL | UL800 | UL700 |
|---|---|---|---|
| Po dokončení RMT hlavičky | SENSE HIGH, READ HIGH | SENSE HIGH, READ LOW | SENSE HIGH, READ HIGH |
| Start | 100 ms | 700 ms pro start kódu na MZ, pak READ HIGH alespoň 1 ms | WRITE HIGH → READ LOW → WRITE LOW |
| První čekání | datový limit | WRITE LOW až 5 s | společný ARM limit 5 s |
| Datový limit | 500 ms | 500 ms | 1 s |
| Dvojice bitů | WRITE LOW → první READ bit → SENSE LOW; WRITE HIGH → druhý READ bit → SENSE HIGH | stejné | stejné |
| Konec | WRITE LOW → READ LOW, SENSE HIGH | stejné | stejné |

Převrácení datového bajtu a rotace o dva bity odpovídají referenci. Polarity READ/WRITE/SENSE se plošně neinvertují. Test přijímačů modeluje inverzi vstupů 8255 PC4/PC5 vůči SENSE/READ a inverzi WRITE vůči PC1. Nejde o měření konektoru ani o emulaci celé ROM MZ. MOTOR ON zůstává HIGH; softwarový bypass S1 zůstává zapnutý podle pokynu uživatele.

## Ověření

- [Čtyři host sady](validation/ul-fast-host.txt): test_ul_z80, test_sharp_stream, test_player_sharp, test_player_rmt; GCC 14.2, ASan/UBSan, PASS. Skutečné generované přijímací instrukce UL/UL800/UL700 pro LOW i HIGH umístění přijaly všech 256 hodnot bajtu; opačné polarity byly odmítnuty. Model vynechává hlavičkovou ROM a analogové vlastnosti HW.
- Nezávislý dekodér GPIO ověřil každý ze tří režimů na 44 033 bajtech, pořadí hran, první ACK po 8,6 ms, dokončené bajty před návratem, pauzu i nulový počet diagnostických čtení GPIO v běžné smyčce. Testy ověřují také startovací prodlevy, timeouty a bezpečné ukončení.
- [Samostatný test se skutečným 10ms tickem](validation/ul-fast-100hz-host.txt): PASS. Spouští skutečnou smyčku player tasku, ověřuje pravidelný idle po 100–110 ms práce a předání při SENSE HIGH.
- [Volitelný detailní trace](validation/ul-fast-trace-host.txt): PASS, včetně přesné fáze timeoutu a stejného plánování tasku.
- [Cílový build ESP32-S3](validation/ul-fast-build.txt): PASS, RAM 155 292 B, aplikace 460 063 B. Verbose režim nástroje narazil při výpisu příkazu balení na chybu SCons; běžné sestavení následně obraz úspěšně vytvořilo.

Časy a bytes_per_s z host testů jsou simulované, obsahují i testovací pauzu a nejsou měřením rychlosti na Sharpu. Na HW nové END uvádí data_ms a bytes_per_s; data_ms se počítá od prvního datového SENSE LOW a zahrnuje případnou uživatelskou pauzu. Oprava odstraňuje doložené zdroje zpomalování, avšak úspěch běžného UL, UL700 i výsledná rychlost UL800 na desce zůstávají k potvrzení.

## Obraz

Boot banner: MZCMTEmu TX-TRACE r15 SD2CMT2 UL FAST S1 BYPASS INTERNAL 2026-11-14.

Soubor: .pio/build/MZCMTEmu/firmware.bin, 460480 B.
SHA256: 7c02e4c2d632e1b8499306bca9793ad6f71c49f20fad9f930ba7b0d3765c4a1b.

Žádný flash, staging, commit, větev ani push nebyl proveden.
