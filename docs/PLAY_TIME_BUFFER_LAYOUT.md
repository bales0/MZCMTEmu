# Čas přehrávání a pevný BUF – r24

Schválená úprava ze 14. 11. 2026 nahrazuje TX na spodním řádku údajem o přehrávání.

- Při známé délce vlevo `00:38/01:22`: uplynulý / zbývající čas. Zbývající čas je nejméně nula. Délky od 100 minut používají kompaktní hodiny/minuty.
- Při neznámé délce vlevo procenta: LEP/L16, MZF s odloženým výpočtem délky a UL. UL délka hlavičky není délkou celého přenosu. Rychlý start MZF zůstává zachován.
- BUF začíná vždy na x=80, y=56; procenta mají tři znakové pozice. Text se neposouvá při 0/7/73/100 %. Neplatný buffer má na stejné pozici pomlčky.
- TX a TIME odstraněny z obrazovky přehrávání. Řádek y=40 vyhrazen pro stav QD a varování; RECORD zachovává čas a RX FREE / MIN. Sériová diagnostika TX zůstává dostupná.

Čtyři host sady s GCC14 ASan/UBSan a cílový build PASS. Pixelové kontroly: pevné BUF, čas/zbývající čas, dokončení, pauza, LEP procenta, chybějící buffer, dlouhé trvání. Dodatečný skutečný player/RMT test LEP a L16 potvrzuje neznámou délku a dokončení LEP s 100 %. [Náhled](validation/play-time-preview.png): PLAY, FINISHED, LEP, QD pauza.

Firmware 462944 B, SHA-256 `d66d5a4e69498d43b346a4e1d52521c8c623b6af9c7320c25dc2ca45ab3fd5df`, banner `TX-TRACE r24 PLAY TIME`. Bez nahrání do desky a HW ověření, bez commitu a vytvoření větve.
