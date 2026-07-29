#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "mbedtls/base64.h"

#include "esphome/components/network/ip_address.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "meshscope_web_assets.h"

namespace meshscope_edge {

static constexpr const char *TAG = "meshscope_c5";
static constexpr const char *JNAP_PREFIX = "http://linksys.com/jnap/";
static constexpr size_t MAX_JNAP_RESPONSE = 512 * 1024;
static constexpr uint32_t REFRESH_INTERVAL_MS = 10000;
static constexpr uint32_t RESTART_COOLDOWN_MS = 90000;
static constexpr uint32_t REFRESH_WAIT_MS = 20000;

struct RawEntry {
  std::string action;
  std::string response;
};

struct MeshStats {
  float nodes_online = NAN;
  float nodes_total = NAN;
  float clients_online = NAN;
  float weak_nodes = NAN;
  float backhaul_mbps = NAN;
};

struct Snapshot {
  std::vector<RawEntry> entries;
  uint32_t generation = 0;
  std::string updated_at;
  MeshStats stats;
  bool ready = false;
};

struct HttpCapture {
  std::string body;
  bool overflow = false;
};

struct JnapResult {
  bool transport_ok = false;
  int status = -1;
  std::string body;
};

struct NodeInfo {
  std::string id;
  std::string name;
  std::string ip;
  bool authority = false;
  bool online = false;
};

static std::string router_host;
static std::string router_password;
static std::string authorization;
static Snapshot snapshot;
static SemaphoreHandle_t snapshot_mutex = nullptr;
static SemaphoreHandle_t jnap_mutex = nullptr;
static TaskHandle_t collector_task_handle = nullptr;
static httpd_handle_t server = nullptr;
static std::atomic<bool> force_refresh{false};
static std::atomic<bool> router_connected{false};
static std::map<std::string, uint32_t> restart_cooldowns;

static const char *const READ_ACTIONS[] = {
    "core/GetDeviceInfo",
    "devicelist/GetDevices3",
    "networkconnections/GetNetworkConnections2",
    "nodes/diagnostics/GetBackhaulInfo",
    "nodes/networkconnections/GetNodesWirelessNetworkConnections",
    "nodes/smartmode/GetDeviceMode",
    "nodes/topologyoptimization/GetTopologyOptimizationSettings2",
    "router/GetWANStatus3",
    "router/GetLANSettings",
    "wirelessap/GetRadioInfo3",
};

static std::string iso_timestamp() {
  const time_t now = ::time(nullptr);
  if (now < 1700000000) {
    char uptime[40];
    snprintf(
        uptime,
        sizeof(uptime),
        "uptime:%llu",
        static_cast<unsigned long long>(esp_timer_get_time() / 1000000ULL));
    return uptime;
  }
  struct tm value {};
  gmtime_r(&now, &value);
  char output[32];
  strftime(output, sizeof(output), "%Y-%m-%dT%H:%M:%SZ", &value);
  return output;
}

static std::string edge_ip() {
  auto *wifi = esphome::wifi::global_wifi_component;
  if (wifi == nullptr) return {};
  char buffer[esphome::network::IP_ADDRESS_BUFFER_SIZE] = {};
  for (const auto &address : wifi->wifi_sta_ip_addresses()) {
    if (address.is_set() && address.is_ip4()) {
      address.str_to(buffer);
      return buffer;
    }
  }
  return {};
}

static bool wifi_connected() {
  auto *wifi = esphome::wifi::global_wifi_component;
  return wifi != nullptr && wifi->is_connected();
}

static std::string base64_basic_auth(const std::string &password) {
  const std::string input = "admin:" + password;
  size_t output_size = 0;
  mbedtls_base64_encode(
      nullptr,
      0,
      &output_size,
      reinterpret_cast<const unsigned char *>(input.data()),
      input.size());
  std::string encoded(output_size, '\0');
  size_t written = 0;
  if (mbedtls_base64_encode(
          reinterpret_cast<unsigned char *>(encoded.data()),
          encoded.size(),
          &written,
          reinterpret_cast<const unsigned char *>(input.data()),
          input.size()) != 0) {
    return {};
  }
  encoded.resize(written);
  return "Basic " + encoded;
}

static esp_err_t http_event_handler(esp_http_client_event_t *event) {
  auto *capture = static_cast<HttpCapture *>(event->user_data);
  if (capture == nullptr || event->event_id != HTTP_EVENT_ON_DATA ||
      event->data == nullptr || event->data_len <= 0) {
    return ESP_OK;
  }
  if (capture->body.size() + static_cast<size_t>(event->data_len) > MAX_JNAP_RESPONSE) {
    capture->overflow = true;
    return ESP_OK;
  }
  if (!capture->overflow) {
    capture->body.append(static_cast<const char *>(event->data), event->data_len);
  }
  return ESP_OK;
}

static JnapResult jnap_request(
    const std::string &host,
    const std::string &action,
    const char *body = "{}") {
  JnapResult result;
  if (jnap_mutex == nullptr ||
      xSemaphoreTake(jnap_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
    ESP_LOGW(TAG, "JNAP %s to %s timed out waiting for transport", action.c_str(), host.c_str());
    return result;
  }
  struct JnapMutexRelease {
    SemaphoreHandle_t handle;
    ~JnapMutexRelease() { xSemaphoreGive(handle); }
  } release{jnap_mutex};

  HttpCapture capture;
  const std::string url = "https://" + host + "/JNAP/";
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = 12000;
  config.transport_type = HTTP_TRANSPORT_OVER_SSL;
  config.skip_cert_common_name_check = true;
  config.disable_auto_redirect = true;
  config.buffer_size = 4096;
  config.buffer_size_tx = 2048;
  config.keep_alive_enable = false;
  config.event_handler = http_event_handler;
  config.user_data = &capture;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) return result;
  const std::string action_header = std::string(JNAP_PREFIX) + action;
  esp_http_client_set_header(client, "Content-Type", "application/json; charset=UTF-8");
  esp_http_client_set_header(client, "X-JNAP-Action", action_header.c_str());
  esp_http_client_set_header(client, "X-JNAP-Authorization", authorization.c_str());
  esp_http_client_set_header(client, "Cache-Control", "no-cache");
  esp_http_client_set_post_field(client, body, strlen(body));

  const esp_err_t error = esp_http_client_perform(client);
  result.status = esp_http_client_get_status_code(client);
  result.transport_ok =
      error == ESP_OK && !capture.overflow &&
      esp_http_client_is_complete_data_received(client);
  if (error != ESP_OK) {
    ESP_LOGW(
        TAG,
        "JNAP %s to %s failed: %s",
        action.c_str(),
        host.c_str(),
        esp_err_to_name(error));
  } else if (capture.overflow) {
    ESP_LOGW(TAG, "JNAP %s exceeded %u bytes", action.c_str(), MAX_JNAP_RESPONSE);
  }
  result.body.swap(capture.body);
  esp_http_client_cleanup(client);
  return result;
}

static bool response_is_ok(const std::string &body) {
  cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (root == nullptr) return false;
  const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
  const bool ok = cJSON_IsString(result) && strcmp(result->valuestring, "OK") == 0;
  cJSON_Delete(root);
  return ok;
}

static const std::string *entry_response(
    const Snapshot &source,
    const char *action) {
  for (const auto &entry : source.entries) {
    if (entry.action == action) return &entry.response;
  }
  return nullptr;
}

static cJSON *parse_response_root(const Snapshot &source, const char *action) {
  const std::string *body = entry_response(source, action);
  if (body == nullptr) return nullptr;
  return cJSON_ParseWithLength(body->c_str(), body->size());
}

static const cJSON *response_output(cJSON *root) {
  if (root == nullptr) return nullptr;
  const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
  if (!cJSON_IsString(result) || strcmp(result->valuestring, "OK") != 0) return nullptr;
  const cJSON *output = cJSON_GetObjectItemCaseSensitive(root, "output");
  return cJSON_IsObject(output) ? output : nullptr;
}

static const char *json_string(const cJSON *object, const char *name) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);
  return cJSON_IsString(value) && value->valuestring != nullptr ? value->valuestring : nullptr;
}

