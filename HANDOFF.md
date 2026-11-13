# Předání UI / RECORD / PLAY / metadata / PCNT — 13. 11. 2026

Uživatel autorizoval změny dle specification/MZCMTEmu_Codex_zadani_UI_RECORD_PLAY_METADATA_PCNT.md. **Bez samostatného souhlasu žádný staging, commit, push, PR ani nové větve.** Checkout main, HEAD d3f67c4a978a38dad23e49c918b145182046452b. Práce zůstává necommitnutá. Uživatelské odstranění starého zadání a nové zadání jsou zachované. Reference600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb a MZTools nejsou upravovány.

## Výsledek

Grafické OLED nad jediným framebufferem: šest řádků browseru, inverzní výběr, marquee od změny kontextu, pravdivé RX FIFO/min/sticky alarmy a TX odhad časové rezervy včetně current/encoded intervalu, oddělený průběh. Overlay má priority/expiry/latch a retry dirty pages po I²C chybě. KEYMAP opravený. Browser po cancel/failed child obnoví původní adresář/výběr bez další2MiB kopie; CANCEL neshazuje SD, d_type omezuje stat.

Sdílený80B model programu/provenience/validity: původní Sharp source před generováním transportního loaderu, kanonické IC, TC až po validním prvním loader payloadu, MZT/QD změny. Pasivní WAV/LEP/L16 nad již produkovanými PLAY intervaly sdílí existující decoder s AutoName, TAP vlastní striktní ZX/XOR. Aktuální titul může přejít na další program, filename zůstane první. Neznámá pole --; ZX BASIC má AUTO LINE, nikdy vymyšlený EXEC. IC rychlý WAV44k nemusí validovat payload checksum; ověřený embedded header zůstává HEADER_VALID.

Volitelný CONFIG_MZ_RX_PCNT_GUARD default n; obě hrany GPIO5, SDK accum_count ±30000, žádná app ISR na každou hranu. Epochy mají explicitní hranicovou nejistotu, potvrzená ztráta EDGE LOSS, nespolehlivé počty CAPTURE ERROR. Shodný součet není garance timing/lossless. MCPWM časování zachováno; actual capture init call-chain je z CPU1. S1 mechanický, GPIO21 INPUT LOW=EXT/40ms: PLAY/UL stop, RECORD/SAVING abort, INTERNAL bez automatického restartu.

## Ověření

23 sad (původních20 + UI policy, metadata, PCNT) v Debianu GCC14.2 s ASan/UBSan, včetně skutečných generovaných audio souborů a dodaných TAP/QD/MZQ; kompletní [log](docs/validation/ui-metadata-host-linux.txt). Cílový build ESP-IDF6.0.1/espressif32@7.0.1/GCC15.2 prošel: RAM154348 B, aplikace461513 B, image461936 B,16MB flash header a octal PSRAM config. PCNT-on guard navíc přeložen jako samostatný target objekt; celý PCNT-on image/HW tím testovaný není. Všech92 fixture má původní SHA256. Historické port-* logy zůstávají výsledkem předchozího portu.

Windows nový test_program_metadata.exe i po uživatelem potvrzeném povolení COMODO vrací WinError5. Nastavení ochrany se neměnilo; Linux sanitizer běh je alternativní důkaz. MinGW5.1 sanitizéry nemá. [Metodika/test report](docs/TEST_REPORT.md), [parita](docs/PARITY_MATRIX.md), [časování](docs/TIMING_PORT.md), [pinmap/S1](docs/HARDWARE.md), [souhrn/hash](docs/validation/ui-metadata-summary.json), [OLED náhledy](docs/validation/ui/record.png).

## Zbývá skutečný HW — NEPROVEDENO

HW není připojený, žádný flash. U1/U2 jsou neinvertující podle uživatele, přesné typy/napájení/reset/power-off nezměřené. Ověřit mechanický S1 kontakty/polohy, boot N16R8/PSRAM a OLED. Sharp LOAD/SAVE všech17 módů a ZX TAP4, analyzátor READ/WRITE/MOTOR/SENSE, PCNT/MCPWM s generátorem za SD/OLED/cache zátěže včetně timestamp jitteru. RMT source-clock pause potřebuje důkaz retence rozpracovaného symbolu/DMA; host mock tento předpoklad pouze simuluje. SD removal/reinsert/full/readonly a S1 během všech transportních/finalize stavů. 30–60min heap minima/largest block, task stack high-water, PSRAM, SD latency a FIFO watermark stress. Statický footprint ani shodné PCNT počty tyto důkazy nenahrazují.
