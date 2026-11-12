#pragma once

#include <stdbool.h>

void sdcard_init(void);
void sdcard_unmount(void);
bool sdcard_is_mounted(void);

// ponecháno kvůli kompatibilitě se starším kódem,
// ale už nedělá zapisovací test do souboru
int sdcard_test(void);