#pragma once

#include "esp_err.h"
#include "render.h"

esp_err_t network_start(void);
posiflex_network_status_t network_status(void);