static bool json_bool(const cJSON *object, const char *name, bool fallback = false) {
  const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);
  return cJSON_IsBool(value) ? cJSON_IsTrue(value) : fallback;
}

static std::string device_name(const cJSON *device) {
  const cJSON *properties = cJSON_GetObjectItemCaseSensitive(device, "properties");
  if (cJSON_IsArray(properties)) {
    const cJSON *property = nullptr;
    cJSON_ArrayForEach(property, properties) {
      if (strcmp(json_string(property, "name") ?: "", "userDeviceName") == 0) {
        const char *value = json_string(property, "value");
        if (value != nullptr && value[0] != '\0') return value;
      }
    }
  }
  const char *friendly = json_string(device, "friendlyName");
  if (friendly != nullptr && friendly[0] != '\0') return friendly;
  const cJSON *model = cJSON_GetObjectItemCaseSensitive(device, "model");
  const char *model_number = cJSON_IsObject(model) ? json_string(model, "modelNumber") : nullptr;
  return model_number != nullptr ? model_number : "Linksys Node";
}

static void collect_device_macs(const cJSON *device, std::set<std::string> &output) {
  const cJSON *known = cJSON_GetObjectItemCaseSensitive(device, "knownMACAddresses");
  if (cJSON_IsArray(known)) {
    const cJSON *value = nullptr;
    cJSON_ArrayForEach(value, known) {
      if (cJSON_IsString(value) && value->valuestring != nullptr) output.insert(value->valuestring);
    }
  }
  for (const char *field : {"knownInterfaces", "connections"}) {
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(device, field);
    if (!cJSON_IsArray(items)) continue;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, items) {
      const char *mac = json_string(item, "macAddress");
      if (mac != nullptr) output.insert(mac);
    }
  }
}

