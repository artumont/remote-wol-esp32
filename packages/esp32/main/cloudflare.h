#include "esp_http_client.h"

#define OPERATION_HANDSHAKE "HANDSHAKE"
#define OPERATION_WAKE "WAKE"

static esp_err_t http_stream_event_handler(esp_http_client_event_t *evt);

void handle_cloudflare_operation(const char *operation);
void cloudflare_stream_task(void *pvParameters);

void convert_mac_string_to_uint(const char *mac_str, uint8_t *mac_array);
void send_wol_packet(const char *mac_address);
