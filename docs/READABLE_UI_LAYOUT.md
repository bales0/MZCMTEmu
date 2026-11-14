# Čitelné OLED rozložení – r23

Schválená úprava ze 14. 11. 2026 sjednocuje všechny texty pohledu UI na font 6×8, včetně hlavičky QD, indexů a druhého řádku překryvných zpráv.

PLAY má osm fyzických řádků:

| Řádek | Obsah |
|---|---|
| 1 (y=0) | Symbol a formát/stav, index vpravo; u QD název obrazu |
| 2 (y=8) | Název programu |
| 3 (y=16) | Typ a profil přenosu/zdroje |
| 4 (y=24) | Velikost |
| 5 (y=32) | LOAD a EXEC/AUTO |
| 6 (y=40) | Čas, případně stav QD nebo varování |
| 7 (y=48) | Grafický průběh bez P a procent |
| 8 (y=56–63) | TX vlevo, BUF vpravo |

FINISHED se zobrazuje jen jednou se symbolem STOP. QD ponechává název obrazu v hlavičce; jeho pozastavení, výběr a dokončení se ukazují vedle času. Dlouhý název a informace o profilech se posouvají. RECORD zachovává vlastní průběh a spodní RX FREE / MIN.

Ověření: cílový PlatformIO build PASS; GCC14 s ASan/UBSan: test_oled_ui, test_ui_policy, test_player_sharp a test_player_rmt PASS. Pixelové kontroly ověřují všechny obsazené PLAY řádky, přesnou pozici TX/BUF, jediný FINISHED a STOP, zarovnání PLAY/PAUSE, QD hlavičku, RX a osm fází animace. [Náhled](validation/readable-ui-preview.png) ukazuje zleva doprava PLAY/FINISHED, QD výběr/pauzu, RECORD/nedostatek bufferu, plný TX buffer/dvouřádkovou zprávu.

Firmware: 462704 B; SHA-256 `84ba7bf541abd8c67df302ebfdf870e905638bd002c27de35698225b012e949c`. Banner `TX-TRACE r23 READABLE UI`. Obraz nebyl nahrán do desky a nové rozložení zatím nemá HW ověření. Bez commitu a vytvoření větve.