static MeshStats calculate_stats(const Snapshot &source) {
  MeshStats stats;
  cJSON *devices_root = parse_response_root(source, "devicelist/GetDevices3");
  cJSON *backhaul_root = parse_response_root(source, "nodes/diagnostics/GetBackhaulInfo");
  cJSON *main_root = parse_response_root(source, "networkconnections/GetNetworkConnections2");
  cJSON *wireless_root =
      parse_response_root(source, "nodes/networkconnections/GetNodesWirelessNetworkConnections");
  const cJSON *devices_output = response_output(devices_root);
  const cJSON *backhaul_output = response_output(backhaul_root);
  const cJSON *main_output = response_output(main_root);
  const cJSON *wireless_output = response_output(wireless_root);
  const cJSON *devices =
      devices_output ? cJSON_GetObjectItemCaseSensitive(devices_output, "devices") : nullptr;
  const cJSON *backhauls =
      backhaul_output
          ? cJSON_GetObjectItemCaseSensitive(backhaul_output, "backhaulDevices")
          : nullptr;

  if (!cJSON_IsArray(devices)) {
    cJSON_Delete(devices_root);
    cJSON_Delete(backhaul_root);
    cJSON_Delete(main_root);
    cJSON_Delete(wireless_root);
    return stats;
  }

  std::set<std::string> backhaul_ids;
  std::set<std::string> live_macs;
  float backhaul_sum = 0;
  int weak = 0;
  if (cJSON_IsArray(backhauls)) {
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, backhauls) {
      const char *id = json_string(item, "deviceUUID");
      if (id != nullptr) backhaul_ids.insert(id);
      const cJSON *speed = cJSON_GetObjectItemCaseSensitive(item, "speedMbps");
      if (cJSON_IsNumber(speed)) backhaul_sum += static_cast<float>(speed->valuedouble);
      if (cJSON_IsString(speed)) backhaul_sum += strtof(speed->valuestring, nullptr);
      const cJSON *wireless =
          cJSON_GetObjectItemCaseSensitive(item, "wirelessConnectionInfo");
      const cJSON *rssi =
          cJSON_IsObject(wireless)
              ? cJSON_GetObjectItemCaseSensitive(wireless, "stationRSSI")
              : nullptr;
      if ((!cJSON_IsNumber(rssi) || rssi->valuedouble == 0) && cJSON_IsObject(wireless)) {
        rssi = cJSON_GetObjectItemCaseSensitive(wireless, "apRSSI");
      }
      if (cJSON_IsNumber(rssi) && rssi->valuedouble < -67) weak++;
    }
  }

  if (main_output != nullptr) {
    const cJSON *connections = cJSON_GetObjectItemCaseSensitive(main_output, "connections");
    if (cJSON_IsArray(connections)) {
      const cJSON *connection = nullptr;
      cJSON_ArrayForEach(connection, connections) {
        const char *mac = json_string(connection, "macAddress");
        if (mac != nullptr) live_macs.insert(mac);
      }
    }
  }
  if (wireless_output != nullptr) {
    const cJSON *groups =
        cJSON_GetObjectItemCaseSensitive(wireless_output, "nodeWirelessConnections");
    if (cJSON_IsArray(groups)) {
      const cJSON *group = nullptr;
      cJSON_ArrayForEach(group, groups) {
        const cJSON *connections = cJSON_GetObjectItemCaseSensitive(group, "connections");
        if (!cJSON_IsArray(connections)) continue;
        const cJSON *connection = nullptr;
        cJSON_ArrayForEach(connection, connections) {
          const char *mac = json_string(connection, "macAddress");
          if (mac != nullptr) live_macs.insert(mac);
        }
      }
    }
  }

  std::set<std::string> node_ids;
  int authority_count = 0;
  const cJSON *device = nullptr;
  cJSON_ArrayForEach(device, devices) {
    const char *id = json_string(device, "deviceID");
    const bool authority = json_bool(device, "isAuthority");
    const char *node_type = json_string(device, "nodeType");
    if (id != nullptr &&
        (authority || node_type != nullptr || backhaul_ids.count(id) != 0)) {
      node_ids.insert(id);
      if (authority) authority_count++;
    }
  }

  int client_total = 0;
  int client_online = 0;
  cJSON_ArrayForEach(device, devices) {
    const char *id = json_string(device, "deviceID");
    if (id != nullptr && node_ids.count(id) != 0) continue;
    client_total++;
    const cJSON *connections = cJSON_GetObjectItemCaseSensitive(device, "connections");
    bool online = cJSON_IsArray(connections) && cJSON_GetArraySize(connections) > 0;
    if (!online) {
      std::set<std::string> macs;
      collect_device_macs(device, macs);
      for (const auto &mac : macs) {
        if (live_macs.count(mac) != 0) {
          online = true;
          break;
        }
      }
    }
    if (online) client_online++;
  }

  stats.nodes_total = static_cast<float>(node_ids.size());
  stats.nodes_online = static_cast<float>(authority_count + backhaul_ids.size());
  stats.clients_online = static_cast<float>(client_online);
  stats.weak_nodes = static_cast<float>(weak);
  stats.backhaul_mbps = backhaul_sum;
  ESP_LOGI(
      TAG,
      "Topology stats nodes=%.0f/%.0f clients=%d/%d weak=%d backhaul=%.1f Mbps",
      stats.nodes_online,
      stats.nodes_total,
      client_online,
      client_total,
      weak,
      backhaul_sum);

  cJSON_Delete(devices_root);
  cJSON_Delete(backhaul_root);
  cJSON_Delete(main_root);
  cJSON_Delete(wireless_root);
  return stats;
}

