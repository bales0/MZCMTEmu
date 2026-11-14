> Následné HW ověření uživatelem: r14 v UL800 program nahraje, ale pomalu. Aktuální kontrola UL/UL700 a oprava výkonu je v [r15](UL_FAMILY_PERFORMANCE.md). Níže je původní záznam r14.

# UL / UL800 – sekvence převzatá ze SD2CMT2 (r14)

Aktuální pokyn uživatele14.11.2026: použít sekvenci z fungujícího SD2CMT2; UL800 potřebuje prodlevu na spuštění kódu na MZ. Referenční projekt C:/Dokumenty/GitHub/MZ-SD2CMT2-Reborn zůstává pouze čtený, HEAD600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb. Žádné úpravy reference, nové větve ani commit.

## Převzatá posloupnost

Podklad: src/play/mzf_loader.cpp, wait_write_level_legacy (628–640), send_byte_legacy (710–722), mzf_loader_begin/service_startup (1110–1167) a mzf_loader_pump (1169–1221). Přechod po hlavičce: src/play/mzf_playback.cpp mzf_advance_after_boundary a mzf_service_boundary_auto_continue (2781–2885). UL boundary v referenci přeskočí běžnou120ms meziblokovou prodlevu; samostatná startovací prodleva je uvnitř loaderu.

| Krok | UL | UL MZ800 |
| --- | --- | --- |
| Po fyzickém dokončení PWM/RMT | SENSE HIGH, READ HIGH | SENSE HIGH, READ LOW |
| Startovací prodleva | 100ms | 700ms pro spuštění přijímače na MZ |
| READY | rovnou přenos | READ HIGH a čekání nejméně1000us |
| První odpověď | standardní datové čekání | až5s na WRITE LOW |
| Každá dvojice bitů | WRITE LOW → první READ bit → SENSE LOW; WRITE HIGH → druhý READ bit → SENSE HIGH | totéž |
| Limit datového čekání | 500ms | 500ms |
| Konec | počkat WRITE LOW; READ LOW, SENSE HIGH | totéž |

Převrácení bajtu a rotace o2bity zůstává shodná s reference rotate_data(~value). Úrovně se předávají přímo GPIO; nebyla změněna žádná globální polarita. UL MZ700 ponechává vlastní ARM sekvenci,5s ARM timeout a1s datové čekání z reference.

700ms/1ms prodlevy už měl původní nativní FSM; v r14 jsou zachovány podle referenčních konstant a výslovně ověřeny. Nejsou nahrazeny novým odhadnutým časem. Přechod z normální hlavičky navíc čeká na prázdný FIFO i zastavený RMT, aby se nevstoupilo do UL před fyzickým dokončením posledního intervalu.

## Odstraněné experimenty a změna obsluhy

Odstraněn limit200us pro LOW ACK, přidané READ setup2us, hold50us a blokování IRQ během LOW. r13 log uživatele potvrdil pouze to, že ACK nepřišel do200us, GPIO cmd/pad přitom odpovídaly; příčina původní závady se tím neprokázala. Předchozí interpretace preempce a analogového obvodu zůstává hypotézou, nikoli potvrzenou diagnózou.

Datová pumpa nyní dokončí všechny4páry celého bajtu v souvislé foreground smyčce stejně jako send_byte_legacy, bez dobrovolného RTOS čekání mezi páry. Po nejvýše64bajtech nebo dosažení servisního rozpočtu vrátí řízení pouze na hranici bajtu. Rozpočet1,5ms se proto kontroluje po dokončení bajtu; nejde o tvrdý horní limit délky celého volání. Při čekání na data platí původní500ms timeout. Přerušení ESP zůstávají povolena. Startup a čekání na první odpověď se dál obsluhují neblokujícím FSM, s původními časovými konstantami.

Zachována diagnostika padů, ring buffer32událostí a LOW duration statistika. BEGIN nově vypíše sequence=SD2CMT2, start_delay_ms, ready_delay_us, initial_wait_ms a data_wait_ms. Běžný PLAY SENSE LOW / klid HIGH, S1 bypass a MOTOR ON=HIGH jsou beze změny. Není přidán adaptivní rearm ani nová mimoreferenční prodleva.

## Ověření

[Build ESP32-S3](validation/ul-reference-build.txt) PASS. [4 sady GCC ASan/UBSan](validation/ul-reference-host.txt) PASS:

- test_player_rmt: všechny17profilů, plný44033bajtový syntetický MZF s LOAD/EXEC1E00h jako Flappy z logu; první ACK má8,6ms, další20us. Protistrana nezávisle dekóduje každý bajt z GPIO READ/SENSE a porovná všechny. Test ověřuje READ zápis před každou hranou SENSE, návrat jen po celém bajtu a5s uživatelskou pauzu během dat. END sent44033/44033,176132oken, simulované low_cmd_max_us8603.
- test_player_sharp: všechny17profilů, správná dlouhá i krátká chybová cesta. Chybějící první WRITE vede na referenční5s initial timeout; chybějící datový ACK na500ms UL TIMEOUT. Důvod přežije close/reset; výstupy končí bezpečně HIGH/LOW podle idle politiky.
- test_sharp_stream: přesné hranice700ms a1ms UL800, první odpověď vs datový ACK, všechny17golden waveform/loaderové vektory a handshaky.
- test_ul_z80: relevantní skutečné instrukce generovaných LOW/HIGH receiverů, všechny256hodnot bajtu, bez přidaných hodinových prodlev. Po dokončení vysílače se nechá doběhnout závěrečný zápis a skok přijímače. Explicitní model inverzí portů není měření reálného8255 ani úplná emulace MZ.

Odstraněny experimentální testovací požadavky na50us hold,35us rearm a neustálý200us ACK limit. Tyto modely nebyly převzaté z reference a nevysvětlily HW výsledek. Host čas při GPIO pollingu modeluje postup CPU; neměří runtime na ESP.

[Souhrn a SHA256](validation/ul-reference-summary.json). Banner **MZCMTEmu TX-TRACE r14 SD2CMT2 UL S1 BYPASS INTERNAL 2026-11-14**. Obraz nebyl nahrán; funkce UL/UL800 na skutečném Sharpu po r14 dosud nepotvrzená. Bez stagingu, commitu, push nebo nové větve.
