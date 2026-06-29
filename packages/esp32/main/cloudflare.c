#include "cloudflare.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cloudflare_poll";

extern const char cloudflare_root_ca_pem_start[]
    asm("_binary_cloudflare_root_ca_pem_start");
extern const char cloudflare_root_ca_pem_end[]
    asm("_binary_cloudflare_root_ca_pem_end");

#define POLL_INTERVAL_MS 60000
#define RESPONSE_BUF_SIZE 256

typedef struct {
  char buffer[RESPONSE_BUF_SIZE];
  int offset;
} response_buf_t;

static esp_err_t poll_event_handler(esp_http_client_event_t *evt) {
  response_buf_t *resp = (response_buf_t *)evt->user_data;
  if (resp == NULL)
    return ESP_OK;

  switch (evt->event_id) {
  case HTTP_EVENT_ON_DATA:
    if (evt->data_len > 0) {
      int remaining = RESPONSE_BUF_SIZE - resp->offset - 1;
      int to_copy = evt->data_len < remaining ? evt->data_len : remaining;
      if (to_copy > 0) {
        memcpy(resp->buffer + resp->offset, evt->data, to_copy);
        resp->offset += to_copy;
        resp->buffer[resp->offset] = '\0';
      }
    }
    break;
  default:
    break;
  }
  return ESP_OK;
}

void cloudflare_stream_task(void *pvParameters) {
  char url_buf[128];
  snprintf(url_buf, sizeof(url_buf), "https://%s/poll", WOL_HOST);

  response_buf_t resp = {0};

  esp_http_client_config_t config = {
      .url = url_buf,
      .event_handler = poll_event_handler,
      .user_data = &resp,
      .cert_pem = cloudflare_root_ca_pem_start,
      .transport_type = HTTP_TRANSPORT_OVER_SSL,
      .timeout_ms = 10000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == NULL) {
    ESP_LOGE(TAG, "Failed to initialize HTTP client");
    vTaskDelete(NULL);
    return;
  }

  esp_http_client_set_header(client, "X-WOL-Secret", WOL_SECRET);

  ESP_LOGI(TAG, "Starting poll loop against %s", url_buf);

  while (1) {
    resp.offset = 0;
    resp.buffer[0] = '\0';

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
      int status = esp_http_client_get_status_code(client);
      if (status == 200) {
        if (strstr(resp.buffer, "\"WAKE\"") != NULL) {
          ESP_LOGI(TAG, "Got wake operation, proceeding with WOL...");
          send_wol_packet(TARGET_MAC);
        }
      } else {
        ESP_LOGE(TAG, "Poll returned HTTP %d", status);
      }
    } else {
      ESP_LOGE(TAG, "Poll request failed: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
  }

  esp_http_client_cleanup(client);
  vTaskDelete(NULL);
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
  convert_mac_string_to_uint(mac_address, target_mac);
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
