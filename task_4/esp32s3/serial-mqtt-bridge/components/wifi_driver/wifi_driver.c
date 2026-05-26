#include "wifi_driver.h"

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

static const char *TAG = "wifi_driver";

int wifi_connected = 0;

int s_retry_num = 0;

httpd_handle_t g_webserver_handler = NULL; // HTTP server handle

static EventGroupHandle_t wifi_event_group;

const int CONNECTED_BIT = BIT0;

char g_ap_ip_str[16]; // Buffer to store the assigned IP as a string

uint8_t g_got_wifi_data_flag = 0;
uint8_t g_device_connected_flag = 0;

static esp_err_t root_get_handler(httpd_req_t *req);
static esp_err_t wifi_update_handler(httpd_req_t *req);

/**
 * @brief Handles Wi-Fi and IP events.
 *
 * This function is called when certain Wi-Fi or IP events occur, such as disconnection from Wi-Fi or obtaining an IP address.
 *
 * @param arg Pointer to argument, typically not used.
 * @param event_base The base event type.
 * @param event_id The event identifier.
 * @param event_data Pointer to event data, typically not used.
 */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d, reason:%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Station started");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = 1;
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}
/**
 * @brief Initializes Wi-Fi hardware and event handling.
 *
 * This function sets up the Wi-Fi hardware and event handlers, creating the necessary event group and configuring the Wi-Fi driver.
 */
static void initialise_wifi(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    static bool initialized = 0;
    if (initialized)
    {
        stop_wifi_driver();
    }
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());

    initialized = 1;
}

/**
 * @brief Configures the ESP32 as an access point (AP).
 *
 * @param ap_ssid The SSID of the access point.
 * @param ap_passwd The password for the access point. If empty, open authentication is used.
 *
 * @return bool Returns 1 if successful, otherwise 0.
 */
static bool wifi_ap(const char *ap_ssid, const char *ap_passwd)
{
    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.ap.ssid, ap_ssid);
    strcpy((char *)wifi_config.ap.password, ap_passwd);
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.ap.ssid_len = strlen(ap_ssid);
    wifi_config.ap.max_connection = 5;
    wifi_config.ap.channel = 8;

    if (strlen(ap_passwd) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WIFI_MODE_AP started. SSID:%s password:%s channel:%d",
             ap_ssid, ap_passwd, CONFIG_AP_WIFI_CHANNEL);
    // Start the HTTP server
    g_webserver_handler = start_webserver();
    return ESP_OK;
}

/**
 * @brief Configures the ESP32 as a station (STA) and connects to the specified network.
 *
 * @param timeout_ms Timeout in milliseconds for connection attempt.
 * @param sta_ssid The SSID of the network to connect to.
 * @param sta_passwd The password for the network.
 *
 * @return bool Returns 1 if successfully connected, otherwise 0.
 */
static bool wifi_sta(int timeout_ms, const char *sta_ssid, const char *sta_passwd)
{
    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.sta.ssid, sta_ssid);
    strcpy((char *)wifi_config.sta.password, sta_passwd);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                                   pdFALSE, pdTRUE, timeout_ms / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "bits=%x", bits);
    if (bits)
    {
        ESP_LOGI(TAG, "WIFI_MODE_STA connected. SSID:%s password:%s",
                 sta_ssid, sta_passwd);
    }
    else
    {
        ESP_LOGI(TAG, "WIFI_MODE_STA can't connected. SSID:%s password:%s",
                 sta_ssid, sta_passwd);
    }
    return (bits & CONNECTED_BIT) != 0;
}

/**
 * @brief Configures the ESP32 as both an access point (AP) and a station (STA).
 *
 * @param timeout_ms Timeout in milliseconds for connection attempt.
 * @param ap_ssid The SSID of the access point.
 * @param ap_passwd The password for the access point. If empty, open authentication is used.
 * @param sta_ssid The SSID of the network to connect to.
 * @param sta_passwd The password for the network.
 *
 * @return bool Returns 1 if successfully connected, otherwise 0.
 */
