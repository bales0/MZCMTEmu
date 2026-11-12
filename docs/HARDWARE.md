# Hardware MZCMTEmu

Pinmap je zmrazená podle zadání a existujících zdrojů; tento zásah nepřesouvá GPIO ani nemění PCB. Podklady: lokální MZCMTEmu.docx a jednostránkové schéma PCB/MZCMTEmu.pdf, vizuálně zkontrolované 12. 11. 2026. Popis DOCX je původní návrh, nikoli důkaz implementace. Pro současný port rozhoduje úplné zadání ESP32-S3.

| Funkce | GPIO | Aktuální význam |
|---|---:|---|
| READ | 4 | Výstup do MZ; raw idle LOW, volitelná inverze může změnit fyzickou úroveň |
| WRITE | 5 | Vstup z MZ přes převod úrovní; GPIO ISR + GPTimer timestamp |
| MOTOR | 6 | Vstup s pull-up; současný kód běží při HIGH, skutečnou polaritu změřit |
| SENSE | 7 | Výstup; idle HIGH, aktivní připravený transport LOW, UL samostatná sekvence |
| PLAY / STOP | 8 / 9 | Tlačítka proti GND, pull-up |
| NEXT / PREV | 10 / 11 | Tlačítka proti GND, repeat |
| REC | 42 | Tlačítko proti GND |
| SD CS / SCK / MOSI / MISO | 13 / 14 / 15 / 16 | SPI2, DMA, nyní 10 MHz |
| OLED SDA / SCL | 17 / 18 | SSD1306 128×64, 0x3C, 400 kHz |
| SWITCH | 21 | Vstup stavu fyzického přepínače, není řízený mux |
| Monitor enable/mute | 41 | Kód: HIGH aktivní, LOW idle/UL mute; ověřit na konkrétní desce |
| SD detect | 47 | Active LOW, debounce v aplikaci |
| USB D− / D+ | 19 / 20 | Zachovat pro USB |

Schéma obsahuje S1A–S1D (4P2T) pro fyzickou volbu trasy a indikaci stavu, dva osmipinové převodníky U1/U2 s VCCA/VCCB/DIR a zesilovač PAM8302A U5. Typy U1/U2 nejsou uvedené; nelze z PDF potvrdit jejich elektrické specifikace nebo power-off chování. Monitor využívá analogovou signálovou cestu ve schématu; GPIO41 nelze bez ověření popisovat jako PWM zvukový výstup. Firmware dosud SWITCH pouze čte a neprovádí požadované úplné blokování transportu v EXT režimu.

Cílový modul je ESP32-S3 WROOM N16R8, 16 MB flash a 8 MB octal PSRAM. sdkconfig.defaults již tuto konfiguraci obsahuje. PlatformIO upload/image header byl v tomto zásahu sjednocen na 16 MB; původní board preset je N8 bez PSRAM. Boot a skutečná velikost PSRAM nebyly měřeny. GPIO používaná pro interní PSRAM se nepřemapovávají.

## Kontroly před HW akceptací

Ověřit osazení U1/U2, napájení obou stran, směry DIR, 3,3V úrovně na GPIO5/6 a kompatibilitu READ/SENSE s MZ. Ověřit idle a reset i při vypnutém ESP nebo MZ, polaritu na obou stranách převodníků a funkci S1 ve všech polohách. Ze schématu nelze odvodit bezpečný reset výstupů firmwarem: během resetu nemusí GPIO ještě řídit aplikace. Přímé přivedení 5 V na GPIO není dovoleno.

Žádná změna PCB nebyla prokázána jako nutná. Chybí elektrická a časová měření; tato skutečnost blokuje HW akceptaci, nikoli další host implementaci. Protokol HW zkoušek je v [TEST_REPORT.md](TEST_REPORT.md).

Ovládání aktuální verze a jeho omezení viz [README](../README.md). Kompletní nová keymap všech cílových obrazovek zbývá navrhnout a ověřit společně s oddělením UI a transportu.
