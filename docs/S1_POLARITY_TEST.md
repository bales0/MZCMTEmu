# Dočasný test obrácené detekce S1

Na výslovnou žádost uživatele je v tomto testovacím buildu GPIO21 HIGH=EXTERNAL a LOW=INTERNAL. ROUTE_EXTERNAL_LEVEL_HIGH=1 v src/app/route_switch.h je jediné místo polarity; běžný40ms debounce a přerušení analýzy používají tutéž funkci route_switch_is_external. Původní mapování podle schématu je HIGH=INTERNAL, LOW=EXTERNAL (konstanta0).

GPIO6/MOTOR zůstává samostatný vstup s ON=HIGH. Raw S1_21 a MOTOR6 v diagnostice se neinvertují; mění se interpretace EXT a navazující blokování/ukončení interního transportu. Mechanické kontakty přepínače a SENSE spojení se tím nemění. Banner testovacího obrazu: MZCMTEmu S1-TEST HIGH=EXT LOW=INT 2026-11-13.

Jde o testovací odchylku od pinového auditu, nikoli změnu pinů ani potvrzení příčiny změn MOTOR. Build není flashován automaticky, nevytváří se commit/větev. Návrat původní polarity: ROUTE_EXTERNAL_LEVEL_HIGH=0 a obnovení běžného boot banneru.

Ověřeno: test_route_switch v Debian GCC14.2 s ASan/UBSan PASS (nová polarita,40ms debounce,bounce), cílový build ESP-IDF6.0.1 PASS. [Host log](validation/s1-invert-host.txt), [build](validation/s1-invert-build.txt). RAM155516 B, aplikace464721 B. HW tohoto obrazu neověřeno.
