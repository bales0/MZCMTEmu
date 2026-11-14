# Automatický SENSE – IC 1:4

Uživatel upřesnil, že testuje IC 1:4 a musí ručně přepnout SENSE tam/zpět před načtením a znovu před payloadem. Jde o nativní pulzní přenos, ne UL handshake. Ruční sepnutí S1 do EXT uzemní konektorový SENSE; jeho účinek odpovídá požadované aktivní nízké úrovni. Samotný průběh na konkrétní desce nebyl změřen.

Předchozí výslovně vyžádaná testovací inverze nastavovala při PLAY GPIO7 HIGH a v klidu LOW. Pro aktuální opravu automatického načítání byla vrácena aktivní LOW a klidová HIGH podle zapojení. SENSE zůstává LOW při čekání MOTOR, PLAY i pauze MOTOR, také mezi hlavičkou a payloadem. Není přidáno periodické pulzování ani vazba každé změny MOTOR na přepínání SENSE. Armování před přípravou souboru a prvním READ zůstává zachováno. S1 software stále ignoruje; mechanicky musí být v interní poloze. Žádná změna READ/WRITE/MOTOR polarity nebo NVS loaderu.

Dále opravena nezávislá chyba souběhu: main dříve testoval vlastnictví UL a zapisoval GPIO7 bez společného zámku s player task. UL mohl získat řízení mezi kontrolou a zápisem. Nová player_set_sense_level kontroluje vlastnictví a zapisuje pod mutexem přehrávače. Zastaralý požadavek z hlavní smyčky během UL vrátí false a nezmění SENSE. Toto není vysvětlení IC 1:4, kde UL neprobíhá, ale odstraňuje skutečnou možnost narušení handshake.

## Ověření

[Build](validation/sense-auto-build.txt) PASS, [pět host sad GCC ASan/UBSan](validation/sense-auto-host.txt) PASS. Rozšířená test_player_sharp aplikuje automatickou politiku přes všechny nativní oblasti všech 17 profilů, ověřuje LOW v pauze MOTOR v IC 1:4 a odmítnutí opačné úrovně během aktivního UL. MZ700 simulátor protistrany byl opraven, aby počáteční uvolnění SENSE před armováním READ nepovažoval za přenášený datový pár; původní simulátor byl s nově testovanou aktivní LOW před UL nekonzistentní. Test prošel až s touto opravou simulace, skutečný loader FSM nebyl kvůli tomu měněn.

Testy simulují automatickou politiku a protistranu; neměří 8255 ani skutečný SENSE přes U2/S1. Chování desky po opravě není potvrzené. [Identifikace obrazu a SHA256](validation/sense-auto-summary.json).

Banner: **MZCMTEmu TX-TRACE r6 SENSE AUTO LOW S1 BYPASS INTERNAL 2026-11-13**. Pro opakování uživatelského případu ponechat IC 1:4 a S1 v interní poloze. Při WAIT/PLAY/PAUSE má diagnostika ukazovat SENSE7_POLICY=LOW/ACTIVE; po STOP HIGH/IDLE. Pokud přepínání stále pomáhá, porovnat tento požadavek se skutečným napětím GPIO7 a SENSE konektoru. Bez těchto měření nelze potvrdit průchod výstupu deskou ani přesný motorový průběh.

Bez nahrání firmwaru, stagingu, commitu, větve nebo push. Starší reporty r5 a dočasná inverze jsou historie. Podklady pro úrovně: [audit polarit](SIGNAL_POLARITY_AUDIT.md).
