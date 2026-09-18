#ifndef _REFEREE_MAIN_UI_H_
#define _REFEREE_MAIN_UI_H_

#include "tx_api.h"

/* Starts the UI/key thread. OLED hardware is initialized lazily on wake. */
UINT referee_ui_init(void);

#endif
