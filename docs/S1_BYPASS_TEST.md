# Dočasné vypnutí detekce S1

Na výslovnou žádost uživatele13. 11. 2026 je ROUTE_FORCE_INTERNAL=1 v src/app/route_switch.h. Firmware po startu i při každé změně GPIO21 zůstává INTERNAL. S1 neblokuje/neukončuje PLAY ani RECORD a neruší analýzu. GPIO21 zůstává vstup s pull-up; raw hodnota je stále v diagnostice. Nevznikl žádný nový mux ani GPIO výstup.

MOTOR6 ON=HIGH, PLAY MOTOR/MANUAL a nezávislé RECORD řízení jsou beze změny. Menu ukazuje CMT:INTERNAL FIXED, volba zobrazí S1 BYPASS INTERNAL. Log má S1_BYPASS=1 a EXT=0 pro obě úrovně S1_21. Banner nového obrazu: MZCMTEmu TX-TRACE r4 S1 BYPASS INTERNAL 2026-11-13.

Mechanické kontakty S1 se softwarovým bypassem nepřepojí. Pro skutečný interní READ na konektoru musí být fyzický přepínač v poloze interního emulátoru; vypnutí detekce nemůže změnit jeho READ/WRITE/SENSE vodiče. Předchozí test logické inverze skončil. Pod bypassem je standardní neaktivní mapování ROUTE_EXTERNAL_LEVEL_HIGH=0; po nastavení ROUTE_FORCE_INTERNAL=0 se vrátí HIGH=INTERNAL, LOW=EXT. K tomuto návratu upravit také test/boot banner, aby odpovídal aktivní konfiguraci.

## Co ukázal poslední uživatelský log

Flappy.mzf se přehrává v mode3, tedy UL MZ800. Encoder callbacks rostou4→67→105, encoded_us dosáhne3620287 a done1 potvrzuje dokončení RMT transakce. GPIO4 má matrix signal84, což je podle skutečné SDK6.0.1 gpio_sig_map.h RMT_SIG_OUT3_IDX; RESERVED odpovídá rezervaci výstupu driverem. To dokládá aktivitu/konfiguraci RMT, nikoli osciloskopicky změřené hrany na GPIO4 nebo na konektoru.

Následné running0 při PLAYING je v UL režimu očekávané: po předběhu vlastní READ/SENSE samostatný UL FSM a monitor je utlumený. Později selže UL komunikace. Časování je slučitelné s čekáním na odpověď Sharpu; přesná chyba ve starém logu byla skrytá generickým SOURCE/TX ERROR. Nově se propagují UL TIMEOUT/READ/SEEK/STATE/CLOCK až do player_error_message/OLED a serialu. Důvod je immutable literál a nezmizí při stream close/reset. Log obsahuje ul: BEGIN mode=... WRITE5=... a ul: FAIL reason=... WRITE5=... sent=... elapsed_us=.... NVS profil nebyl automaticky měněn.

Pro izolované první ověření běžného READ použít v PLAY menu NORMAL1:1; UL vyžaduje odpovídající loader a obousměrnou odpověď. S1 fyzicky na interní trasu. Chybějící analogový tón/READ na skutečné desce zatím není označený jako opravený nebo HW akceptovaný.

## Důkazy

5 sad GCC14.2 ASan/UBSan PASS: test_route_switch (bypass HIGH/LOW/startup), test_sharp_stream (všechny17módové golden waveformy a chyba zachovaná po close), test_player_sharp (navíc zablokovaný MZ800 peer vyvolá konkrétní UL TIMEOUT a safe idle), test_player_rmt (17módů/TAP4/WAV/L16), test_player_tap (WAV4 sample rates/TAP/stop/replay). [Host log](validation/s1-bypass-host.txt), [target build](validation/s1-bypass-build.txt).

Nový obraz nebyl flashován; bez stagingu, commitu, větve nebo push. Předchozí inverzní test a TX-TRACE r3 reporty zůstávají historické.

Finální build: RAM155532 B, aplikace468005 B, image468432 B; [souhrn/hash](validation/s1-bypass-summary.json).