static bool wifi_apsta(int timeout_ms, const char *ap_ssid, const char *ap_passwd, const char *sta_ssid, const char *sta_passwd)
{
    wifi_config_t ap_config = {0};
    strcpy((char *)ap_config.ap.ssid, ap_ssid);
    strcpy((char *)ap_config.ap.password, ap_passwd);
    ap_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    ap_config.ap.ssid_len = strlen(ap_ssid);
    ap_config.ap.max_connection = 1;
    ap_config.ap.channel = CONFIG_AP_WIFI_CHANNEL;

    if (strlen(ap_passwd) == 0)
    {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    wifi_config_t sta_config = {0};
    strcpy((char *)sta_config.sta.ssid, sta_ssid);
    strcpy((char *)sta_config.sta.password, sta_passwd);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WIFI_MODE_AP started. SSID:%s password:%s channel:%d",
             ap_ssid, ap_passwd, CONFIG_AP_WIFI_CHANNEL);

    ESP_ERROR_CHECK(esp_wifi_connect());
    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                                   pdFALSE, pdTRUE, timeout_ms / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "bits=%x", bits);
    if (bits)
    {
        ESP_LOGI(TAG, "WIFI_MODE_STA connected. SSID:%s password:%s",
                 sta_ssid, sta_passwd);
    }
    else
    {
        ESP_LOGI(TAG, "WIFI_MODE_STA can't connected. SSID:%s password:%s",
                 sta_ssid, sta_passwd);
    }
    return (bits & CONNECTED_BIT) != 0;
}

/**
 * @brief Initializes the Wi-Fi driver and starts it in the specified mode.
 *
 * This function handles NVS flash initialization, Wi-Fi initialization, and starts the Wi-Fi driver in one of the following modes:
 * - Access Point (AP) mode
 * - Station (STA) mode
 * - Both AP and STA modes (APSTA)
 *
 * @param wifi_op_mode The operation mode for the Wi-Fi driver.
 *                     0 for AP mode, 1 for STA mode, 2 for APSTA mode.
 * @param ap_ssid The SSID for the access point (used only if wifi_op_mode is 0 or 2).
 * @param ap_passwd The password for the access point (used only if wifi_op_mode is 0 or 2).
 * @param sta_ssid The SSID of the network to connect to (used only if wifi_op_mode is 1 or 2).
 * @param sta_passwd The password of the network to connect to (used only if wifi_op_mode is 1 or 2).
 */
void esp_start_wifi_driver(int wifi_op_mode, const char *ap_ssid, const char *ap_passwd, const char *sta_ssid, const char *sta_passwd)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    initialise_wifi();

    if (wifi_op_mode == 0)
    {
        ESP_LOGW(TAG, "Start AP Mode");
        wifi_ap(ap_ssid, ap_passwd);
    }
    else if (wifi_op_mode == 1)
    {
        ESP_LOGW(TAG, "Start STA Mode");
        wifi_sta(5000, sta_ssid, sta_passwd);
    }
    else if (wifi_op_mode == 2)
    {
        ESP_LOGW(TAG, "Start APSTA Mode");
        wifi_apsta(5000, ap_ssid, ap_passwd, sta_ssid, sta_passwd);
    }
    else
    {
        ESP_LOGW(TAG, "No Wifi Mode Selected");
        return;
    }

    while ((wifi_connected != 1) && (wifi_op_mode != 0))
    {
        ESP_LOGI("MAIN", "Connecting Wifi ....");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Retrieve AP IP address
    esp_netif_ip_info_t ip_info;
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"); // Default AP interface
    if (ap_netif && esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK)
    {
        snprintf(g_ap_ip_str, sizeof(g_ap_ip_str), IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "AP IP Address: %s", g_ap_ip_str);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to get AP IP address");
    }
}
/**
 * @brief Stops the Wi-Fi driver and deinitializes it.
 *
 * This function turns off the Wi-Fi driver and frees up the resources.
 */
void stop_wifi_driver(void)
{
    // Stop Wi-Fi if it is currently enabled
    ESP_LOGW(TAG, "Stopping Wi-Fi driver...");

    // Stop the Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_stop());

    // Deinitialize the Wi-Fi
    ESP_ERROR_CHECK(esp_wifi_deinit());

    // Reset the connection status
    wifi_connected = 0;
    ESP_LOGI(TAG, "Wi-Fi driver stopped.");
}

