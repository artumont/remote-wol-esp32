#include "esp_http_client.h"

void cloudflare_stream_task(void *pvParameters);

void convert_mac_string_to_uint(const char *mac_str, uint8_t *mac_array);
void send_wol_packet(const char *mac_address);
