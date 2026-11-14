> Historie r13: následný HW log neprošel –200us limit ukončil první ACK (pair0/sent0). Uživatel požaduje přesnou sekvenci SD2CMT2; [aktuální r14](UL_SD2CMT2_REFERENCE.md) tento limit, přidané prodlevy i IRQ kritický úsek odstraňuje.

# UL800 r13: nevydávat procesor během SENSE LOW

## Nový HW důkaz a referenční porovnání

Uživatel potvrdil, že původní SD2CMT2 UL funguje na témže Sharpu. Nový log Flappy.mzf ukazuje LOAD/EXEC1E00h a receiverLOW, takže payload nezačíná v oblasti headerového receiveru1110h. Příkaz a pad GPIO4/7 se ve všech záznamech shodují. BEGIN 44033bajtů; FAIL PAIR_WRITE_HIGH, pair1, sent0, WRITE0.

Poslední kroky: u701009us byl stav PAIR_WRITE_LOW se SENSE HIGH; první SENSE LOW je následně potvrzen až v záznamu709603us (WRITE1/MOTOR1). Po709693us je SENSE HIGH a WRITE0/MOTOR0. U709754us přijde další SENSE LOW, ale MOTOR/WRITE zůstávají0 až do timeoutu. Časová mezera činí přibližně8,6ms. Z journalu nelze přesně rozlišit celou délku jednotlivých instrukcí od přerušení plánovačem, ale interval je mnohonásobně delší než nastavené50us.

r12 servis mohl vrátit řízení v libovolné fázi; také preempce mezi kroky prodloužila LOW libovolně. Referenční AVR send_byte_legacy naopak souvisle čeká WRITE a dokončí následující READ/SENSE krok v blokující smyčce. Nebyl důvod obracet polarity globálně.

[Sharp Service Manual](https://www.idealine.info/sharpmz/mz-800/download/sm800.pdf), funkční obrázek str30 a CPU obvod str44, ukazuje SENSE jako vstup do klopného/motorového obvodu, nikoli libovolně rychlý přímý hodinový vstup PC4. Ve tvarovači jsou R69 220ohm a C70 22uF (součin přibližně4,84ms). Z dlouhého LOW následovaného krátkým HIGH proto nelze automaticky odvozovat stejnou reakci jako z krátkých pulzů referenčního kódu. Jde o vysvětlení podpořené schématem a časovou souvislostí v logu, nikoli přímé měření analogového uzlu nebo plánovače.

## Oprava v r13

- Zdrojový bajt se nadále načítá v běžném popředí, když je SENSE HIGH.
- Jakmile je připraven krok do LOW, nový finish_low_clock uzamkne krátký portMUX kritický úsek ještě před změnou READ/SENSE.
- Nastaví READ, ponechá2us setup, nastaví SENSE LOW a čeká jen na ACK WRITE HIGH s limitem200us. Následující krok FSM PAIR_WRITE_HIGH pouze posune data/čítače; nenačítá soubor.
- Nastaví druhý READ bit, po2us vrátí SENSE HIGH a teprve potom odemkne. Ani chyba bez ACK nesmí opustit kritický úsek s LOW.
- Min50us držení HIGH probíhá už mimo kritický úsek. Plánování, čtení SD a ostatní dlouhé operace tedy mohou prodloužit HIGH, ne neomezeně LOW.

Uvnitř kritického úseku není UART, SD, alokace ani čekání na FreeRTOS. Krátký polling má limit; chybějící ACK se publikuje jako immutable UL ACK WINDOW, po uvolnění kritického úseku se zapíše trace a výstupy se ukončí bezpečně. Původní5s čekání na první odpověď zůstává neblokující a není uvnitř kritického úseku. Servisní rozpočet1500us/512kroků zůstává, kontroluje se mezi dokončenými HIGH kroky.

Polarity READ/WRITE/SENSE ani loaderové bajty nebyly změněny. Běžný PLAY SENSE LOW, klid HIGH, S1 bypass a řízení MOTOR jsou zachovány. Jde o opravu způsobu obsluhy hodin v ESP portu, ne o požadavek na úpravu Sharpu.

## Regrese

Host model byl rozšířen o předčasné WRITE ACK,20us kladnou dobu odpovědi,1ms plánování a vložené8,6ms pozastavení. Dlouhé LOW vybije modelovaný tvarovač; krátké HIGH neumožní následující LOW rozpoznat. To je zjednodušený fault model, nikoli SPICE měření všech analogových parametrů konkrétního Sharpu.

[Negativní test s vypnutou ochranou](validation/ul-atomic-unprotected.txt) skončil PAIR_WRITE_HIGH, pair1, sent0 a low_cmd_max_us17203. Ochrana je v tomto pokusu vypnuta výhradně host testovacím makrem CMT_UL_UNPROTECTED_TEST; cílový obraz obsahuje ochranu zapnutou. [Stejný chráněný test](validation/ul-atomic-host.txt) dokončí všech44033bajtů a176132hodinových oken, nezávisle dekóduje a porovná všechny bajty. Vkládá přes100přerušení při HIGH; žádný datový LOW není nechráněný. Host naměřil low_cmd_max_us22 při20us odpovědi. Zkontrolována návratová HIGH při každém kritickém odemknutí a simulovaná délka kritického úseku do220us.

Zvláštní test chybějícího datového ACK ověřuje ukončení krátkého čekání, návrat SENSE HIGH před odemknutím, bezpečné READ/MONITOR a zachování přesného UL ACK WINDOW i po close/reset adaptéru. Host low_cmd_max_us202 při této chybě zahrnuje2us finálního setup; nejde o měření MCU. Čtyři sady GCC ASan/UBSan PASS: player/RMT, player/Sharp, Sharp stream a instrukční test generovaných LOW/HIGH receiverů se všemi256hodnotami bajtu. Instrukční test stále explicitně idealizuje převody na PC4/PC5; nová fault simulace není úplný Sharp.

[Target build](validation/ul-atomic-build.txt) PASS; [velikost a SHA256](validation/ul-atomic-summary.json). Obraz nebyl nahrán a oprava dosud není potvrzena na HW.

## Následující HW ověření

Banner **MZCMTEmu TX-TRACE r13 UL ATOMIC CLOCK S1 BYPASS INTERNAL 2026-11-13**; BEGIN navíc low_ack_us200. END i FAIL nyní obsahují clock_windows a low_cmd_max_us. Tento údaj je čas softwarového LOW příkazu podle ESP timeru, ne měření hrany za převodníkem. Úspěch má skončit ul: END sent44033/44033 pro Flappy a spuštěním hry; při další chybě zkontrolovat přesný reason a low_cmd_max_us. Bez flash, stagingu, commitu, nové větve nebo push.
