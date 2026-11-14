# Aktuální/celkový čas – r25

Dle upřesnění uživatele zůstává rozložení z r24. Vlevo na spodním řádku je při známé délce aktuální/celkový čas (`00:38/02:00`), při neznámé délce procenta. Po dokončení například `02:00/02:00`. Dlouhé časy mají formát `2h00/5h00`. BUF zůstává pevně na x=80 a y=56; šestý řádek zůstává pro stav a varování.

Výpočet dostupnosti délky zachován: WAV, TAP a Sharp se známou úplnou délkou používají čas; LEP/L16, odložená délka MZF a UL používají procenta. Bez nové analýzy souboru.

Pixelové testy test_oled_ui s GCC14 ASan/UBSan a cílový PlatformIO build PASS. [Náhled](validation/current-total-preview.png). SHA-256 firmwaru `4a8909b8f295aeffe6e3e6eeb52169bf96c2868ba202455d5d38083088a7540b`, 462896 B. Bez nahrání do desky a HW ověření. Bez commitu a vytvoření větve.