static bool collect_snapshot(Snapshot &candidate) {
  candidate.entries.clear();
  candidate.entries.reserve(sizeof(READ_ACTIONS) / sizeof(READ_ACTIONS[0]));
  bool devices_ok = false;
  for (const char *action : READ_ACTIONS) {
    const JnapResult result = jnap_request(router_host, action);
    if (!result.transport_ok || result.status != 200) {
      ESP_LOGW(TAG, "Topology action %s returned HTTP %d", action, result.status);
      return false;
    }
    if (strcmp(action, "devicelist/GetDevices3") == 0) {
      devices_ok = response_is_ok(result.body);
    }
    candidate.entries.push_back({action, result.body});
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  if (!devices_ok) {
    ESP_LOGW(TAG, "Topology refresh rejected: device list was not authorized");
    return false;
  }
  candidate.updated_at = iso_timestamp();
  candidate.stats = calculate_stats(candidate);
  candidate.ready = true;
  return true;
}

static void collector_task(void *) {
  uint32_t generation = 0;
  while (true) {
    if (!wifi_connected()) {
      router_connected.store(false, std::memory_order_release);
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }
    Snapshot candidate;
    if (collect_snapshot(candidate)) {
      candidate.generation = ++generation;
      if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        snapshot = std::move(candidate);
        xSemaphoreGive(snapshot_mutex);
        router_connected.store(true, std::memory_order_release);
        ESP_LOGI(
            TAG,
            "Topology generation %u cached; free=%u external=%u",
            static_cast<unsigned>(generation),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        App.wake_loop_threadsafe();
      }
    } else {
      router_connected.store(false, std::memory_order_release);
    }
    force_refresh.store(false, std::memory_order_release);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(REFRESH_INTERVAL_MS));
  }
}

static esp_err_t send_json(httpd_req_t *request, const char *body, const char *status = nullptr) {
  if (status != nullptr) httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_error_json(
    httpd_req_t *request,
    const char *status,
    const char *message) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "error", message);
  char *serialized = cJSON_PrintUnformatted(root);
  const esp_err_t result = send_json(request, serialized ?: "{\"error\":\"unknown\"}", status);
  cJSON_free(serialized);
  cJSON_Delete(root);
  return result;
}

static esp_err_t asset_handler(httpd_req_t *request) {
  const auto *asset = static_cast<const meshscope_web_assets::Asset *>(request->user_ctx);
  if (asset == nullptr) return ESP_FAIL;
  httpd_resp_set_type(request, asset->content_type);
  httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  return httpd_resp_send(
      request,
      reinterpret_cast<const char *>(asset->data),
      asset->size);
}

static esp_err_t status_handler(httpd_req_t *request) {
  const std::string ip = edge_ip();
  uint32_t generation = 0;
  std::string cached_at;
  bool ready = false;
  if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    generation = snapshot.generation;
    cached_at = snapshot.updated_at;
    ready = snapshot.ready;
    xSemaphoreGive(snapshot_mutex);
  }
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "connected", ready);
  cJSON_AddBoolToObject(root, "demo", false);
  cJSON_AddBoolToObject(root, "managedConnection", true);
  cJSON_AddStringToObject(root, "router", router_host.c_str());
  cJSON_AddStringToObject(root, "edgeAddress", ip.c_str());
  cJSON_AddStringToObject(root, "edgeUrl", ip.empty() ? "" : ("http://" + ip + "/").c_str());
  cJSON_AddStringToObject(root, "cachedAt", cached_at.c_str());
  cJSON_AddNumberToObject(root, "generation", generation);
  char *body = cJSON_PrintUnformatted(root);
  const esp_err_t result = send_json(request, body ?: "{}");
  cJSON_free(body);
  cJSON_Delete(root);
  return result;
}

