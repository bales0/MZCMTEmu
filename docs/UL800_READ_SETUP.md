# UL800: zastavení v datové části a odstup READ/SENSE

## Naměřený uživatelský log

Flappy.mzf, UL MZ800, 44033 bajtů. Hlavička RMT dokončená: done1, encoded_us3620287. UL: FAIL UL TIMEOUT phasePAIR_WRITE_HIGH expect_WRITE1 WRITE5=0 pair1 READ4_CMD0 SENSE7_CMD0 write_edges171 sent21/44033 elapsed_us1249664. Úvodní handshake tedy prošel a proběhly odpovědi pro 21 celých bajtů. Nejde o INITIAL_WRITE_LOW ani o samotnou hlavičku. Poslední chybějící odpověď je WRITE HIGH při SENSE LOW. MOTOR OFF nemá v playeru během UL způsobit softwarovou pauzu; tím ale není ověřeno chování motorového/SENSE obvodu uvnitř Sharpu.

Uživatel potvrdil vadný kontakt převodníku jako předchozí problém SENSE. Nepotvrdil přesně opakované sent21/pair1 ani LOAD/EXEC metadata; jeho další upřesnění bylo, že chyba nastává po hlavičce. Z tohoto jednoho podrobného logu nelze rozlišit časování dat/hodin od jiného problému přijímače nebo signálové cesty. Příčina není prohlášena za prokázanou.

## Změna r10

V UL adaptéru po zápisu READ a před změnou SENSE přidán bounded esp_rom_delay_us(2). Čas na ustálení READ je zaručen v softwarové sekvenci před každou změnou hodin SENSE; v pouhém čekání na ACK se prodleva nevkládá. Datová polarita, normální PLAY SENSE, loaderové bajty, počáteční prodlevy a timeouty zůstávají stejné. Předchozí foreground budget 1500 us a 512 kroků na servis zachován, kontrola času probíhá mezi kroky. Nejde o nové dlouhé blokující čekání na Sharp.

Jde o opatření pro ustálení dat před hodinovou hranou na rychlejším MCU; bez měření na desce není doloženo, že právě jeho absence způsobila zaznamenanou chybu. Automatické přepínání SENSE nebylo invertováno ani nahrazeno pulzováním podle MOTOR. S1 bypass zůstává podle žádosti uživatele.

## Regrese a jejich meze

Nový player/RMT test vytvoří MZF s 44033 pseudonáhodnými bajty, load/exec1200h, stejnou velikostí jako uživatelský log. Protistrana nezávisle skládá bajty z každého fyzicky modelovaného READ/SENSE páru, ruší rotaci a inverzi, porovná všechny bajty a ověří >=2 us mezi zápisem READ a každou změnou SENSE. Dojde až na EOF, nikoli na byte21. Test zahrnuje skutečný player, RMT backend a UL adaptér; GPIO/čas/odpověď protistrany jsou simulované, nejedná se o emulaci kompletního Z80/8255/motorového obvodu ani o elektrické měření na konektoru.

[Build ESP32-S3](validation/ul-read-setup-build.txt) PASS. [3 sady GCC ASan/UBSan](validation/ul-read-setup-host.txt) PASS: player/RMT, player/Sharp, Sharp stream. Krátké testy všech17profilů a chyb prvního ACK zachovány. [Velikosti a SHA256](validation/ul-read-setup-summary.json).

Banner: **MZCMTEmu TX-TRACE r10 UL READ SETUP S1 BYPASS INTERNAL 2026-11-13**. Obraz nebyl nahrán. Další ověření je stejné Flappy.mzf/UL MZ800 na Sharpu a porovnání sent/pair/phase, pokud se chyba vrátí. Do té doby jde o připravenou úpravu časování, ne potvrzenou HW opravu. Bez stagingu, commitu, větve a push.
