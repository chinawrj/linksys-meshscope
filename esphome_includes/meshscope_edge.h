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
#include "esp_timer.h"
#if defined(CONFIG_SPIRAM)
#include "esp_psram.h"
#endif
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

static constexpr const char *TAG = "meshscope_edge";
static constexpr const char *JNAP_PREFIX = "http://linksys.com/jnap/";
static constexpr size_t MAX_JNAP_RESPONSE = 512 * 1024;
// An 8 KB match/history window keeps the no-PSRAM HTTP streaming workspace
// below the smallest contiguous heap block observed during live C6 refreshes.
// Compression remains lossless; the complete raw JNAP response is preserved.
static constexpr size_t COMPRESSION_WINDOW = 8 * 1024;
static constexpr size_t COMPRESSION_HASH_SIZE = 2048;
static constexpr uint32_t REFRESH_INTERVAL_MS = 10000;
static constexpr uint32_t RESTART_COOLDOWN_MS = 90000;
static constexpr uint32_t REFRESH_WAIT_MS = 20000;

struct RawEntry {
  std::string action;
  std::string compressed_response;
  size_t response_size = 0;
};

struct MeshStats {
  float nodes_online = NAN;
  float nodes_total = NAN;
  float clients_online = NAN;
  float weak_nodes = NAN;
  float backhaul_mbps = NAN;
};