static esp_err_t topology_handler(httpd_req_t *request) {
  if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    return send_error_json(request, "503 Service Unavailable", "拓扑缓存暂时繁忙。");
  }
  if (!snapshot.ready) {
    xSemaphoreGive(snapshot_mutex);
    return send_error_json(request, "503 Service Unavailable", "ESP32 正在读取首次拓扑。");
  }
  httpd_resp_set_type(request, "application/json; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  const std::string ip = edge_ip();
  std::string prefix =
      "{\"router\":\"" + router_host +
      "\",\"meta\":{\"updatedAt\":\"" + snapshot.updated_at +
      "\",\"edgeAddress\":\"" + ip +
      "\",\"generation\":" + std::to_string(snapshot.generation) +
      "},\"rawJnap\":{";
  esp_err_t result = httpd_resp_send_chunk(request, prefix.c_str(), prefix.size());
  bool first = true;
  for (const auto &entry : snapshot.entries) {
    if (result != ESP_OK) break;
    std::string key = (first ? "\"" : ",\"") + entry.action + "\":";
    result = httpd_resp_send_chunk(request, key.c_str(), key.size());
    if (result == ESP_OK) {
      result = httpd_resp_send_chunk(request, entry.response.c_str(), entry.response.size());
    }
    first = false;
  }
  if (result == ESP_OK) result = httpd_resp_send_chunk(request, "}}", 2);
  if (result == ESP_OK) result = httpd_resp_send_chunk(request, nullptr, 0);
  xSemaphoreGive(snapshot_mutex);
  return result;
}

static uint32_t current_generation() {
  uint32_t value = 0;
  if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    value = snapshot.generation;
    xSemaphoreGive(snapshot_mutex);
  }
  return value;
}

static void request_refresh() {
  force_refresh.store(true, std::memory_order_release);
  if (collector_task_handle != nullptr) xTaskNotifyGive(collector_task_handle);
}

static esp_err_t refresh_handler(httpd_req_t *request) {
  const uint32_t previous = current_generation();
  request_refresh();
  const uint32_t started = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  while (current_generation() <= previous &&
         static_cast<uint32_t>(esp_timer_get_time() / 1000ULL) - started < REFRESH_WAIT_MS) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return topology_handler(request);
}

static esp_err_t connect_handler(httpd_req_t *request) {
  if (request->content_len > 0) {
    char discard[256];
    size_t remaining = request->content_len;
    while (remaining > 0) {
      const int received =
          httpd_req_recv(request, discard, std::min(remaining, sizeof(discard)));
      if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
      if (received <= 0) break;
      remaining -= static_cast<size_t>(received);
    }
  }
  return refresh_handler(request);
}

static bool private_ipv4(const std::string &value) {
  ip4_addr_t address;
  if (!ip4addr_aton(value.c_str(), &address)) return false;
  const uint32_t host = lwip_ntohl(ip4_addr_get_u32(&address));
  const uint8_t a = (host >> 24) & 0xff;
  const uint8_t b = (host >> 16) & 0xff;
  return a == 10 || (a == 172 && b >= 16 && b <= 31) ||
         (a == 192 && b == 168) || (a == 169 && b == 254);
}

static bool resolve_node(const std::string &node_id, NodeInfo &node) {
  if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) return false;
  cJSON *devices_root = parse_response_root(snapshot, "devicelist/GetDevices3");
  cJSON *backhaul_root = parse_response_root(snapshot, "nodes/diagnostics/GetBackhaulInfo");
  const cJSON *devices_output = response_output(devices_root);
  const cJSON *backhaul_output = response_output(backhaul_root);
  const cJSON *devices =
      devices_output ? cJSON_GetObjectItemCaseSensitive(devices_output, "devices") : nullptr;
  const cJSON *backhauls =
      backhaul_output
          ? cJSON_GetObjectItemCaseSensitive(backhaul_output, "backhaulDevices")
          : nullptr;
  bool found = false;
  if (cJSON_IsArray(devices)) {
    const cJSON *device = nullptr;
    cJSON_ArrayForEach(device, devices) {
      if (node_id != (json_string(device, "deviceID") ?: "")) continue;
      node.id = node_id;
      node.name = device_name(device);
      node.authority = json_bool(device, "isAuthority");
      node.online = node.authority;
      const cJSON *connections = cJSON_GetObjectItemCaseSensitive(device, "connections");
      if (cJSON_IsArray(connections)) {
        const cJSON *connection = nullptr;
        cJSON_ArrayForEach(connection, connections) {
          const char *ip = json_string(connection, "ipAddress");
          if (ip != nullptr && private_ipv4(ip)) {
            node.ip = ip;
            break;
          }
        }
      }
      found = true;
      break;
    }
  }
  if (found && cJSON_IsArray(backhauls)) {
    const cJSON *backhaul = nullptr;
    cJSON_ArrayForEach(backhaul, backhauls) {
      if (node_id != (json_string(backhaul, "deviceUUID") ?: "")) continue;
      node.online = true;
      const char *ip = json_string(backhaul, "ipAddress");
      if (ip != nullptr && private_ipv4(ip)) node.ip = ip;
      break;
    }
  }
  cJSON_Delete(devices_root);
  cJSON_Delete(backhaul_root);
  xSemaphoreGive(snapshot_mutex);
  return found && node.online && !node.ip.empty();
}

