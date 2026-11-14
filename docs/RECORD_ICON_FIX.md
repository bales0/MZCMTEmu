# Ikona RECORD a MIN – r27

Schválená úprava: RECORD má vyplněný kruh 7×7 s oříznutými rohy, STOP zůstává čtverec 5×5. RX MIN zkráceno na MIN. Rozložení, časy a význam BUF zachovány.

Pixelové kontroly celé ikony RECORD, čtverce STOP a textu MIN: test_oled_ui s GCC14 ASan/UBSan PASS. Cílový PlatformIO build PASS. [Náhled RECORD/SAVED](validation/record-icon-preview.png).

Firmware 462896 B, SHA-256 `ade69ad5665a57ccfd670aac3b6e060e817bcd644b978e48eee00ad14c8dbf4d`, banner TX-TRACE r27 REC ICON. Bez nahrání do desky a HW ověření. Bez commitu a větve.

Nově nahlášený okamžitý underrun WAV je samostatně rozpracovaná diagnostika; tato revize WAV nemění.
