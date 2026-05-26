#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "cli_handler.h"

void app_main(void)
{
    ESP_LOGI(__func__,"SYS INIT ...");

    

    ESP_ERROR_CHECK(init_cli_handler());
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