static bool query_value(httpd_req_t *request, const char *key, std::string &value) {
  const size_t length = httpd_req_get_url_query_len(request);
  if (length == 0 || length > 512) return false;
  std::vector<char> query(length + 1);
  if (httpd_req_get_url_query_str(request, query.data(), query.size()) != ESP_OK) return false;
  char output[160] = {};
  if (httpd_query_key_value(query.data(), key, output, sizeof(output)) != ESP_OK) return false;
  value = output;
  return !value.empty();
}

static cJSON *parse_output_copy(const JnapResult &response) {
  if (!response.transport_ok || response.status != 200) return nullptr;
  cJSON *root = cJSON_ParseWithLength(response.body.c_str(), response.body.size());
  const cJSON *output = response_output(root);
  cJSON *copy = output != nullptr ? cJSON_Duplicate(output, true) : nullptr;
  cJSON_Delete(root);
  return copy;
}

static bool services_contain(const cJSON *identity, const char *needle) {
  const cJSON *services = cJSON_GetObjectItemCaseSensitive(identity, "services");
  if (!cJSON_IsArray(services)) return false;
  const cJSON *service = nullptr;
  cJSON_ArrayForEach(service, services) {
    if (cJSON_IsString(service) && service->valuestring != nullptr &&
        strncmp(service->valuestring, needle, strlen(needle)) == 0) {
      return true;
    }
  }
  return false;
}

static esp_err_t capabilities_handler(httpd_req_t *request) {
  std::string node_id;
  if (!query_value(request, "nodeId", node_id)) {
    return send_error_json(request, "400 Bad Request", "缺少 Node ID。");
  }
  NodeInfo node;
  if (!resolve_node(node_id, node)) {
    return send_error_json(request, "400 Bad Request", "Node 不在线或不在当前拓扑。");
  }

  const JnapResult check = jnap_request(node.ip, "core/CheckAdminPassword");
  const JnapResult identity_response = jnap_request(node.ip, "core/GetDeviceInfo");
  const JnapResult mode_response = jnap_request(node.ip, "nodes/smartmode/GetDeviceMode");
  const JnapResult optimization_response =
      jnap_request(node.ip, "nodes/topologyoptimization/GetTopologyOptimizationSettings2");
  const bool credentials_ok = check.transport_ok && check.status == 200 &&
                              response_is_ok(check.body);
  cJSON *identity = parse_output_copy(identity_response);
  cJSON *mode = parse_output_copy(mode_response);
  cJSON *optimization = parse_output_copy(optimization_response);
  if (identity == nullptr) {
    cJSON_Delete(mode);
    cJSON_Delete(optimization);
    return send_error_json(request, "502 Bad Gateway", "无法读取 Node 身份。");
  }

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "nodeId", node.id.c_str());
  cJSON_AddStringToObject(root, "name", node.name.c_str());
  cJSON_AddStringToObject(root, "ipAddress", node.ip.c_str());
  cJSON_AddStringToObject(root, "managementUrl", ("https://" + node.ip + "/ca").c_str());
  cJSON_AddStringToObject(root, "managementEntry", "ca-support");
  cJSON_AddBoolToObject(root, "credentialsSynchronized", credentials_ok);
  cJSON_AddStringToObject(root, "deviceMode", json_string(mode, "mode") ?: "");
  cJSON *identity_json = cJSON_AddObjectToObject(root, "identity");
  cJSON_AddStringToObject(identity_json, "model", json_string(identity, "modelNumber") ?: "");
  cJSON_AddStringToObject(
      identity_json,
      "hardwareVersion",
      json_string(identity, "hardwareVersion") ?: "");
  cJSON_AddStringToObject(
      identity_json,
      "firmwareVersion",
      json_string(identity, "firmwareVersion") ?: "");
  cJSON_AddStringToObject(
      identity_json,
      "serialNumber",
      json_string(identity, "serialNumber") ?: "");
  cJSON *services = cJSON_AddObjectToObject(root, "services");
  cJSON_AddBoolToObject(
      services,
      "coreReboot",
      services_contain(identity, "http://linksys.com/jnap/core/Core"));
  const bool setup3 =
      services_contain(identity, "http://linksys.com/jnap/nodes/setup/Setup3");
  cJSON_AddBoolToObject(services, "nodesSetup3", setup3);
  cJSON_AddBoolToObject(
      services,
      "topologyOptimization2",
      services_contain(
          identity,
          "http://linksys.com/jnap/nodes/topologyoptimization/TopologyOptimization2"));
  cJSON *topology = cJSON_AddObjectToObject(root, "topologyOptimization");
  cJSON_AddBoolToObject(
      topology,
      "clientSteeringEnabled",
      json_bool(optimization, "isClientSteeringEnabled"));
  cJSON_AddBoolToObject(
      topology,
      "nodeSteeringEnabled",
      json_bool(optimization, "isNodeSteeringEnabled"));
  cJSON *restart = cJSON_AddObjectToObject(root, "individualRestart");
  cJSON_AddBoolToObject(restart, "visibleInCaSupportUi", setup3);
  cJSON_AddStringToObject(restart, "action", "core/Reboot");
  cJSON_AddBoolToObject(restart, "hasTargetDeviceId", false);
  cJSON_AddStringToObject(restart, "scope", "single-node");
  cJSON_AddBoolToObject(restart, "executed", false);
  cJSON *manual = cJSON_AddObjectToObject(root, "manualParentSelection");
  cJSON_AddBoolToObject(manual, "available", false);
  cJSON_AddStringToObject(manual, "transport", "not-available");
  cJSON_AddBoolToObject(manual, "firmwareInternalPathDiscovered", true);
  cJSON_AddStringToObject(
      manual,
      "reason",
      "固件内部已确认指定 Parent 数据路径，但普通 JNAP 尚无传输入口。");
  cJSON_AddStringToObject(root, "observedAt", iso_timestamp().c_str());

  char *body = cJSON_PrintUnformatted(root);
  const esp_err_t result = send_json(request, body ?: "{}");
  cJSON_free(body);
  cJSON_Delete(root);
  cJSON_Delete(identity);
  cJSON_Delete(mode);
  cJSON_Delete(optimization);
  return result;
}

