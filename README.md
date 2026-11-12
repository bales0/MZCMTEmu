# MZCMTEmu

Rozpracovaný emulátor kazetového rozhraní Sharp MZ na ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM), SD SPI a SSD1306 128×64. **Plná parita se SD2CMT2-Reborn 2.0.3 zatím není implementovaná.** Aktuální stav je v [matici parity](docs/PARITY_MATRIX.md), navazující práce v [HANDOFF.md](HANDOFF.md).

Existující firmware obsahuje základní browser adresářů, PLAY WAV/LEP/L16/MZF/MZT/M12/TAP, RECORD WAV 22,05/44,1 kHz a LEP/L16, motorovou a uživatelskou pauzu, AUTO detekci aktivity záznamu a částečný UL. Tento seznam popisuje existující kód, nikoli ověření kompatibility všech formátů na Sharpu. MZT zatím přehrává posloupnost rekordů bez referenčního selektoru. Chybí QuickDisk, kompletní profily/loadery, metadata MFI/M2I/MTI, MZF record, AutoName, klasifikace a perzistentní nastavení.

ZX Spectrum TAP je připojený do browseru a přehrávače. Menu TAP SPEED nabízí 1400, 1800, 2100 a 2500 Bd (výchozí 1400); PLAY změní rychlost, STOP opustí menu. Aktivní přehrávání si ponechá rychlost zvolenou při startu. Nastavení rychlosti zatím nepřežije restart. Parser kontroluje celý kontejner před PLAY a streamuje jeho bloky bez načtení celého souboru do RAM. Přenáší všechny bajty včetně původního checksumu; stejně jako reference neodmítá soubor podle XOR checksumu obsahu. Golden waveform a host simulace pulzního driveru prošly; přehrání na Sharpu a odezva OLED menu nebyly ověřeny.

LEP/L16 recorder nyní zaokrouhluje každý fyzický interval nezávisle, bez přenosu chyby do následující úrovně. WAV zachovává absolutní vzorkovací fázi. RMT TX DMA ani HW capture backend zatím nejsou implementované; současné časování používá GPTimer/GPIO ISR. Funkce se jménem RMT nejsou důkazem RMT provozu.

## Sestavení a testy

Použijte PlatformIO s ESP-IDF a cílem MZCMTEmu:

    pio run -e MZCMTEmu -j 2

Ověřená lokální sestava: Espressif32 7.0.1, ESP-IDF 6.0.1. Závislosti platformy zatím nejsou pinované; jiné verze nejsou tímto výsledkem ověřené. Výchozí sdkconfig nastavuje 16 MB flash a octal PSRAM; PlatformIO image/upload header je také 16 MB. Jednomegabajtová app partition je odlišná od celkové velikosti flash.

Host test produkčního recorderu, writerů a edge parseru vyžaduje Python 3 a C11 kompilátor:

    python tests/host/run_tests.py --cc gcc

Na podporovaném GCC/Clang lze přidat --sanitize. Výsledky, konkrétní Windows příkazy a omezení jsou v [TEST_REPORT.md](docs/TEST_REPORT.md). Výchozí testy nepotřebují externí soubory a vytvářejí dočasné výstupy v .tmp. Volitelně lze přidat --tap-fixtures tests/TAP pro read-only kontrolu dodaných TAP souborů; tento příkaz je součástí aktuálního test reportu.

## Současné ovládání

| Kontext | PLAY | STOP krátce | STOP dlouze | NEXT / PREV | REC |
|---|---|---|---|---|---|
| Browser | Vstup do složky nebo příprava/start souboru podle MOTOR/módu | Nadřazená složka | Menu | Výběr s repeat | Zahájení/čekání záznamu |
| PLAY | Pauza/pokračování; pokračování respektuje MOTOR/mód | Ukončení | Ukončení | Mění výběr browseru na pozadí; není seek | Ignorováno při aktivním/čekajícím PLAY |
| RECORD | Pauza/pokračování při povoleném MOTOR/módu | Uložení; další STOP návrat | Zrušení a odstranění rozpracovaného souboru | Ignorováno | Ignorováno |
| Menu | Změna vybrané volby | Návrat | Ignorováno | Výběr volby s repeat | Bez akce |
| Bez SD | Bez startu | Návrat podle stavu | Menu | Bez procházení | Bez startu |

STOP dlouze je 800 ms, NEXT/PREV repeat po 400 ms po 120 ms. Současný 20ms polling není kompletně otestované odrušení tlačítek; STOP se vyhodnocuje krátce až při uvolnění. Plná nová keymap pro selector, PLAY/RECORD/SYSTEM a ochrana emergency STOP zbývají implementovat. Menu nyní obsahuje mód signálu, inverzi, UL, formát záznamu, AUTO aktivitu a TAP SPEED; volby nepřežijí restart. Záznamy směřují do /RECORDINGS na SD.

Pinmap, fyzická cesta přepínače S1 a dosud neověřené 3,3/5V úrovně jsou v [HARDWARE.md](docs/HARDWARE.md). SWITCH zatím není úplná obsluha EXT režimu. Žádný HW test tohoto portu nebyl proveden.

Referenční funkční vzor: [SD2CMT2-Reborn](https://github.com/bales0/MZ-SD2CMT2-Reborn/tree/600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb). Inventura obsahuje [všechny zdrojové soubory](docs/SOURCE_INVENTORY.md). Timing fakta v testech pocházejí z referenční tabulky. TAP protokol a jeho rychlosti byly portovány z play/tap_playback.cpp a tap_speed.cpp v uvedeném commitu pod GPLv3, s atribucí ve zdrojovém modulu. AVR ovladače ani loaderové binárky nebyly převzaty. Licenční text projektu zůstává v [LICENSE](LICENSE); při dalších portech zachovat původní GPLv3 licence a atribuce jednotlivých částí.