struct StatsAccumulator {
  std::set<std::string> backhaul_ids;
  std::set<std::string> live_macs;
  float backhaul_sum = 0;
  int weak = 0;
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
  size_t limit = 0;
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

enum class ClientDetailsMode {
  AUTO,
  FULL,
  NODES_ONLY,
};

static std::string router_host;
static std::string router_password;
static std::string authorization;
static std::string web_authorization;
static Snapshot snapshot;
static SemaphoreHandle_t snapshot_mutex = nullptr;
static SemaphoreHandle_t jnap_mutex = nullptr;
static SemaphoreHandle_t memory_mutex = nullptr;
static TaskHandle_t collector_task_handle = nullptr;
static httpd_handle_t server = nullptr;
static std::atomic<bool> force_refresh{false};
static std::atomic<bool> router_connected{false};
static std::map<std::string, uint32_t> restart_cooldowns;
static ClientDetailsMode requested_client_details = ClientDetailsMode::AUTO;
static ClientDetailsMode active_client_details = ClientDetailsMode::FULL;
static bool client_details_resolved = false;

static const char *const READ_ACTIONS[] = {
    "core/GetDeviceInfo",
    "networkconnections/GetNetworkConnections2",
    "nodes/networkconnections/GetNodesWirelessNetworkConnections",
    "nodes/diagnostics/GetBackhaulInfo",
    "nodes/smartmode/GetDeviceMode",
    "nodes/topologyoptimization/GetTopologyOptimizationSettings2",
    "router/GetWANStatus3",
    "router/GetLANSettings",
    "wirelessap/GetRadioInfo3",
    "devicelist/GetDevices3",
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

static const char *client_details_name(ClientDetailsMode mode) {
  switch (mode) {
    case ClientDetailsMode::AUTO:
      return "auto";
    case ClientDetailsMode::NODES_ONLY:
      return "nodes-only";
    default:
      return "full";
  }
}

static ClientDetailsMode parse_client_details(const char *value) {
  if (strcmp(value ?: "", "nodes-only") == 0) {
    return ClientDetailsMode::NODES_ONLY;
  }
  if (strcmp(value ?: "", "full") == 0) {
    return ClientDetailsMode::FULL;
  }
  return ClientDetailsMode::AUTO;
}

static size_t external_memory_size();

static void resolve_client_details_mode() {
  if (client_details_resolved) return;
  active_client_details = requested_client_details;
  if (active_client_details == ClientDetailsMode::AUTO) {
    const size_t full_reserve = 123 * 1024;
    const size_t free_heap =
        heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    active_client_details =
        external_memory_size() > 0 ||
                (free_heap >= full_reserve + 112 * 1024 &&
                 largest >= full_reserve + 1024)
            ? ClientDetailsMode::FULL
            : ClientDetailsMode::NODES_ONLY;
    ESP_LOGI(
        TAG,
        "Client details auto-selected %s; free=%u largest=%u PSRAM=%u",
        client_details_name(active_client_details),
        static_cast<unsigned>(free_heap),
        static_cast<unsigned>(largest),
        static_cast<unsigned>(external_memory_size()));
  } else {
    ESP_LOGI(
        TAG,
        "Client details configured as %s",
        client_details_name(active_client_details));
  }
  client_details_resolved = true;
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

static size_t external_memory_size() {
#if defined(CONFIG_SPIRAM)
  return esp_psram_get_size();
#else
  return 0;
#endif
}

static size_t external_memory_free() {
#if defined(CONFIG_SPIRAM)
  return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
  return 0;
#endif
}

static std::string base64_basic_auth(
    const std::string &username,
    const std::string &password) {
  const std::string input = username + ":" + password;
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

static bool base64_decode_string(
    const std::string &encoded,
    std::string &decoded) {
  size_t required = 0;
  const int sizing = mbedtls_base64_decode(
      nullptr,
      0,
      &required,
      reinterpret_cast<const unsigned char *>(encoded.data()),
      encoded.size());
  if (sizing != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL || required == 0) return false;
  decoded.resize(required);
  size_t written = 0;
  const int result = mbedtls_base64_decode(
      reinterpret_cast<unsigned char *>(decoded.data()),
      decoded.size(),
      &written,
      reinterpret_cast<const unsigned char *>(encoded.data()),
      encoded.size());
  if (result != 0) {
    decoded.clear();
    return false;
  }
  decoded.resize(written);
  return true;
}

static bool authorize_web_request(httpd_req_t *request) {
  const size_t length = httpd_req_get_hdr_value_len(request, "Authorization");
  std::vector<char> header(length + 1);
  const bool authorized =
      length > 0 &&
      httpd_req_get_hdr_value_str(
          request,
          "Authorization",
          header.data(),
          header.size()) == ESP_OK &&
      web_authorization == header.data();
  if (authorized) return true;
  httpd_resp_set_status(request, "401 Unauthorized");
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  httpd_resp_set_hdr(request, "WWW-Authenticate", "Basic realm=\"MeshScope\"");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_send(request, "MeshScope login required.", HTTPD_RESP_USE_STRLEN);
  return false;
}

static esp_err_t http_event_handler(esp_http_client_event_t *event) {
  auto *capture = static_cast<HttpCapture *>(event->user_data);
  if (capture == nullptr || event->event_id != HTTP_EVENT_ON_DATA ||
      event->data == nullptr || event->data_len <= 0) {
    return ESP_OK;
  }
  if (capture->body.size() + static_cast<size_t>(event->data_len) >
      capture->limit) {
    capture->overflow = true;
    return ESP_OK;
  }
  if (!capture->overflow) {
    capture->body.append(static_cast<const char *>(event->data), event->data_len);
  }
  return ESP_OK;
}

static size_t response_reserve(const std::string &action) {
  if (action == "devicelist/GetDevices3") {
    return active_client_details == ClientDetailsMode::NODES_ONLY
               ? 24 * 1024
               : 123 * 1024;
  }
  return 12 * 1024;
}

static bool compress_response(
    std::string &source,
    std::string &compressed,
    std::string *recycle_receive_buffer = nullptr) {
  compressed.clear();
  const size_t max_compressed_buffer =
      external_memory_size() > 0 ? 64 * 1024 : 31 * 1024;
  const size_t maximum = source.size() + source.size() / 255 + 32;
  const size_t output_capacity =
      std::min(maximum, max_compressed_buffer);
  const size_t hash_bytes =
      COMPRESSION_HASH_SIZE * sizeof(int32_t);
  const bool needs_output_allocation =
      compressed.capacity() < output_capacity;
  const size_t required_free =
      hash_bytes + 1024 +
      (needs_output_allocation ? output_capacity : 0);
  const size_t largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (required_free > heap_caps_get_free_size(MALLOC_CAP_8BIT) ||
      hash_bytes + 1024 > largest ||
      (needs_output_allocation && output_capacity + 1024 > largest)) {
    return false;
  }
  // Write directly into the string that will become the cache entry. Keeping
  // a separate temporary output allocation would briefly require two copies
  // of the compressed 110 KB device list on no-PSRAM targets.
  compressed.resize(output_capacity);
  auto *output = reinterpret_cast<unsigned char *>(compressed.data());
  auto *hash = static_cast<int32_t *>(heap_caps_malloc(
      hash_bytes,
      MALLOC_CAP_8BIT));
  if (hash == nullptr) {
    std::string().swap(compressed);
    return false;
  }
  std::fill_n(hash, COMPRESSION_HASH_SIZE, -1);

  size_t output_size = 0;
  const auto emit_byte = [&](unsigned char value) {
    if (output_size >= output_capacity) return false;
    output[output_size++] = value;
    return true;
  };
  const auto emit_bytes = [&](const char *data, size_t length) {
    if (length > output_capacity - output_size) return false;
    memcpy(output + output_size, data, length);
    output_size += length;
    return true;
  };
  const auto emit_length = [&](size_t length) {
    while (length >= 255) {
      if (!emit_byte(255)) return false;
      length -= 255;
    }
    return emit_byte(static_cast<unsigned char>(length));
  };
  const auto hash_at = [&source](size_t offset) {
    uint32_t value = 0;
    memcpy(&value, source.data() + offset, sizeof(value));
    return static_cast<size_t>(
        (value * 2654435761U) >> 21) & (COMPRESSION_HASH_SIZE - 1);
  };

  size_t anchor = 0;
  size_t offset = 0;
  while (offset + 4 <= source.size()) {
    const size_t slot = hash_at(offset);
    const int32_t previous = hash[slot];
    hash[slot] = static_cast<int32_t>(offset);
    if (previous < 0 ||
        offset - static_cast<size_t>(previous) > COMPRESSION_WINDOW ||
        memcmp(
            source.data() + previous,
            source.data() + offset,
            4) != 0) {
      offset++;
      continue;
    }

    size_t match = 4;
    while (offset + match < source.size() &&
           source[previous + match] == source[offset + match]) {
      match++;
    }
    const size_t literals = offset - anchor;
    const size_t token_index = output_size;
    if (!emit_byte(0)) {
      heap_caps_free(hash);
      std::string().swap(compressed);
      return false;
    }
    const unsigned char token = static_cast<unsigned char>(
        (std::min<size_t>(literals, 15) << 4) |
        std::min<size_t>(match - 4, 15));
    if ((literals >= 15 && !emit_length(literals - 15)) ||
        !emit_bytes(source.data() + anchor, literals)) {
      heap_caps_free(hash);
      std::string().swap(compressed);
      return false;
    }
    const uint16_t distance =
        static_cast<uint16_t>(offset - static_cast<size_t>(previous));
    if (!emit_byte(static_cast<unsigned char>(distance & 0xff)) ||
        !emit_byte(static_cast<unsigned char>(distance >> 8)) ||
        (match - 4 >= 15 && !emit_length(match - 4 - 15))) {
      heap_caps_free(hash);
      std::string().swap(compressed);
      return false;
    }
    output[token_index] = token;
    offset += match;
    anchor = offset;
  }

  const size_t literals = source.size() - anchor;
  if (!emit_byte(static_cast<unsigned char>(
          std::min<size_t>(literals, 15) << 4)) ||
      (literals >= 15 && !emit_length(literals - 15)) ||
      !emit_bytes(source.data() + anchor, literals)) {
    heap_caps_free(hash);
    std::string().swap(compressed);
    return false;
  }
  heap_caps_free(hash);
  compressed.resize(output_size);
  if (recycle_receive_buffer != nullptr) {
    recycle_receive_buffer->swap(source);
    recycle_receive_buffer->clear();
  } else {
    std::string().swap(source);
  }
  return true;
}

static bool decompress_response(
    const RawEntry &entry,
    std::string &response) {
  const size_t largest =
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (entry.response_size + 4 * 1024 > largest) {
    ESP_LOGW(
        TAG,
        "Unable to expand %s: need %u bytes; largest block is %u",
        entry.action.c_str(),
        static_cast<unsigned>(entry.response_size),
        static_cast<unsigned>(largest));
    return false;
  }
  response.resize(entry.response_size);
  const auto *input = reinterpret_cast<const unsigned char *>(
      entry.compressed_response.data());
  size_t input_offset = 0;
  size_t output_offset = 0;
  const auto read_length = [&](size_t initial, size_t &length) {
    length = initial;
    if (initial != 15) return true;
    while (input_offset < entry.compressed_response.size()) {
      const size_t value = input[input_offset++];
      length += value;
      if (value != 255) return true;
    }
    return false;
  };
  while (input_offset < entry.compressed_response.size()) {
    const unsigned char token = input[input_offset++];
    size_t literals = 0;
    if (!read_length(token >> 4, literals) ||
        input_offset + literals > entry.compressed_response.size() ||
        output_offset + literals > response.size()) {
      response.clear();
      return false;
    }
    memcpy(
        response.data() + output_offset,
        input + input_offset,
        literals);
    input_offset += literals;
    output_offset += literals;
    if (input_offset == entry.compressed_response.size()) break;
    if (input_offset + 2 > entry.compressed_response.size()) {
      response.clear();
      return false;
    }
    const size_t distance =
        input[input_offset] |
        (static_cast<size_t>(input[input_offset + 1]) << 8);
    input_offset += 2;
    size_t match = 0;
    if (distance == 0 || distance > output_offset ||
        !read_length(token & 0x0f, match)) {
      response.clear();
      return false;
    }
    match += 4;
    if (output_offset + match > response.size()) {
      response.clear();
      return false;
    }
    for (size_t index = 0; index < match; index++) {
      response[output_offset + index] =
          response[output_offset - distance + index];
    }
    output_offset += match;
  }
  if (output_offset != response.size()) {
    response.clear();
    return false;
  }
  return true;
}

static esp_err_t stream_decompressed_response(
    httpd_req_t *request,
    const RawEntry &entry) {
  static constexpr size_t SEND_BUFFER_SIZE = 2048;
  auto *workspace = static_cast<unsigned char *>(heap_caps_malloc(
      COMPRESSION_WINDOW + SEND_BUFFER_SIZE,
      MALLOC_CAP_8BIT));
  if (workspace == nullptr) {
    ESP_LOGW(
        TAG,
        "Unable to allocate stream workspace for %s",
        entry.action.c_str());
    return ESP_ERR_NO_MEM;
  }
  unsigned char *history = workspace;
  unsigned char *send_buffer = workspace + COMPRESSION_WINDOW;
  size_t send_size = 0;
  size_t output_size = 0;
  esp_err_t result = ESP_OK;

  const auto flush = [&]() {
    if (send_size == 0 || result != ESP_OK) return result == ESP_OK;
    result = httpd_resp_send_chunk(
        request,
        reinterpret_cast<const char *>(send_buffer),
        send_size);
    send_size = 0;
    return result == ESP_OK;
  };
  const auto emit = [&](unsigned char value) {
    if (output_size >= entry.response_size || result != ESP_OK) {
      result = ESP_FAIL;
      return false;
    }
    history[output_size % COMPRESSION_WINDOW] = value;
    send_buffer[send_size++] = value;
    output_size++;
    return send_size < SEND_BUFFER_SIZE || flush();
  };

  const auto *input = reinterpret_cast<const unsigned char *>(
      entry.compressed_response.data());
  size_t input_offset = 0;
  const auto read_length = [&](size_t initial, size_t &length) {
    length = initial;
    if (initial != 15) return true;
    while (input_offset < entry.compressed_response.size()) {
      const size_t value = input[input_offset++];
      length += value;
      if (value != 255) return true;
    }
    return false;
  };

  while (input_offset < entry.compressed_response.size() &&
         result == ESP_OK) {
    const unsigned char token = input[input_offset++];
    size_t literals = 0;
    if (!read_length(token >> 4, literals) ||
        input_offset + literals > entry.compressed_response.size()) {
      result = ESP_FAIL;
      break;
    }
    for (size_t index = 0; index < literals; index++) {
      if (!emit(input[input_offset++])) break;
    }
    if (result != ESP_OK ||
        input_offset == entry.compressed_response.size()) {
      break;
    }
    if (input_offset + 2 > entry.compressed_response.size()) {
      result = ESP_FAIL;
      break;
    }
    const size_t distance =
        input[input_offset] |
        (static_cast<size_t>(input[input_offset + 1]) << 8);
    input_offset += 2;
    size_t match = 0;
    if (distance == 0 || distance > output_size ||
        distance > COMPRESSION_WINDOW ||
        !read_length(token & 0x0f, match)) {
      result = ESP_FAIL;
      break;
    }
    match += 4;
    for (size_t index = 0; index < match; index++) {
      const unsigned char value =
          history[(output_size - distance) % COMPRESSION_WINDOW];
      if (!emit(value)) break;
    }
  }
  if (result == ESP_OK &&
      (output_size != entry.response_size || !flush())) {
    result = ESP_FAIL;
  }
  if (result != ESP_OK) {
    ESP_LOGW(
        TAG,
        "Topology stream %s stopped at compressed=%u/%u output=%u/%u: %s",
        entry.action.c_str(),
        static_cast<unsigned>(input_offset),
        static_cast<unsigned>(entry.compressed_response.size()),
        static_cast<unsigned>(output_size),
        static_cast<unsigned>(entry.response_size),
        esp_err_to_name(result));
  }
  heap_caps_free(workspace);
  return result;
}

static JnapResult jnap_request(
    const std::string &host,
    const std::string &action,
    const char *body = "{}",
    std::string *receive_workspace = nullptr,
    size_t response_limit = 0) {
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
  capture.limit =
      response_limit > 0
          ? response_limit
          : (external_memory_size() > 0
                 ? MAX_JNAP_RESPONSE
                 : response_reserve(action));
  if (receive_workspace != nullptr &&
      receive_workspace->capacity() >= capture.limit) {
    capture.body.swap(*receive_workspace);
    capture.body.clear();
  } else {
    const size_t largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const size_t reserve_headroom =
        response_limit > 0
            ? 4 * 1024
            : (action == "devicelist/GetDevices3"
                   ? 8 * 1024
                   : 24 * 1024);
    if (capture.limit + reserve_headroom > largest) {
      ESP_LOGW(
          TAG,
          "JNAP %s needs %u-byte receive reserve; largest block is %u",
          action.c_str(),
          static_cast<unsigned>(capture.limit),
          static_cast<unsigned>(largest));
      return result;
    }
    capture.body.reserve(capture.limit);
  }
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
    ESP_LOGW(
        TAG,
        "JNAP %s exceeded its %u-byte receive reserve",
        action.c_str(),
        static_cast<unsigned>(capture.limit));
  }
  result.body.swap(capture.body);
  esp_http_client_cleanup(client);
  return result;
}

static bool response_is_ok(const std::string &body) {
  // Avoid building a second, much larger cJSON tree while the raw device list
  // is still resident on no-PSRAM targets. JNAP places this scalar at the
  // top-level response object.
  const size_t key = body.find("\"result\"");
  if (key == std::string::npos) return false;
  size_t cursor = body.find(':', key + 8);
  if (cursor == std::string::npos) return false;
  cursor++;
  while (cursor < body.size() &&
         (body[cursor] == ' ' || body[cursor] == '\t' ||
          body[cursor] == '\r' || body[cursor] == '\n')) {
    cursor++;
  }
  return cursor + 4 <= body.size() &&
         body.compare(cursor, 4, "\"OK\"") == 0;
}

static const RawEntry *find_entry(
    const Snapshot &source,
    const char *action) {
  for (const auto &entry : source.entries) {
    if (entry.action == action) return &entry;
  }
  return nullptr;
}

static void reuse_snapshot_compression_buffer(
    const char *action,
    std::string &destination) {
  if (snapshot_mutex == nullptr ||
      xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return;
  }
  for (auto &entry : snapshot.entries) {
    if (entry.action == action) {
      destination.swap(entry.compressed_response);
      break;
    }
  }
  xSemaphoreGive(snapshot_mutex);
}

static cJSON *parse_response_root(const Snapshot &source, const char *action) {
  const RawEntry *entry = find_entry(source, action);
  if (entry == nullptr) return nullptr;
  std::string response;
  if (!decompress_response(*entry, response)) return nullptr;
  return cJSON_ParseWithLength(response.c_str(), response.size());
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

static std::string build_node_device_filter(
    const StatsAccumulator &accumulator,
    std::string &receive_workspace) {
  std::set<std::string> node_ids = accumulator.backhaul_ids;
  JnapResult nodes = jnap_request(
      router_host,
      "nodes/firmwareupdate/GetFirmwareUpdateStatus",
      "{}",
      &receive_workspace);
  if (!nodes.transport_ok || nodes.status != 200) {
    receive_workspace.swap(nodes.body);
    receive_workspace.clear();
    return {};
  }
  std::string response = std::move(nodes.body);
  cJSON *root = cJSON_ParseWithLength(response.c_str(), response.size());
  const cJSON *output = response_output(root);
  const cJSON *statuses =
      output != nullptr
          ? cJSON_GetObjectItemCaseSensitive(
                output,
                "firmwareUpdateStatus")
          : nullptr;
  if (cJSON_IsArray(statuses)) {
    const cJSON *status = nullptr;
    cJSON_ArrayForEach(status, statuses) {
      const char *id = json_string(status, "deviceUUID");
      if (id != nullptr) node_ids.insert(id);
    }
  }
  cJSON_Delete(root);
  receive_workspace.swap(response);
  receive_workspace.clear();
  if (node_ids.empty()) return {};

  cJSON *request = cJSON_CreateObject();
  cJSON *ids = cJSON_CreateArray();
  if (request == nullptr || ids == nullptr) {
    cJSON_Delete(ids);
    cJSON_Delete(request);
    return {};
  }
  cJSON_AddItemToObject(request, "deviceIDs", ids);
  for (const auto &id : node_ids) {
    cJSON_AddItemToArray(ids, cJSON_CreateString(id.c_str()));
  }
  char *serialized = cJSON_PrintUnformatted(request);
  std::string body = serialized ?: "";
  cJSON_free(serialized);
  cJSON_Delete(request);
  return body;
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

template<typename Callback>
static bool for_each_json_object_in_array(
    const std::string &document,
    const char *key,
    Callback callback) {
  const std::string marker = std::string("\"") + key + "\"";
  const size_t key_offset = document.find(marker);
  if (key_offset == std::string::npos) return false;
  size_t cursor = document.find('[', key_offset + marker.size());
  if (cursor == std::string::npos) return false;
  cursor++;

  while (cursor < document.size()) {
    while (cursor < document.size() &&
           (document[cursor] == ' ' || document[cursor] == '\t' ||
            document[cursor] == '\r' || document[cursor] == '\n' ||
            document[cursor] == ',')) {
      cursor++;
    }
    if (cursor >= document.size()) return false;
    if (document[cursor] == ']') return true;
    if (document[cursor] != '{') return false;

    const size_t start = cursor;
    size_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (; cursor < document.size(); cursor++) {
      const char value = document[cursor];
      if (in_string) {
        if (escaped) {
          escaped = false;
        } else if (value == '\\') {
          escaped = true;
        } else if (value == '"') {
          in_string = false;
        }
        continue;
      }
      if (value == '"') {
        in_string = true;
      } else if (value == '{') {
        depth++;
      } else if (value == '}') {
        if (depth == 0) return false;
        depth--;
        if (depth == 0) {
          const size_t length = cursor - start + 1;
          cJSON *item = cJSON_ParseWithLength(
              document.data() + start,
              length);
          if (item == nullptr) return false;
          const bool keep_going = callback(item);
          cJSON_Delete(item);
          if (!keep_going) return false;
          cursor++;
          break;
        }
      }
    }
    if (depth != 0) return false;
  }
  return false;
}

static cJSON *parse_raw_response(const std::string &response) {
  cJSON *root = cJSON_ParseWithLength(response.c_str(), response.size());
  return response_output(root) != nullptr ? root : (cJSON_Delete(root), nullptr);
}

static bool accumulate_connections(
    const std::string &response,
    bool grouped,
    StatsAccumulator &accumulator) {
  cJSON *root = parse_raw_response(response);
  if (root == nullptr) return false;
  const cJSON *output = response_output(root);
  if (grouped) {
    const cJSON *groups =
        cJSON_GetObjectItemCaseSensitive(output, "nodeWirelessConnections");
    if (!cJSON_IsArray(groups)) {
      cJSON_Delete(root);
      return false;
    }
    const cJSON *group = nullptr;
    cJSON_ArrayForEach(group, groups) {
      const cJSON *connections =
          cJSON_GetObjectItemCaseSensitive(group, "connections");
      if (!cJSON_IsArray(connections)) continue;
      const cJSON *connection = nullptr;
      cJSON_ArrayForEach(connection, connections) {
        const char *mac = json_string(connection, "macAddress");
        if (mac != nullptr) accumulator.live_macs.insert(mac);
      }
    }
  } else {
    const cJSON *connections =
        cJSON_GetObjectItemCaseSensitive(output, "connections");
    if (!cJSON_IsArray(connections)) {
      cJSON_Delete(root);
      return false;
    }
    const cJSON *connection = nullptr;
    cJSON_ArrayForEach(connection, connections) {
      const char *mac = json_string(connection, "macAddress");
      if (mac != nullptr) accumulator.live_macs.insert(mac);
    }
  }
  cJSON_Delete(root);
  return true;
}

static bool accumulate_backhaul(
    const std::string &response,
    StatsAccumulator &accumulator) {
  cJSON *root = parse_raw_response(response);
  if (root == nullptr) return false;
  const cJSON *output = response_output(root);
  const cJSON *backhauls =
      cJSON_GetObjectItemCaseSensitive(output, "backhaulDevices");
  if (!cJSON_IsArray(backhauls)) {
    cJSON_Delete(root);
    return false;
  }
  const cJSON *item = nullptr;
  cJSON_ArrayForEach(item, backhauls) {
    const char *id = json_string(item, "deviceUUID");
    if (id != nullptr) accumulator.backhaul_ids.insert(id);
    const cJSON *speed =
        cJSON_GetObjectItemCaseSensitive(item, "speedMbps");
    if (cJSON_IsNumber(speed)) {
      accumulator.backhaul_sum += static_cast<float>(speed->valuedouble);
    } else if (cJSON_IsString(speed)) {
      accumulator.backhaul_sum += strtof(speed->valuestring, nullptr);
    }
    const cJSON *wireless =
        cJSON_GetObjectItemCaseSensitive(item, "wirelessConnectionInfo");
    const cJSON *rssi =
        cJSON_IsObject(wireless)
            ? cJSON_GetObjectItemCaseSensitive(wireless, "stationRSSI")
            : nullptr;
    if ((!cJSON_IsNumber(rssi) || rssi->valuedouble == 0) &&
        cJSON_IsObject(wireless)) {
      rssi = cJSON_GetObjectItemCaseSensitive(wireless, "apRSSI");
    }
    if (cJSON_IsNumber(rssi) && rssi->valuedouble < -67) {
      accumulator.weak++;
    }
  }
  cJSON_Delete(root);
  return true;
}

static bool calculate_device_stats(
    const std::string &response,
    const StatsAccumulator &accumulator,
    MeshStats &stats) {
  std::set<std::string> node_ids;
  int authority_count = 0;
  int client_total = 0;
  int client_online = 0;
  const bool devices_ok = for_each_json_object_in_array(
      response,
      "devices",
      [&](const cJSON *device) {
        const char *id = json_string(device, "deviceID");
        const bool authority = json_bool(device, "isAuthority");
        const char *node_type = json_string(device, "nodeType");
        if (id != nullptr &&
            (authority || node_type != nullptr ||
             accumulator.backhaul_ids.count(id) != 0)) {
          node_ids.insert(id);
          if (authority) authority_count++;
          return true;
        }
        client_total++;
        const cJSON *connections =
            cJSON_GetObjectItemCaseSensitive(device, "connections");
        bool online =
            cJSON_IsArray(connections) && cJSON_GetArraySize(connections) > 0;
        if (!online) {
          std::set<std::string> macs;
          collect_device_macs(device, macs);
          for (const auto &mac : macs) {
            if (accumulator.live_macs.count(mac) != 0) {
              online = true;
              break;
            }
          }
        }
        if (online) client_online++;
        return true;
      });
  if (!devices_ok) return false;

  stats.nodes_total = static_cast<float>(node_ids.size());
  stats.nodes_online = static_cast<float>(
      authority_count + accumulator.backhaul_ids.size());
  stats.clients_online = static_cast<float>(client_online);
  stats.weak_nodes = static_cast<float>(accumulator.weak);
  stats.backhaul_mbps = accumulator.backhaul_sum;
  ESP_LOGI(
      TAG,
      "Topology stats nodes=%.0f/%.0f clients=%d/%d weak=%d backhaul=%.1f Mbps",
      stats.nodes_online,
      stats.nodes_total,
      client_online,
      client_total,
      accumulator.weak,
      accumulator.backhaul_sum);
  return true;
}

static bool collect_snapshot(
    Snapshot &candidate,
    std::string &device_receive_workspace,
    std::string &standard_receive_workspace) {
  resolve_client_details_mode();
  device_receive_workspace.clear();
  standard_receive_workspace.clear();
  if (external_memory_size() == 0) {
    const size_t device_capacity =
        response_reserve("devicelist/GetDevices3");
    if (device_receive_workspace.capacity() < device_capacity) {
      if (device_capacity + 1024 >
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) {
        ESP_LOGW(TAG, "Unable to reserve the device-list receive workspace");
        return false;
      }
      device_receive_workspace.reserve(device_capacity);
    }
    const size_t standard_capacity = response_reserve("");
    if (standard_receive_workspace.capacity() < standard_capacity) {
      if (standard_capacity + 1024 >
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) {
        ESP_LOGW(TAG, "Unable to reserve the standard receive workspace");
        return false;
      }
      standard_receive_workspace.reserve(standard_capacity);
    }
  }

  candidate.entries.clear();
  candidate.entries.reserve(sizeof(READ_ACTIONS) / sizeof(READ_ACTIONS[0]));
  bool devices_ok = false;
  bool stats_ok = true;
  StatsAccumulator stats_accumulator;
  for (const char *action : READ_ACTIONS) {
    const bool is_device_list =
        strcmp(action, "devicelist/GetDevices3") == 0;
    std::string *receive_workspace = nullptr;
    if (external_memory_size() == 0) {
      receive_workspace =
          is_device_list
              ? &device_receive_workspace
              : &standard_receive_workspace;
    }
    std::string request_body;
    if (is_device_list &&
        active_client_details == ClientDetailsMode::NODES_ONLY) {
      request_body = build_node_device_filter(
          stats_accumulator,
          standard_receive_workspace);
      if (request_body.empty()) {
        ESP_LOGW(TAG, "Unable to build the nodes-only device filter");
        return false;
      }
    }
    JnapResult result =
        jnap_request(
            router_host,
            action,
            request_body.empty() ? "{}" : request_body.c_str(),
            receive_workspace);
    if (!result.transport_ok || result.status != 200) {
      ESP_LOGW(TAG, "Topology action %s returned HTTP %d", action, result.status);
      return false;
    }
    std::string response = std::move(result.body);
    if (strcmp(action, "networkconnections/GetNetworkConnections2") == 0) {
      stats_ok =
          accumulate_connections(response, false, stats_accumulator) &&
          stats_ok;
    } else if (
        strcmp(
            action,
            "nodes/networkconnections/GetNodesWirelessNetworkConnections") ==
        0) {
      stats_ok =
          accumulate_connections(response, true, stats_accumulator) &&
          stats_ok;
    } else if (
        strcmp(action, "nodes/diagnostics/GetBackhaulInfo") == 0) {
      stats_ok = accumulate_backhaul(response, stats_accumulator) && stats_ok;
    } else if (strcmp(action, "devicelist/GetDevices3") == 0) {
      devices_ok = response_is_ok(response);
      stats_ok =
          calculate_device_stats(
              response,
              stats_accumulator,
              candidate.stats) &&
          stats_ok;
      if (active_client_details == ClientDetailsMode::NODES_ONLY) {
        candidate.stats.clients_online = NAN;
      }
    }
    RawEntry entry;
    entry.action = action;
    entry.response_size = response.size();
    if (is_device_list && external_memory_size() == 0) {
      // Reuse the previous generation's 31 KB compressed cache allocation.
      // Topology responses already wait on memory_mutex while collection is in
      // progress, so no client can observe the brief hand-off.
      reuse_snapshot_compression_buffer(
          action,
          entry.compressed_response);
    }
    std::string *recycle_receive_buffer =
        external_memory_size() == 0 ? receive_workspace : nullptr;
    if (!compress_response(
            response,
            entry.compressed_response,
            recycle_receive_buffer)) {
      ESP_LOGW(TAG, "Topology action %s could not be compressed", action);
      return false;
    }
    ESP_LOGI(
        TAG,
        "Topology action %s cached %u -> %u bytes; free=%u largest=%u",
        action,
        static_cast<unsigned>(entry.response_size),
        static_cast<unsigned>(entry.compressed_response.size()),
        static_cast<unsigned>(
            heap_caps_get_free_size(MALLOC_CAP_8BIT)),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    candidate.entries.push_back(std::move(entry));
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  if (!devices_ok) {
    ESP_LOGW(TAG, "Topology refresh rejected: device list was not authorized");
    return false;
  }
  if (!stats_ok) {
    ESP_LOGW(TAG, "Topology snapshot cached without complete HA statistics");
  }
  candidate.updated_at = iso_timestamp();
  candidate.ready = true;
  return true;
}

static void collector_task(void *) {
  uint32_t generation = 0;
  // The no-PSRAM targets need one large, contiguous receive block for the
  // device list. Keep it for the lifetime of the collector task so repeated
  // refreshes cannot fragment that block into smaller heap allocations.
  std::string device_receive_workspace;
  std::string standard_receive_workspace;
  while (true) {
    if (!wifi_connected()) {
      router_connected.store(false, std::memory_order_release);
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
      continue;
    }
    Snapshot candidate;
    const bool memory_locked =
        memory_mutex != nullptr &&
        xSemaphoreTake(memory_mutex, pdMS_TO_TICKS(15000)) == pdTRUE;
    if (!memory_locked) {
      ESP_LOGW(TAG, "Topology refresh timed out waiting for memory workspace");
      router_connected.store(false, std::memory_order_release);
    } else if (collect_snapshot(
                   candidate,
                   device_receive_workspace,
                   standard_receive_workspace)) {
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
            static_cast<unsigned>(external_memory_free()));
        App.wake_loop_threadsafe();
      }
    } else {
      router_connected.store(false, std::memory_order_release);
    }
    if (memory_locked) xSemaphoreGive(memory_mutex);
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
  if (!authorize_web_request(request)) return ESP_OK;
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
  if (!authorize_web_request(request)) return ESP_OK;
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
  const bool connected = router_connected.load(std::memory_order_acquire);
  cJSON *root = cJSON_CreateObject();
  cJSON_AddBoolToObject(root, "connected", connected);
  cJSON_AddBoolToObject(root, "snapshotReady", ready);
  cJSON_AddBoolToObject(root, "demo", false);
  cJSON_AddBoolToObject(root, "managedConnection", true);
  cJSON_AddStringToObject(root, "router", router_host.c_str());
  cJSON_AddStringToObject(root, "edgeAddress", ip.c_str());
  cJSON_AddStringToObject(root, "edgeUrl", ip.empty() ? "" : ("http://" + ip + "/").c_str());
  cJSON_AddStringToObject(root, "cachedAt", cached_at.c_str());
  cJSON_AddNumberToObject(root, "generation", generation);
  cJSON_AddStringToObject(
      root,
      "clientDetails",
      client_details_name(active_client_details));
  cJSON_AddStringToObject(
      root,
      "clientDetailsRequested",
      client_details_name(requested_client_details));
  char *body = cJSON_PrintUnformatted(root);
  const esp_err_t result = send_json(request, body ?: "{}");
  cJSON_free(body);
  cJSON_Delete(root);
  return result;
}

static esp_err_t topology_handler(httpd_req_t *request) {
  if (!authorize_web_request(request)) return ESP_OK;
  if (memory_mutex == nullptr ||
      xSemaphoreTake(memory_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
    return send_error_json(
        request,
        "503 Service Unavailable",
        "The topology workspace is temporarily busy.");
  }
  if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    xSemaphoreGive(memory_mutex);
    return send_error_json(request, "503 Service Unavailable", "The topology cache is temporarily busy.");
  }
  if (!snapshot.ready) {
    xSemaphoreGive(snapshot_mutex);
    xSemaphoreGive(memory_mutex);
    return send_error_json(request, "503 Service Unavailable", "The ESP32 is loading its first topology snapshot.");
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
      "\",\"routerConnected\":" +
      (router_connected.load(std::memory_order_acquire) ? "true" : "false") +
      ",\"generation\":" + std::to_string(snapshot.generation) +
      ",\"clientDetails\":\"" +
      client_details_name(active_client_details) +
      "\"" +
      "},\"rawJnap\":{";
  esp_err_t result = httpd_resp_send_chunk(request, prefix.c_str(), prefix.size());
  bool first = true;
  for (const auto &entry : snapshot.entries) {
    if (result != ESP_OK) break;
    std::string key = (first ? "\"" : ",\"") + entry.action + "\":";
    result = httpd_resp_send_chunk(request, key.c_str(), key.size());
    if (result == ESP_OK) {
      result = stream_decompressed_response(request, entry);
    }
    first = false;
  }
  if (result == ESP_OK) result = httpd_resp_send_chunk(request, "}}", 2);
  if (result == ESP_OK) result = httpd_resp_send_chunk(request, nullptr, 0);
  xSemaphoreGive(snapshot_mutex);
  xSemaphoreGive(memory_mutex);
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
  if (!authorize_web_request(request)) return ESP_OK;
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
  if (!authorize_web_request(request)) return ESP_OK;
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
  if (memory_mutex == nullptr ||
      xSemaphoreTake(memory_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
    return false;
  }
  if (xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(3000)) != pdTRUE) {
    xSemaphoreGive(memory_mutex);
    return false;
  }
  cJSON *backhaul_root = parse_response_root(snapshot, "nodes/diagnostics/GetBackhaulInfo");
  const cJSON *backhaul_output = response_output(backhaul_root);
  const cJSON *backhauls =
      backhaul_output
          ? cJSON_GetObjectItemCaseSensitive(backhaul_output, "backhaulDevices")
          : nullptr;
  cJSON *filter = cJSON_CreateObject();
  cJSON *ids = cJSON_CreateArray();
  if (filter == nullptr || ids == nullptr) {
    cJSON_Delete(ids);
    cJSON_Delete(filter);
    cJSON_Delete(backhaul_root);
    xSemaphoreGive(snapshot_mutex);
    xSemaphoreGive(memory_mutex);
    return false;
  }
  cJSON_AddItemToObject(filter, "deviceIDs", ids);
  cJSON_AddItemToArray(ids, cJSON_CreateString(node_id.c_str()));
  char *serialized = cJSON_PrintUnformatted(filter);
  const JnapResult filtered = jnap_request(
      router_host,
      "devicelist/GetDevices3",
      serialized ?: "{}",
      nullptr,
      8 * 1024);
  cJSON_free(serialized);
  cJSON_Delete(filter);

  cJSON *filtered_root =
      filtered.transport_ok && filtered.status == 200
          ? cJSON_ParseWithLength(
                filtered.body.c_str(),
                filtered.body.size())
          : nullptr;
  const cJSON *filtered_output = response_output(filtered_root);
  const cJSON *devices =
      filtered_output != nullptr
          ? cJSON_GetObjectItemCaseSensitive(filtered_output, "devices")
          : nullptr;
  const cJSON *device =
      cJSON_IsArray(devices) && cJSON_GetArraySize(devices) == 1
          ? cJSON_GetArrayItem(devices, 0)
          : nullptr;
  bool found =
      cJSON_IsObject(device) &&
      node_id == (json_string(device, "deviceID") ?: "");
  if (found) {
    node.id = node_id;
    node.name = device_name(device);
    node.authority = json_bool(device, "isAuthority");
    node.online = node.authority;
    if (node.authority && private_ipv4(router_host)) {
      node.ip = router_host;
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
  const bool recognized_node =
      found &&
      (node.authority ||
       json_string(device, "nodeType") != nullptr ||
       node.online);
  cJSON_Delete(filtered_root);
  cJSON_Delete(backhaul_root);
  xSemaphoreGive(snapshot_mutex);
  xSemaphoreGive(memory_mutex);
  return recognized_node && node.online && !node.ip.empty();
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
  if (!authorize_web_request(request)) return ESP_OK;
  std::string node_id;
  if (!query_value(request, "nodeId", node_id)) {
    return send_error_json(request, "400 Bad Request", "Node ID is required.");
  }
  NodeInfo node;
  if (!resolve_node(node_id, node)) {
    return send_error_json(request, "400 Bad Request", "The node is offline or absent from the current topology.");
  }

  if (memory_mutex == nullptr ||
      xSemaphoreTake(memory_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
    return send_error_json(
        request,
        "503 Service Unavailable",
        "The node workspace is temporarily busy.");
  }
  struct MemoryRelease {
    SemaphoreHandle_t handle;
    ~MemoryRelease() { xSemaphoreGive(handle); }
  } memory_release{memory_mutex};

  const JnapResult check = jnap_request(
      node.ip,
      "core/CheckAdminPassword",
      "{}",
      nullptr,
      1024);
  const JnapResult identity_response = jnap_request(
      node.ip,
      "core/GetDeviceInfo",
      "{}",
      nullptr,
      8 * 1024);
  const JnapResult mode_response = jnap_request(
      node.ip,
      "nodes/smartmode/GetDeviceMode",
      "{}",
      nullptr,
      1024);
  const JnapResult optimization_response =
      jnap_request(
          node.ip,
          "nodes/topologyoptimization/GetTopologyOptimizationSettings2",
          "{}",
          nullptr,
          1024);
  const bool credentials_ok = check.transport_ok && check.status == 200 &&
                              response_is_ok(check.body);
  cJSON *identity = parse_output_copy(identity_response);
  cJSON *mode = parse_output_copy(mode_response);
  cJSON *optimization = parse_output_copy(optimization_response);
  if (identity == nullptr) {
    cJSON_Delete(mode);
    cJSON_Delete(optimization);
    return send_error_json(request, "502 Bad Gateway", "Unable to read the node identity.");
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
      "The exact-parent data path is confirmed in firmware, but ordinary JNAP exposes no transport.");
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
  if (!authorize_web_request(request)) return ESP_OK;
  const size_t content_type_length =
      httpd_req_get_hdr_value_len(request, "Content-Type");
  std::vector<char> content_type(content_type_length + 1);
  if (content_type_length == 0 ||
      httpd_req_get_hdr_value_str(
          request, "Content-Type", content_type.data(), content_type.size()) != ESP_OK ||
      strstr(content_type.data(), "application/json") == nullptr) {
    return send_error_json(request, "415 Unsupported Media Type", "The request must use JSON.");
  }
  std::string request_body;
  if (!read_request_json(request, request_body)) {
    return send_error_json(request, "400 Bad Request", "The restart request is invalid.");
  }
  cJSON *payload = cJSON_ParseWithLength(request_body.c_str(), request_body.size());
  const char *node_id = payload != nullptr ? json_string(payload, "nodeId") : nullptr;
  if (node_id == nullptr || node_id[0] == '\0') {
    cJSON_Delete(payload);
    return send_error_json(request, "400 Bad Request", "Node ID is required.");
  }
  NodeInfo node;
  if (!resolve_node(node_id, node)) {
    cJSON_Delete(payload);
    return send_error_json(request, "400 Bad Request", "The node is offline or absent from the current topology.");
  }
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  const auto cooldown = restart_cooldowns.find(node.id);
  if (cooldown != restart_cooldowns.end() && now - cooldown->second < RESTART_COOLDOWN_MS) {
    cJSON_Delete(payload);
    return send_error_json(request, "409 Conflict", "The node is already restarting. Wait for it to recover.");
  }
  restart_cooldowns[node.id] = now;
  if (memory_mutex == nullptr ||
      xSemaphoreTake(memory_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
    restart_cooldowns.erase(node.id);
    cJSON_Delete(payload);
    return send_error_json(
        request,
        "503 Service Unavailable",
        "The node workspace is temporarily busy.");
  }
  const JnapResult reboot = jnap_request(
      node.ip,
      "core/Reboot",
      "{}",
      nullptr,
      1024);
  xSemaphoreGive(memory_mutex);
  if (!reboot.transport_ok || reboot.status != 200 || !response_is_ok(reboot.body)) {
    restart_cooldowns.erase(node.id);
    cJSON_Delete(payload);
    return send_error_json(request, "502 Bad Gateway", "The node did not accept the restart request.");
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
  if (!authorize_web_request(request)) return ESP_OK;
  return send_error_json(request, "404 Not Found", "Endpoint not found.");
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

static void setup(
    const char *host,
    const char *password_b64,
    const char *web_username,
    const char *web_password,
    const char *client_details) {
  router_host = host ?: "";
  requested_client_details = parse_client_details(client_details);
  client_details_resolved = false;
  const std::string encoded_password = password_b64 ?: "";
  const bool router_password_ok =
      encoded_password != "SET_IN_LOCAL_CONFIG" &&
      base64_decode_string(encoded_password, router_password);
  authorization = base64_basic_auth("admin", router_password);
  web_authorization = base64_basic_auth(
      web_username ?: "",
      web_password ?: "");
  snapshot_mutex = xSemaphoreCreateMutex();
  jnap_mutex = xSemaphoreCreateMutex();
  memory_mutex = xSemaphoreCreateMutex();
  if (!router_password_ok || snapshot_mutex == nullptr || jnap_mutex == nullptr ||
      memory_mutex == nullptr ||
      authorization.empty() || web_authorization.empty() ||
      (web_username ?: "")[0] == '\0' ||
      (web_password ?: "")[0] == '\0' ||
      strcmp(web_username ?: "", "SET_IN_LOCAL_CONFIG") == 0 ||
      strcmp(web_password ?: "", "SET_IN_LOCAL_CONFIG") == 0) {
    ESP_LOGE(TAG, "MeshScope initialization failed");
    return;
  }
  ESP_LOGI(
      TAG,
      "MeshScope ESPHome edge starting; assets=%u source=%s PSRAM=%u",
      static_cast<unsigned>(meshscope_web_assets::ASSET_COUNT),
      meshscope_web_assets::SOURCE_SHA256,
      static_cast<unsigned>(external_memory_size()));
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

inline void meshscope_edge_setup(
    const char *host,
    const char *password_b64,
    const char *web_username,
    const char *web_password,
    const char *client_details) {
  meshscope_edge::setup(
      host,
      password_b64,
      web_username,
      web_password,
      client_details);
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
  return ip.empty() ? std::string("Waiting for Wi-Fi") : "http://" + ip + "/";
}

inline std::string meshscope_edge_summary() {
  const meshscope_edge::MeshStats stats = meshscope_edge::stats_copy();
  if (std::isnan(stats.nodes_online)) return "Waiting for first topology";
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
  return value.empty() ? std::string("Waiting for first topology") : value;
}