static bool read_request_json(httpd_req_t *request, std::string &body, size_t maximum = 1024) {
  if (request->content_len <= 0 || static_cast<size_t>(request->content_len) > maximum) return false;
  body.resize(request->content_len);
  size_t offset = 0;
  while (offset < body.size()) {
    const int received =
        httpd_req_recv(request, body.data() + offset, body.size() - offset);
    if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (received <= 0) return false;
    offset += received;
  }
  return true;
}

static esp_err_t restart_handler(httpd_req_t *request) {
  const size_t content_type_length =
      httpd_req_get_hdr_value_len(request, "Content-Type");
  std::vector<char> content_type(content_type_length + 1);
  if (content_type_length == 0 ||
      httpd_req_get_hdr_value_str(
          request, "Content-Type", content_type.data(), content_type.size()) != ESP_OK ||
      strstr(content_type.data(), "application/json") == nullptr) {
    return send_error_json(request, "415 Unsupported Media Type", "请求必须是 JSON。");
  }
  std::string request_body;
  if (!read_request_json(request, request_body)) {
    return send_error_json(request, "400 Bad Request", "重启请求格式无效。");
  }
  cJSON *payload = cJSON_ParseWithLength(request_body.c_str(), request_body.size());
  const char *node_id = payload != nullptr ? json_string(payload, "nodeId") : nullptr;
  if (node_id == nullptr || node_id[0] == '\0') {
    cJSON_Delete(payload);
    return send_error_json(request, "400 Bad Request", "缺少 Node ID。");
  }
  NodeInfo node;
  if (!resolve_node(node_id, node)) {
    cJSON_Delete(payload);
    return send_error_json(request, "400 Bad Request", "Node 不在线或不在当前拓扑。");
  }
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  const auto cooldown = restart_cooldowns.find(node.id);
  if (cooldown != restart_cooldowns.end() && now - cooldown->second < RESTART_COOLDOWN_MS) {
    cJSON_Delete(payload);
    return send_error_json(request, "409 Conflict", "该 Node 正在重启，请等待恢复。");
  }
  restart_cooldowns[node.id] = now;
  const JnapResult reboot = jnap_request(node.ip, "core/Reboot");
  if (!reboot.transport_ok || reboot.status != 200 || !response_is_ok(reboot.body)) {
    restart_cooldowns.erase(node.id);
    cJSON_Delete(payload);
    return send_error_json(request, "502 Bad Gateway", "Node 未接受重启请求。");
  }
  cJSON_Delete(payload);
  request_refresh();
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "accepted", true);
  cJSON_AddStringToObject(root, "action", "core/Reboot");
  cJSON_AddStringToObject(root, "scope", "single-node");
  cJSON_AddStringToObject(root, "requestedAt", iso_timestamp().c_str());
  cJSON *target = cJSON_AddObjectToObject(root, "requestedThroughNode");
  cJSON_AddStringToObject(target, "id", node.id.c_str());
  cJSON_AddStringToObject(target, "name", node.name.c_str());
  cJSON_AddStringToObject(target, "ipAddress", node.ip.c_str());
  char *body = cJSON_PrintUnformatted(root);
  const esp_err_t result = send_json(request, body ?: "{}");
  cJSON_free(body);
  cJSON_Delete(root);
  return result;
}

static esp_err_t not_found_handler(httpd_req_t *request, httpd_err_code_t) {
  return send_error_json(request, "404 Not Found", "未找到接口。");
}

