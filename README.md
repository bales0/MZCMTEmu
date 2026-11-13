# MZCMTEmu

Port kazetového rozhraní Sharp MZ na ESP32-S3 N16R8: 16 MB flash, 8 MB octal PSRAM, SD SPI a SSD1306 128×64. Funkční vzor je [SD2CMT2-Reborn 2.0.3](https://github.com/bales0/MZ-SD2CMT2-Reborn/tree/600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb).

Implementované části prošly 20 hostitelskými sadami a cílovým sestavením. **Port dosud nemá HW akceptaci (HW zatím není připojený):** neproběhl LOAD na Sharpu, měření analyzátorem ani hodinový test SD/PSRAM. Rozsah důkazů a otevřené kontroly jsou v [matici parity](docs/PARITY_MATRIX.md) a [test reportu](docs/TEST_REPORT.md).

## Funkce

- PLAY: MZF/MZT/M12, read-only QuickDisk MZQ/QDF/QD, WAV, LEP/L16 a ZX TAP 1400/1800/2100/2500 Bd. Sharp má všech 17 režimů, původní IC/TC/FAST3/UL loadery, MFI/M2I/MTI a AUTO s bezpečným fallbackem.
- MZT/QD selektor: titul, rekord, individuální profil; návaznost MZT podle reference včetně kombinací UL a běžných rekordů. Po UL→UL se přenos ukončí. STOP za běhu kontejneru vrací selektor, také u jediného rekordu. Po opětovném vložení SD se přerušený kontejner znovu validuje a může obnovit selektor; vyžaduje nové PLAY.
- RECORD: WAV 22,05/44,1 kHz, LEP, L16 a validovaný MZF. Nezávislé MOTOR/MANUAL/AUTO řízení, uživatelská pauza, AutoName a detekce Sharp/IC/TC/Sinclair/CP/M. MZF kontroluje checksum a normalizuje kopírovací loader; neúplný záznam smaže.
- Dokončení záznamu běží na úloze SD se stavem SAVING RECORD. AutoName používá _01…_99 při kolizi, nikdy nepřepíše existující soubor. MFI vzniká pod výsledným názvem; chybu metadat OLED ohlásí samostatně.
- Browser: řazení podle názvu/přípony, scroll, návrat k rodiči, skryté sidecary, zrušitelná analýza. Jednorázový PSRAM index nejvýše 65535 položek a 2 MiB arena jmen; překročení limitu je explicitní chyba. Cesty mají limit 1024 B.
- Nabídka: rodina loaderu a zapamatovaná rychlost každé rodiny, TAP speed, inverze, PLAY MOTOR, RECORD formát/řízení/AutoName, jas OLED, stav SD/retry, ABOUT a KEYMAP. NVS schéma v2 má CRC a migraci v1; změněné nastavení se ukládá při návratu z menu.

## Transport a paměť

Výchozí PLAY používá jeden kontinuální RMT DMA přenos s rozlišením 16 MHz. Sharp/TAP ticks zůstávají přesné, WAV vychází z absolutní sample osy; LEP/L16 používají uložené šířky. RX používá hardwarově zachycené MCPWM timestampy. ISR neprovádí FS, alokace ani I²C.

Jediná úloha storage vlastní SD deskriptory, adresáře a mount/unmount. Pevný pool osmi souborů má 1 KiB cache; opravy placeholderů uvnitř cache nevyvolávají seek na SD. Nativní souborová cesta nealokuje stdio FILE/buffery. RECORD finalize je asynchronní. PSRAM browser, pool rekordových profilů a MZF payload se alokují jednou při inicializaci.

RMT pauza pozastavuje zdrojové hodiny celé RMT skupiny; CMT musí být jejím jediným uživatelem. Retenci rozpracovaného symbolu je nutné ověřit na ESP32-S3. Diagnostické alternativy CONFIG_MZ_TX_GPTIMER_FALLBACK a CONFIG_MZ_CAPTURE_GPIO_FALLBACK jsou ve výchozím sestavení vypnuté. [Časování a footprint](docs/TIMING_PORT.md).

## Sestavení a testy

PlatformIO, pinovaná platforma espressif32@7.0.1, ESP-IDF 6.0.1:

    pio run -e MZCMTEmu -j 1
    python tests/host/run_tests.py --cc gcc

Volitelně přidejte --tap-fixtures tests/TAP --qd-fixtures tests/QD --qd-fixtures tests/MZQ. Sanitizery vyžadují jejich podporu hostitelským kompilátorem; na ověřeném MinGW dostupné nebyly. Testy vytvářejí dočasné výstupy v .tmp a uživatelské fixture pouze čtou.

N16R8 override nastavuje image/upload header i sdkconfig na 16 MB a octal PSRAM. PlatformIO úvodní popis generické desky stále uvádí N8; rozhoduje konfigurace a hlavička výsledného obrazu. App partition má 1 MB. [Výsledky sestavení](docs/validation/port-build.txt).

## Ovládání pěti tlačítky

| Kontext | PLAY | STOP krátce | STOP dlouze | NEXT / PREV | REC |
|---|---|---|---|---|---|
| Browser | Složka, příprava/start souboru | Rodič | Menu | Výběr + repeat | Začátek/čekání RECORD |
| MZT/QD selektor | Zvolený rekord | Browser | Ukončení | Rekord | Profil tohoto rekordu |
| PLAY | Pauza/pokračování dle MOTOR/módu | Stop; kontejner → selektor | Ukončení | Výběr browseru na pozadí, bez seek | Bez akce |
| RECORD | Pauza/pokračování | Uložit; další STOP návrat | Zrušit a smazat | Bez akce | Bez akce |
| SAVING | Bez akce | Vyčkat dokončení | Vyžádat smazání po uzavření | Bez akce | Bez akce |
| Menu | Změnit volbu / otevřít info | Zpět + uložit | Bez akce | Volba; v KEYMAP stránka | Bez akce |
| Bez SD | Bez startu | Návrat dle stavu | Menu | Bez procházení | Bez startu |

Odrušení 30 ms; STOP long 800 ms; repeat po 400 ms, dále po 120 ms. STOP má prioritu v kombinaci tlačítek a blokuje opakování držené navigační klávesy do další obrazovky. Krátký STOP se vyhodnotí při uvolnění. Záznamy směřují do /RECORDINGS.

S1 fyzicky přepíná CMT trasu. EXT po debounce zablokuje PLAY/REC a bezpečně ukončí transport, software nevytváří mux. Uživatel potvrdil neinvertující převodníky U1/U2; elektrické parametry a resetové stavy stále vyžadují měření. [Pinmap a HW kontroly](docs/HARDWARE.md).

Přenesené algoritmy, Z80 loadery a znakové tabulky zachovávají GPLv3 atribuci; AVR periferie se nepoužívají. [Licence](LICENSE), [inventura zdrojů](docs/SOURCE_INVENTORY.md), [předání](HANDOFF.md).
