# Diagnostika chybějícího READ signálu

Uživatel hlásí chybějící signál a pouze lupání odposlechu. Dodaný opakovaný vzorek:

    diag: MOTOR6=1/ON PLAY_CTRL=MOTOR WAIT=0 S1_21=0 EXT=0 SD=1 PLAY=3 PAUSE=0 REC=0 REC_CTRL=0
    diag: TX running=0 paused=0 fifo=4095/4095 reserve_us=21141904 underrun=0 error=none

V aktuálním player_state_t je3=STOPPED,1=PLAYING,2=PAUSED,7=READY. V zachyceném okamžiku tedy neběží TX. Vzorek neprokazuje, co se dělo před STOP, ani příčinu chybějícího signálu při aktivním PLAY. Uživatel zároveň uvádí pohyb pruhu na OLED; časové přiřazení výpisu k pohybu zatím není potvrzené.

## Opravená chyba

Výchozí RMT raw_pulse_stop nechával source FIFO a queued_ticks naplněné. Neaktivní driver pak vracel4095 událostí a přes21s rezervy, přestože už nic nevysílal. STOP nyní po zastavení SDK/odpojení callbacků zahodí nesent FIFO a časovou rezervu, stejně jako existující GPTimer backend. Dokončený čas a source byte counter zůstávají pro finální player snapshot. Pauza dál zachovává frontu a rozpracovaný interval; tato změna se týká pouze STOP.

Regresní test naplní4095 položek o5000us, spustí TX, zastaví ho a vyžaduje fifo0/reserve0, idle pin, plnou kapacitu a nemožnost restartu bez nových dat. Existující pause/retention, waveform/EOF a replay testy zůstávají.

## Nová diagnostika TX-TRACE r3

Banner: MZCMTEmu TX-TRACE r3 S1 HIGH=EXT LOW=INT 2026-11-13. Testovací inverze S1 zůstává dle uživatele; GPIO6/MOTOR ON=HIGH beze změny.

- PLAY se vypisuje číslem i názvem, například PLAY=3/STOPPED nebo PLAY=1/PLAYING.
- STOP_CAUSE uchovává poslední explicitní důvod: S1_EXTERNAL, BUTTON_STOP, BUTTON_STOP_LONG, SD_REMOVED, SD_REINIT, SD_RESTORE_FAILED, RECORD_START nebo API_STOP. Při novém startu se resetuje na NONE.
- Každé explicitní zastavení vypíše player: STOP cause=... from=... path=...; všechny koncové stavy vypíší finish from→to a completed_us. Událost TX STARTED potvrzuje úspěšný návrat ze start API. Pravidelný diag obsahuje také název souboru FILE.
- RMT enc_cb počítá volání encoderu, done počet přijatých SDK TX-done callbacků a encoded_us délku dat předaných encoderem. Po STOP zůstávají pro diagnostiku, další init je resetuje. enc_cb>0 může vzniknout už při počátečním předplnění; sledovat růst za aktivního přenosu, nikoli jen jednu nenulovou hodnotu. Ani callback není měření výstupního padu.
- Při přechodu do PLAYING se jednou vypíše skutečná GPIO4 konfigurace pomocí SDK gpio_dump_io_configuration: output enable a matrix output signal. Jde o čtení registrů, žádné přepojení GPIO nebo nový periferní čítač. Dump sám neměří hrany na vodiči.

Žádný printf/dump neběží v ISR. ISR přibyly pouze32bit inkrementy po callbacku, nikoli po hraně. GPTimer fallback vrací rmt=0 a nulové RMT diagnostické počty. Foreground heartbeat a rate limit zůstávají.

## Fyzická trasa S1

Testovací softwarová inverze nemění mechanické kontakty. Jestliže deska odpovídá schématu, GPIO21 LOW znamená kontakt A/EXT a interní READ přes U2 je od K1/8 odpojený. Obrácený firmware tuto polohu považuje za INTERNAL. Proto může být nutné samostatně ověřit skutečný GPIO4 průběh a konektorovou trasu; nepřítomnost tónu v analogovém odposlechu sama neprokazuje chybějící průběh na MCU. Mapu fyzických kontaktů nebylo v tomto zásahu možné proměřit a tato možnost není potvrzená příčina uživatelova problému.

## Ověření a další reprodukce

Prošlo5 dotčených sad s GCC14.2 ASan/UBSan: test_raw_rmt, test_player_rmt (17 Sharp módů,4 TAP rychlosti,WAV/L16), test_player_sharp, test_player_tap (WAV4 sample rates/stop/underrun/replay), test_route_switch (obrácený S1). Ověřuje se také STOP_CAUSE=S1_EXTERNAL a reset důvodu při replay. [Host log](validation/tx-trace-host.txt), [target build](validation/tx-trace-build.txt). Předchozí24sadový průchod zůstává historický, nebyl zde znovu vydáván za nový kompletní běh.

Na novém obrazu zachytit v COM6/115200 souvislý výpis od player: request přes TX STARTED/GPIO4 dump až po první STOP/finish a několik diag při skutečně aktivním přehrávání. FILE už identifikuje formát. Při PLAY=STOPPED je nejprve potřeba vysvětlit STOP_CAUSE; při PLAYING/running1 a rostoucích enc_cb řešit skutečné GPIO4 a mechanickou trasu. Samotná konzervativní wall-time progress indikace nenahrazuje analyzátor. Nový obraz nebyl tímto zásahem flashován; chybějící HW signál zatím není uzavřený jako vyřešený.

Finální target build: RAM155532 B, aplikace467709 B, image468128 B; [hash/souhrn](validation/tx-trace-summary.json). Žádný commit/staging/nová větev/push.

Uživatel následně upřesnil, že výpis byl získán z několika minut provozu. To potvrzuje dlouhodobě zachycený STOPPED stav; soubor/formát ani úplný úsek od startu nebyly uvedeny. Nový STOP_CAUSE má tuto mezeru v diagnostice odstranit.
