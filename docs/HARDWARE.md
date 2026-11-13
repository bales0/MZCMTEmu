# Hardware MZCMTEmu

Pinmap je zmrazená podle zadání a existujících zdrojů; tento zásah nepřesouvá GPIO ani nemění PCB. Podklady: lokální MZCMTEmu.docx a jednostránkové schéma PCB/MZCMTEmu.pdf, vizuálně zkontrolované 12. 11. 2026. Popis DOCX je původní návrh, nikoli důkaz implementace. Pro tuto změnu rozhoduje zadání UI_RECORD_PLAY_METADATA_PCNT; dokumenty jsou technické podklady.

| Funkce | GPIO | Aktuální význam |
|---|---:|---|
| READ | 4 | Výstup do MZ; safe idle vždy LOW, INVERT mění jen aktivní waveform |
| WRITE | 5 | Vstup z MZ přes převod úrovní; MCPWM hardware-latched timestamp (výchozí backend) |
| MOTOR | 6 | Vstup s pull-up; současný kód běží při HIGH, skutečnou polaritu změřit |
| SENSE | 7 | Výstup; idle/READY HIGH, PLAY/PAUSE/WAIT a aktivní RECORD LOW, UL samostatná sekvence |
| PLAY / STOP | 8 / 9 | Tlačítka proti GND, pull-up |
| NEXT / PREV | 10 / 11 | Tlačítka proti GND, repeat |
| REC | 42 | Tlačítko proti GND |
| SD CS / SCK / MOSI / MISO | 13 / 14 / 15 / 16 | SPI2, DMA, nyní 10 MHz |
| OLED SDA / SCL | 17 / 18 | SSD1306 128×64, 0x3C, 400 kHz |
| SWITCH | 21 | Vstup stavu fyzického přepínače, není řízený mux |
| Monitor enable/mute | 41 | HIGH aktivní signál, LOW READY/WAIT/pause/idle/UL/finalize; HW ověřit |
| SD detect | 47 | Active LOW, debounce v aplikaci |
| USB D− / D+ | 19 / 20 | Zachovat pro USB |

Schéma obsahuje S1A–S1D (4P2T) pro fyzickou volbu trasy a indikaci stavu, dva osmipinové převodníky U1/U2 s VCCA/VCCB/DIR a zesilovač PAM8302A U5. Uživatel 12. 11. 2026 potvrdil, že U1/U2 jsou neinvertující. Konkrétní typy nejsou uvedené; jejich elektrické specifikace nebo power-off chování zůstávají neověřené. GPIO a konektor tak podle tohoto údaje nepřidávají další hardwarovou inverzi; logické Sharp timingové úrovně stále nejsou automaticky stejné jako fyzický READ. Monitor využívá analogovou signálovou cestu ve schématu; GPIO41 nelze bez ověření popisovat jako PWM zvukový výstup. Firmware SWITCH debouncuje 40 ms; EXT zablokuje PLAY/REC, zastaví PLAY a zruší rozpracovaný RECORD včetně SAVING. Monitor je analogová cesta, nikoli nově vymyšlený PWM výstup.

Cílový modul je ESP32-S3 WROOM N16R8, 16 MB flash a 8 MB octal PSRAM. sdkconfig.defaults již tuto konfiguraci obsahuje. PlatformIO upload/image header byl v tomto zásahu sjednocen na 16 MB; původní board preset je N8 bez PSRAM. Boot a skutečná velikost PSRAM nebyly měřeny. GPIO používaná pro interní PSRAM se nepřemapovávají.

## Kontroly před HW akceptací

Ověřit osazení U1/U2, napájení obou stran, směry DIR, 3,3V úrovně na GPIO5/6 a kompatibilitu READ/SENSE s MZ. Ověřit idle a reset i při vypnutém ESP nebo MZ, polaritu na obou stranách převodníků a funkci S1 ve všech polohách. Ze schématu nelze odvodit bezpečný reset výstupů firmwarem: během resetu nemusí GPIO ještě řídit aplikace. Přímé přivedení 5 V na GPIO není dovoleno.

Žádná změna PCB nebyla prokázána jako nutná. Chybí elektrická a časová měření; tato skutečnost blokuje HW akceptaci, nikoli další host implementaci. Protokol HW zkoušek je v [TEST_REPORT.md](TEST_REPORT.md).

Ovládání aktuální verze a jeho omezení viz [README](../README.md). Keymap je v README a v nabídce KEYMAP; fyzická ergonomie, latence, OLED layout a resetové stavy čekají na HW ověření. Firmware před povolením výstupů přednastaví latch READ LOW, SENSE HIGH a MONITOR LOW; reset mimo běžící aplikaci musí zajistit a prokázat hardware.


## Mechanický S1 a volitelný PCNT — 13. 11. 2026

Při nové vizuální kontrole schématu je S1D COM4 na GND, A4 na GPIO21 a B4 nezapojený. S pull-up v kódu tedy kontakt A znamená LOW/EXT a kontakt B HIGH/INTERNAL. S1B A2 vede na EXWRITE. To je čtení zapojení, nikoli proměření kontinuity, fyzické polohy páčky nebo všech kontaktů. GPIO21 je výhradně INPUT, debounce40 ms; žádný relé/mux driver ani nové GPIO. EXT během PLAY/UL vyvolá bezpečné zastavení a během rozpracovaného RECORD/SAVING abort bez hlášení SAVED; INTERNAL předchozí transport nerestartuje.

CONFIG_MZ_RX_PCNT_GUARD (default n) připojuje interní PCNT přes GPIO matrix ke stejnému WRITE GPIO5 jako MCPWM, bez změny zapojení. Počítá obě hrany bez filtru; SDK accum_count a ±30000 watchpoints, IRQ3/cache-safe. Nemění MCPWM timestampy. Hranice epoch mají explicitní interval nejistoty, tail není fyzická hrana. Potvrzená ztráta končí EDGE LOSS; nespolehlivý čítač CAPTURE ERROR. Ani shodné počty neprokazují správné časování. Detaily a testy jsou v [TEST_REPORT](TEST_REPORT.md).

**HW NEPROVEDENO:** deska není připojena; zbývá ověřit elektrické úrovně/reset, mechanickou kontinuitu a PCNT/MCPWM časovou shodu za zátěže. Neinvertující U1/U2 jsou údaj uživatele, jejich přesné typy a power-off chování nejsou doloženy.
