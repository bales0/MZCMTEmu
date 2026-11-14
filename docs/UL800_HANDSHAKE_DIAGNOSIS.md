# UL800: selhání po hlavičce – diagnostika r9

Uživatel potvrdil vadný kontakt pouzdra převodníku jako příčinu problémů SENSE. Automatická funkce SENSE zůstává obnovená. Nový samostatný problém: UL800 selhává hned po hlavičce. Přesný sériový chybový řádek z HW zatím není k dispozici.

UL HANDSHAKE na OLED je stav, nikoli sám o sobě chybový kód. Stávající FSM čeká u UL MZ800 700 ms s READ LOW, poté READ HIGH a 1 ms prodlevu, poté až 5 s na WRITE LOW. To odpovídá časovým konstantám a postupu referenčního MZ-SD2CMT2-Reborn. Datové fáze čekají střídavě WRITE LOW/HIGH. Polarity, loaderové bajty a timeouty nebyly pro tento pokus změněny.

Na začátku UL bylo sjednoceno pořadí fyzických zápisů s referencí: nejprve SENSE HIGH, potom počáteční READ. Během dat zůstává READ před SENSE. Jde o odstranění rozdílu v pořadí startu; není potvrzeno, že byl příčinou chyby na desce.

Nový ul: FAIL vypisuje reason, phase, expect_WRITE, WRITE5, pair, READ4_CMD, SENSE7_CMD, write_edges, sent/total a elapsed_us. Úrovně CMD jsou stav před selhávajícím krokem; následně se vrací safe idle READ LOW/SENSE HIGH. write_edges je počet změn pozorovaných pollingem GPIO5 během UL, nikoli záznam všech fyzických hran analyzátorem. phase INITIAL_WRITE_LOW a sent0 znamená, že nepřišlo počáteční potvrzení WRITE LOW. PAIR_WRITE_HIGH znamená chybějící potvrzení po prvním/dalším SENSE LOW. Samotné INITIAL_WRITE_LOW/WRITE5=1 nerozlišuje nespuštěný přijímač Sharpu od neprůchodného WRITE přes desku.

[Build](validation/ul800-trace-build.txt) PASS. [3 sady GCC ASan/UBSan](validation/ul800-trace-host.txt) PASS: player/RMT, player/Sharp a Sharp stream. Nový test ověřuje hranice 700 ms a 1 ms, zvlášť chybějící počáteční WRITE LOW a chybějící první ACK HIGH, správný diagnostický název/očekávání i bezpečné ukončení. Simulace zablokovaného peeru vyprodukovala INITIAL_WRITE_LOW expect_WRITE0 WRITE5=1 sent0/4; to je host test, nikoli měření uživatelovy desky.

Obraz: **MZCMTEmu TX-TRACE r9 UL PHASE TRACE S1 BYPASS INTERNAL 2026-11-13**. Bez flash, stagingu, commitu, větve a push. Oprava UL800 na skutečném Sharpu dosud není potvrzená. Dalším podkladem je ul: BEGIN a ul: FAIL z COM6 při opakování stejného souboru v UL MZ800.
