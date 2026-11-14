# Předplnění WAV pro RMT – r28

Uživatel hlásil okamžitý UNDERRUN souboru Turbo Copy V1.22.WAV (PCM8 mono 44 kHz). Výpis ukazuje jediný encoder callback a ukončení po 371519 us, přestože FIFO obsahovalo další data.

Původní WAV předplnění končilo po dosažení 300 ms zdrojového času. Ticho se skládá z řídkých intervalů po nejvýše 256 vzorcích; při 44,1 kHz původní kód připravil jen 64 intervalů. RMT načítá dopředu 128 symbolů. Encoder tak při startu dosáhl konce dosud naplněného FIFO, vložil terminátor a další doplnění už ukončenou transakci nezachránilo. Chyba reprodukována novým host testem ještě před změnou produkčního kódu: [původní selhání](validation/wav-prefill-before.txt).

Schválená oprava ponechává časový práh 300 ms a vyžaduje současně alespoň 256 intervalů před startem (nebo konec souboru). Horní mez 3072 intervalů a ukončení při chybě/EOF zůstávají. Tím se tichý úvod nevejde celý do počátečního načtení DMA. Žádné doplňování umělého signálu ani maskování skutečného underrunu.

Ověření s GCC14 ASan/UBSan: test_player_rmt, test_raw_rmt a test_player_tap PASS. Nové player/RMT případy obsahují tři sekundy ticha a dvousekundové ticho následované signálem pro 44,1/48/96 kHz; doplňování po 10 ms, otevřené kódování po startu, přesná fyzická délka a EOF100. Existující testy pokrývají krátký WAV, čtyři vzorkovací frekvence GPTimer, hysterezi, inverzi, pauzu, skutečný underrun, TAP, LEP/L16 i 17 Sharp režimů. Cílový PlatformIO build PASS.

Firmware 462880 B, SHA-256 `1ad1ebed6933bbe6be8a025a6c70157a544b7d133d38a09ef2243a93a303de8a`, banner TX-TRACE r28 WAV PREFILL. Bez nahrání do desky; skutečný uživatelův soubor ani HW zatím nebyly ověřeny. Bez commitu a vytvoření větve.