static void register_handler(
    const char *uri,
    httpd_method_t method,
    esp_err_t (*handler)(httpd_req_t *),
    void *context = nullptr) {
  httpd_uri_t descriptor = {};
  descriptor.uri = uri;
  descriptor.method = method;
  descriptor.handler = handler;
  descriptor.user_ctx = context;
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &descriptor));
}

static void start_server() {
  if (server != nullptr) return;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 20;
  config.max_open_sockets = 4;
  config.backlog_conn = 4;
  config.stack_size = 16384;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 10;
  config.send_wait_timeout = 15;
  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "Unable to start MeshScope HTTP server");
    server = nullptr;
    return;
  }
  for (size_t index = 0; index < meshscope_web_assets::ASSET_COUNT; index++) {
    const auto &asset = meshscope_web_assets::ASSETS[index];
    register_handler(
        asset.path,
        HTTP_GET,
        asset_handler,
        const_cast<meshscope_web_assets::Asset *>(&asset));
  }
  register_handler("/api/status", HTTP_GET, status_handler);
  register_handler("/api/topology", HTTP_GET, topology_handler);
  register_handler("/api/refresh", HTTP_POST, refresh_handler);
  register_handler("/api/connect", HTTP_POST, connect_handler);
  register_handler("/api/node-capabilities", HTTP_GET, capabilities_handler);
  register_handler("/api/restart-node", HTTP_POST, restart_handler);
  httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found_handler);
  ESP_LOGI(TAG, "MeshScope HTTP server started on port 80");
}

static void setup(const char *host, const char *password) {
  router_host = host ?: "";
  router_password = password ?: "";
  authorization = base64_basic_auth(router_password);
  snapshot_mutex = xSemaphoreCreateMutex();
  jnap_mutex = xSemaphoreCreateMutex();
  if (snapshot_mutex == nullptr || jnap_mutex == nullptr || authorization.empty()) {
    ESP_LOGE(TAG, "MeshScope initialization failed");
    return;
  }
  ESP_LOGI(
      TAG,
      "MeshScope ESPHome edge starting; assets=%u source=%s PSRAM=%u",
      static_cast<unsigned>(meshscope_web_assets::ASSET_COUNT),
      meshscope_web_assets::SOURCE_SHA256,
      static_cast<unsigned>(esp_psram_get_size()));
  start_server();
  if (xTaskCreate(
          collector_task,
          "meshscope_collect",
          16384,
          nullptr,
          2,
          &collector_task_handle) != pdPASS) {
    ESP_LOGE(TAG, "Unable to start topology collector task");
    collector_task_handle = nullptr;
  }
}

static MeshStats stats_copy() {
  MeshStats value;
  if (snapshot_mutex != nullptr &&
      xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    value = snapshot.stats;
    xSemaphoreGive(snapshot_mutex);
  }
  return value;
}

static std::string last_update_copy() {
  std::string value;
  if (snapshot_mutex != nullptr &&
      xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    value = snapshot.updated_at;
    xSemaphoreGive(snapshot_mutex);
  }
  return value;
}

}  // namespace meshscope_edge

inline void meshscope_edge_setup(const char *host, const char *password) {
  meshscope_edge::setup(host, password);
}

inline void meshscope_edge_force_refresh() {
  meshscope_edge::request_refresh();
}

inline bool meshscope_edge_router_connected() {
  return meshscope_edge::router_connected.load(std::memory_order_acquire);
}

inline float meshscope_edge_nodes_online() {
  return meshscope_edge::stats_copy().nodes_online;
}

inline float meshscope_edge_nodes_total() {
  return meshscope_edge::stats_copy().nodes_total;
}

inline float meshscope_edge_clients_online() {
  return meshscope_edge::stats_copy().clients_online;
}

inline float meshscope_edge_weak_nodes() {
  return meshscope_edge::stats_copy().weak_nodes;
}

inline float meshscope_edge_backhaul_mbps() {
  return meshscope_edge::stats_copy().backhaul_mbps;
}

inline float meshscope_edge_free_heap() {
  return static_cast<float>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
}

inline std::string meshscope_edge_url() {
  const std::string ip = meshscope_edge::edge_ip();
  return ip.empty() ? std::string("等待 Wi-Fi") : "http://" + ip + "/";
}

inline std::string meshscope_edge_summary() {
  const meshscope_edge::MeshStats stats = meshscope_edge::stats_copy();
  if (std::isnan(stats.nodes_online)) return "等待首次拓扑";
  char output[96];
  snprintf(
      output,
      sizeof(output),
      "%.0f/%.0f nodes · %.0f clients · %.0f weak",
      stats.nodes_online,
      stats.nodes_total,
      stats.clients_online,
      stats.weak_nodes);
  return output;
}

inline std::string meshscope_edge_last_update() {
  const std::string value = meshscope_edge::last_update_copy();
  return value.empty() ? std::string("等待首次拓扑") : value;
}
