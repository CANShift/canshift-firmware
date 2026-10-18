#pragma once

#include "app_config.h"

void taskUI(void *pvParameters);

#if APP_PROFILE_UI
void reportTaskStackHeadroom();
#endif
