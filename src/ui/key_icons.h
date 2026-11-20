/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/* Private one-cell OLED glyphs: never UTF-8 bytes or source metadata. */
enum { UI_KEY_PLAY_CODE=1, UI_KEY_STOP_CODE, UI_KEY_REC_CODE, UI_KEY_PREV_CODE, UI_KEY_NEXT_CODE };
#define UI_KEY_PLAY "\001"
#define UI_KEY_STOP "\002"
#define UI_KEY_REC  "\003"
#define UI_KEY_PREV "\004"
#define UI_KEY_NEXT "\005"
#define UI_HELP_OK_CANCEL UI_KEY_PLAY " OK  " UI_KEY_STOP " CANCEL"
