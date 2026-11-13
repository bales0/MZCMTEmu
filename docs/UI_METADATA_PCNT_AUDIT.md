# Read-only audit a implementační plán — 13. 11. 2026

HEAD main d3f67c4a978a38dad23e49c918b145182046452b přesně odpovídá zadání. Před implementací byl pracovní strom čistý ve zdrojích; uživatel odstranil původní zadání a přidal nové. Tyto změny zachovat. Žádný commit, staging, větev, PR ani push nejsou autorizovány. Reference a MZTools zůstanou pouze ke čtení. HW není připojený; LOW=EXT podle dosavadního schématu/kódu nelze označit jako nově fyzicky ověřené.

Potvrzené problémy: main.c nečte overlay při renderu; KEYMAP obsahuje chybný cmt_file_t text; marquee používá uptime; RX pokles je omezen na8 bodů/500ms; browser při selhání load smaže položky a navigate obnoví pouze cestu. Chybí jednotný model programu, TX časová rezerva, samostatné PCNT porovnání a pixelové rozložení. Upřesnění kontroly call-chain: recorder_init v app_main pouze zastavuje; skutečný edge_capture_init volá recorder_start_now z app_task na CPU1. Lokální ESP-IDF mcpwm_cap.c alokuje IRQ přes esp_intr_alloc_intrstatus při register_event_callbacks na tomto jádře. Očekávaná vazba je tedy již CPU1; skutečný runtime core ještě nebyl změřen.

Plán:
1. Sdílený bounded model programu, atomicky publikované snapshoty a extrakce původních Sharp/IC/TC hlaviček před transportní transformací. Rozšířit stávající pasivní AutoName decoder pro PLAY i RECORD bez druhého trvalého payload bufferu; TAP zvlášť ZX/XOR.
2. Oddělená telemetrie RX FIFO/min/sticky chyby a inkrementální TX tick reserve; čisté UI politiky pro zotavení, alarmy, overlay a marquee.
3. Pixelové primitivy nad jediným1024B framebufferem, byte-dirty pages s retry, grafický browser a metadata/bary PLAY/REC; zachovat20Hz/10ms I²C hranice.
4. Browser rollback opětovným načtením původního adresáře bez další2MiB kopie; využít d_type při dostupnosti, dávkovat/cancel, měřit host10000 položek.
5. Volitelný PCNT na GPIO5, obě hrany, bez filtru jako MCPWM. ESP-IDF6.0.1 lokální pulse_cnt.h potvrzuje accum_count/watchpoints a GPIO matrix. Žádný PCNT ISR na každé hraně. Start/end čtení tvoří interval nejistoty; potvrzená neshoda mimo interval je EDGE LOSS, nejistota je diagnostická a nesmí běžně mazat záznam. Testovat wrap/overflow/pending ISR/tail a epochy; neprohlašovat absolutní bezchybnost.
6. Původních20 sad + nové cílené testy, cílový N16R8 build, map/footprint, dokumentace a HW omezení.

## Dokončení pracovní implementace

Plán výše je původní audit před editací, nikoli zbývající návrh. Implementace a automatické ověření jsou popsány v [TEST_REPORT](TEST_REPORT.md) a [HANDOFF](../HANDOFF.md); aktuální HEAD d3f67c4 + pracovní změny,23 sad/ASan/UBSan, target build RAM154348 B/aplikace461513 B. PCNT default off; zapnutá varianta má host test a SDK target object compile. HW není připojený, akceptace na HW zůstává NEPROVEDENO.