// Function to handle form submission and save SSID/password to NVS
static esp_err_t wifi_update_handler(httpd_req_t *req)
{
    char buf[200];
    int ret, remaining = req->content_len;

    // Read JSON data
    int total_received = 0;
    while (remaining > 0)
    {
        ret = httpd_req_recv(req, buf + total_received, MIN(remaining, sizeof(buf) - total_received - 1));
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                continue; // Retry in case of timeout
            }
            return ESP_FAIL;
        }
        remaining -= ret;
        total_received += ret;
    }
    buf[total_received] = '\0'; // Null-terminate the received data

    // Parse JSON data
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    const cJSON *password = cJSON_GetObjectItem(json, "password");

    if (!cJSON_IsString(ssid) || !cJSON_IsString(password))
    {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid SSID or password");
        return ESP_FAIL;
    }

    // Save to NVS
    // nvs_handle_t nvs_handle;
    // ESP_ERROR_CHECK(nvs_open("wifi_config", NVS_READWRITE, &nvs_handle));
    // ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "ssid", ssid->valuestring));
    // ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "password", password->valuestring));
    // ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    // nvs_close(nvs_handle);
    nvs_save_wifi_credentials(ssid->valuestring, password->valuestring);

    cJSON_Delete(json);

    // Respond with a success message
    httpd_resp_set_type(req, "application/json");
    const char *response = "{\"message\": \"WiFi credentials updated. Rebooting...\"}";
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    g_got_wifi_data_flag = 1;
    return ESP_OK;
}

// Function to serve the Wi-Fi configuration page
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char *html_response =
        "<!DOCTYPE html>"
        "<html>"
        "<head><title>ESP32 WiFi Config</title></head>"
        "<body>"
        "<h1>WiFi Configuration</h1>"
        "<form id=\"wifiForm\">"
        "<label>SSID:</label><br>"
        "<input type=\"text\" id=\"ssid\" required><br><br>"
        "<label>Password:</label><br>"
        "<input type=\"password\" id=\"password\" required><br><br>"
        "<button type=\"button\" onclick=\"submitForm()\">Update WiFi</button>"
        "</form>"
        "<script>"
        "function submitForm() {"
        "  const ssid = document.getElementById('ssid').value;"
        "  const password = document.getElementById('password').value;"
        "  const xhr = new XMLHttpRequest();"
        "  xhr.open('POST', '/update', 1);"
        "  xhr.setRequestHeader('Content-Type', 'application/json');"
        "  xhr.onreadystatechange = function() {"
        "    if (xhr.readyState === 4) {"
        "      alert(xhr.responseText);"
        "    }"
        "  };"
        "  xhr.send(JSON.stringify({ssid, password}));"
        "}"
        "</script>"
        "</body>"
        "</html>";
    httpd_resp_send(req, html_response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Start the HTTP server
httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &root);

        httpd_uri_t update = {
            .uri = "/update",
            .method = HTTP_POST,
            .handler = wifi_update_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &update);

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

// Stop the HTTP server
void stop_webserver(httpd_handle_t server)
{
    if (server)
    {
        httpd_stop(server);
    }
}

// Initialize NVS
esp_err_t nvs_initialize(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS partition needs to be erased");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "NVS initialized successfully");
    }
    return err;
}

// Write SSID and password to NVS
esp_err_t nvs_save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Save SSID
    err = nvs_set_str(nvs_handle, "ssid", ssid);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Save password
    err = nvs_set_str(nvs_handle, "password", password);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "WiFi credentials saved successfully");
    }

    nvs_close(nvs_handle);
    return err;
}

// Read SSID and password from NVS
esp_err_t nvs_read_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Read SSID
    err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_size);
    if (err != ESP_OK)
    {
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(TAG, "SSID not found in NVS");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to read SSID: %s", esp_err_to_name(err));
        }
        nvs_close(nvs_handle);
        return err;
    }

    // Read password
    err = nvs_get_str(nvs_handle, "password", password, &password_size);
    if (err != ESP_OK)
    {
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(TAG, "Password not found in NVS");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to read password: %s", esp_err_to_name(err));
        }
    }
    else
    {
        ESP_LOGI(TAG, "WiFi credentials read successfully");
    }

    nvs_close(nvs_handle);
    return err;
}

// Erase WiFi credentials from NVS
esp_err_t nvs_erase_wifi_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(nvs_handle, "ssid");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "Failed to erase SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_erase_key(nvs_handle, "password");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "Failed to erase password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "WiFi credentials erased successfully");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to commit NVS erase changes: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}