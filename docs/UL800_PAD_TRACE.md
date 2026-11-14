# UL800 r12: kontrola polarit a záznam skutečných padů ESP

## Co prokázal uživatelský r11 log

Tc122.mzf, UL MZ800, 7083 bajtů, BEGIN read_setup_us2 sense_hold_us50. Selhání PAIR_WRITE_HIGH expect_WRITE1 WRITE5=0 pair3 READ4_CMD0 SENSE7_CMD0 write_edges31 sent3/7083 elapsed_us1219745. Držení50us tedy **nevyřešilo skutečný HW problém**. Starý r10 model s okamžitým ACK ani r11 model s opožděnou připraveností neověřoval vykonávání celého přijímače. Shodná fáze simulovaného timeoutu neprokázala příčinu chyby na desce. MOTOR v logu přechází do OFF; softwarový player tuto změnu v UL ignoruje, vliv skutečného motorového/SENSE obvodu MZ není vyloučen.

## Nezávislejší kontrola přijímače

Nová test_ul_z80 přímo vykonává relevantní instrukce skutečně generovaného UL800 přijímače LOW i HIGH. Obsahuje registr BC/DE/HL i jejich alternativní sadu, stack, flag Z, skoky a časování použitého podmnožinového Z80 jádra. ROM inicializace a bankování se přeskakují; HIGH relokace je v modelu provedena před začátkem přijímací smyčky.

Při explicitně zadaném ideálním mapování PC5=!READ, PC4=!SENSE a WRITE=!PC1 přijímač dokončí všech256hodnot bajtu ve správném pořadí při2us setup a50us hold. Opačné kombinace SENSE/WRITE neprojdou nebo dostanou jiná data. Tento model stanovuje předpoklady převodu na portové bity; **neměří a nedokazuje, že je splňuje aktuální Sharp/deska**. Analogový a motorový obvod ani úplný stroj se neemuluje. Dřívější tvrzení o přímém bezpodmínečném propojení SENSE→PC4 bylo v auditu již opraveno; konektorovou úroveň nelze prostě zaměnit za portový bit.

r12 neobrací žádný signál a nezvyšuje další prodlevy. Zůstává setup2us/hold50us pro porovnatelnost s r11. Normální PLAY SENSE LOW, idle HIGH a S1 bypass zachovány.

## Nová diagnostika

UL nastaví GPIO4/7 na INPUT_OUTPUT: výstupy nadále řídí, vstupní část umožní přečíst skutečnou úroveň na padech MCU. GPIO6 zůstává vstup MOTOR. To je čtení na3,3V straně ESP, nikoli měření5V strany U2, konektoru nebo8255.

Ring buffer32posledních změnových událostí ukládá čas, sent, pair, fázi, očekávané WRITE, vzorkované WRITE5/MOTOR6 a READ4/SENSE7 jako příkaz i přečtený pad. Zápis do UART nastane až po chybě a uvolnění sběrnice, takže netiskne během datového handshake. Po ul: FAIL následuje ul: TRACE a ul: edge. R4=cmd/pad a S7=cmd/pad umožní odlišit neodpovídající pad od pouze správného softwarového požadavku. Shoda cmd/pad stále neprokazuje průchod přes převodník/S1 do Sharpu. Vzorkování není logický analyzátor a může minout krátké špičky.

BEGIN doplněn o LOAD, EXEC a receiverLOW/HIGH, aby se výběr umístění loaderu nemusel odhadovat podle názvu souboru. Potřebný další podklad je jeden nový BEGIN, FAIL a navazujících nejvýše32řádků ul: edge ze stejného Tc122.mzf. Pokud cmd/pad souhlasí, další rozlišení signálové cesty a vstupu8255 už vyžaduje měření na straně Sharpu.

## Ověření

[Build ESP32-S3](validation/ul-pad-trace-build.txt) PASS. [4 sady GCC ASan/UBSan](validation/ul-pad-trace-host.txt) PASS: nová instrukční test_ul_z80, player/RMT, player/Sharp a Sharp stream. Dosavadní nezávislé dekódování44033bajtů zachováno. Test blokovaného peeru nyní také vydá ukázku journalu s cmd/pad; je to host simulace, ne HW výpis. [Velikost a SHA256](validation/ul-pad-trace-summary.json).

Banner: **MZCMTEmu TX-TRACE r12 UL PAD TRACE S1 BYPASS INTERNAL 2026-11-13**. Bez flash, stagingu, commitu, větve nebo push. UL800 na HW zůstává nevyřešený; r12 je diagnostický obraz, nikoli prohlášená oprava závady.
