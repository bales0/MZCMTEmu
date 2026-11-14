# Sjednocení RECORD a PLAY – r26

Schválená úprava ze 14. 11. 2026 používá stejné rozložení metadat a plný průběh (x=0..127, y=49..54) v obou režimech. RECORD nemá u průběhu D ani procenta. Aktivní REC je označen ikonou, text REC se neopakuje; pauza, čekání na signál/motor, ukládání, uložení a chyba zůstávají textem v hlavičce.

Dole je čas záznamu vlevo (x=0, y=56) a pevný BUF vpravo (x=80, y=56). BUF u RECORD znamená volné místo RX bufferu, tedy stejnou veličinu jako dřívější RX FREE. Pole má tři pozice pro procenta. Minimum RX je na šestém řádku; LOW/CRIT/BAD zůstává vpravo, konkrétní chybová zpráva má přednost před minimem. PLAY zachovává aktuální/celkový čas při známé délce a procenta při neznámé.

Ověření: test_oled_ui a test_ui_policy s GCC14 ASan/UBSan PASS, cílový PlatformIO build PASS, git diff --check PASS. Pixelové kontroly ověřují široký průběh, pevné BUF při 0/7/73/100 %, čas dole, RX minimum, aktivní ikonu bez REC, pauzu, čekání na motor, SAVING, SAVED jen jednou se STOP a prioritu chyby. [Náhled](validation/record-ui-preview.png) ukazuje PLAY/RECORD, nízký buffer/pauzu, ukládání/uložení a chybu/LEP.

Firmware 462816 B, SHA-256 `a26128aad867b5775d20b71fa2adf3e53a4fdcfc680d11eb5c8c2a746a71a26b`, banner `TX-TRACE r26 RECORD UI`. Bez nahrání do desky a HW ověření. Bez commitu a vytvoření větve.
