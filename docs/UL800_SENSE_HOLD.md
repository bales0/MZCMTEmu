> Pozdější HW test r11 neprošel: Tc122 sent3/7083, pair3. Níže uvedený model neprokázal příčinu na desce. Aktuální diagnostika: [r12](UL800_PAD_TRACE.md).

# UL800: držení SENSE při předčasném WRITE ACK

## Nový podklad z HW

Tc122.mzf: 7083 bajtů; hlavička RMT done1/encoded_us3632696, UL BEGIN WRITE5=1. Chyba UL TIMEOUT phasePAIR_WRITE_HIGH expect_WRITE1 WRITE5=0 pair1 READ4_CMD1 SENSE7_CMD0 write_edges3 sent0/7083 elapsed_us1209683. Oproti Flappy (sent21/pair1) tedy selže již v prvním bajtu, opět na pair1. r10 READ setup2us problém na tomto souboru neodstranil. MOTOR se během záznamu mění, ale player v UL MOTOR pauzu neprovádí; elektrický vliv motorového/SENSE obvodu tím není vyloučen.

## Časovací slabina a změna

Přijímač loader_body_mz800_low_working_P zapisuje potvrzení PC1/WRITE před dokončením DEC DE, DJNZ a návratem do dalšího testování PC4. Na hranici bajtu po potvrzení ještě ukládá bajt, obnovuje/dekrementuje čítač a znovu vstupuje do přijímací smyčky. Vysílač proto nesmí automaticky předpokládat, že okamžitá přítomnost WRITE ACK znamená současně připravenost rozpoznat libovolně krátkou další úroveň SENSE. To je konkrétní časovací omezení portu, nikoli důkaz osciloskopicky změřené příčiny na uživatelově desce.

r11 ponechává READ setup2us a po každé datové změně SENSE drží výstup po dobu50us před dalším krokem. Obě úrovně jsou tak viditelné i při okamžitém ACK; rychlý ESP nemůže úroveň zkrátit pouze na dobu několika GPIO volání. Pouze UL adaptér; běžné PLAY/WAIT/PAUSE SENSE, polarity, loaderové bajty a timeouty beze změny. Startup stále700ms/1ms. S1 software stále ignoruje podle dřívější žádosti.

Prodleva je bounded esp_rom_delay_us v popředí, nikoli ISR. Zachován servisní rozpočet1500us/512kroků s kontrolou mezi kroky. Přenos bude pomalejší než nekontrolované GPIO pulzy r10; jde o záměrnou rezervu pro zpracování na Z80. Hodnota50us je konzervativní parametr pro tento test a není deklarována jako změřená minimální hodnota konkrétního Sharpu.

## Regrese před a po opravě

Protistrana test_player_rmt byla rozšířena: WRITE ACK je viditelný ihned, ale po HIGH SENSE je připravena na další LOW až za35us. Dřívější mock tuto fázi neměl a vždy odpověděl okamžitě na každou hranu. Nový model ztracenou příliš brzkou LOW hranu neobslouží, WRITE zůstane LOW. Příjem bajtů se stále nezávisle dekóduje z READ/SENSE a porovnává s 44033bajtovým pseudonáhodným payloadem.

S nezměněným r10 adaptérem test vyvolal **UL TIMEOUT PAIR_WRITE_HIGH expect_WRITE1 WRITE5=0 pair1 sent0/44033**, tedy shodnou fázi a dvojici s Tc122. [Log očekávaného selhání](validation/ul-clock-hold-before.txt). Po přidání50us držení stejný model dokončí všech44033bajtů bez ztracené hrany. [Host log](validation/ul-clock-hold-host.txt): 3 sady GCC ASan/UBSan PASS (player/RMT, player/Sharp, Sharp stream). [Target build](validation/ul-clock-hold-build.txt) PASS. [Identifikace obrazu](validation/ul-clock-hold-summary.json).

Model reprodukuje třídu závady s předčasným ACK; nejde o plnou instrukční emulaci Z80/8255/motorového obvodu ani o měření skutečného stroje. Shoda timeoutu posiluje hypotézu, ale neprokazuje, že nemá deska další problém. r11 musí být ověřen na stejném Tc122.mzf a Flappy.mzf.

Banner: **MZCMTEmu TX-TRACE r11 UL SENSE HOLD S1 BYPASS INTERNAL 2026-11-13**. BEGIN navíc read_setup_us2 sense_hold_us50. Bez flash, stagingu, commitu, větve nebo push. Starší r9/r10 reporty jsou historie; HW úspěch UL800 dosud nepotvrzený.
