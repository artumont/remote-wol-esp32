#include "cloudflare.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cloudflare_stream";

void cloudflare_stream_task(void *pvParameters) {
  char url_buf[128];
  snprintf(url_buf, sizeof(url_buf), "http://%s/stream", WOL_HOST);

  esp_http_client_config_t config = {
      .url = url_buf,
      .event_handler = http_stream_event_handler,
      .is_async = false,
      .timeout_ms = 60000,
      .skip_cert_common_name_check = true,
      .crt_bundle_attach = NULL,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);

  esp_http_client_set_header(client, "X-WOL-Secret", WOL_SECRET);
  esp_http_client_set_header(client, "Accept", "text/event-stream");
  esp_http_client_set_header(client, "Connection", "keep-alive");

  while (1) {
    ESP_LOGI(TAG, "Connecting to Cloudflare push stream: %s", url_buf);

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
      ESP_LOGI(TAG, "HTTP Stream ended gracefully or timed out.");
    } else {
      ESP_LOGE(TAG, "HTTP Stream connection failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Retrying stream connection in 10 seconds...");
    vTaskDelay(pdMS_TO_TICKS(10000));
  }

  esp_http_client_cleanup(client);
  vTaskDelete(NULL);
}

static esp_err_t http_stream_event_handler(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
  case HTTP_EVENT_ON_DATA:
    if (evt->data_len < 0)
      break;

    size_t chunk_size = evt->data_len;

    char *chunk = (char *)malloc(chunk_size + 1);
    if (chunk == NULL)
      return ESP_ERR_NO_MEM;

    memcpy(chunk, evt->data, chunk_size);

    chunk[chunk_size] = '\0';

    ESP_LOGD(TAG, "Received stream data: %s", chunk);

    handle_cloudflare_operation(chunk);
    free(chunk);
    break;
  case HTTP_EVENT_DISCONNECTED:
    ESP_LOGE(TAG, "HTTP Stream disconnected");
    break;
  case HTTP_EVENT_ERROR:
    ESP_LOGE(TAG, "HTTP Stream experienced an error");
    break;
  default:
    break;
  }
  return ESP_OK;
}

void handle_cloudflare_operation(const char *operation) {
  if (strcmp(operation, OPERATION_HANDSHAKE) == 0) {
    ESP_LOGI(TAG, "Successfully connected to the cloudflare event stream");
  } else if (strcmp(operation, OPERATION_WAKE)) {
    ESP_LOGI(TAG, "Got wake operation, proceeding with WOL...");
    send_wol_packet(TARGET_MAC);
  } else {
    ESP_LOGW(TAG, "Unknown operation received: %s", operation);
  }
}

void convert_mac_string_to_uint(const char *mac_str, uint8_t *mac_array) {
  unsigned int mac_bytes[6];

  int parsed =
      sscanf(mac_str, "%2x:%2x:%2x:%2x:%2x:%2x", &mac_bytes[0], &mac_bytes[1],
             &mac_bytes[2], &mac_bytes[3], &mac_bytes[4], &mac_bytes[5]);

  if (parsed != 6) {
    sscanf(mac_str, "%2x%2x%2x%2x%2x%2x", &mac_bytes[0], &mac_bytes[1],
           &mac_bytes[2], &mac_bytes[3], &mac_bytes[4], &mac_bytes[5]);
  }

  for (int i = 0; i < 6; i++) {
    mac_array[i] = (uint8_t)mac_bytes[i];
  }
}

void send_wol_packet(const char *mac_address) {
  uint8_t wol_packet[102];
  int packet_index = 0;

  for (int i = 0; i < 6; i++) {
    wol_packet[packet_index++] = 0xFF;
  }

  uint8_t target_mac[6];
  convert_mac_string_to_uint(TARGET_MAC, target_mac);
  for (int j = 0; j < 16; j++) {
    for (int k = 0; k < 6; k++) {
      wol_packet[packet_index++] = target_mac[k];
    }
  }

  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
    return;
  }

  int broadcast_permission = 1;
  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast_permission,
                 sizeof(broadcast_permission)) < 0) {
    ESP_LOGE(TAG, "Failed to set socket options: errno %d", errno);
    close(sock);
    return;
  }

  struct sockaddr_in dest_addr;
  dest_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(9);

  int err = sendto(sock, wol_packet, sizeof(wol_packet), 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err < 0) {
    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
  } else {
    ESP_LOGI(TAG, "Wake-On-LAN Magic Packet sent successfully");
  }

  close(sock);
}
