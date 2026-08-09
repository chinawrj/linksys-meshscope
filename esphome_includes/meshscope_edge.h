#pragma once

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
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
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#if defined(CONFIG_SPIRAM)
#include "esp_psram.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
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
static constexpr uint32_t TOPOLOGY_LOCK_ACTION_COOLDOWN_MS = 5 * 60 * 1000;
static constexpr uint8_t TOPOLOGY_LOCK_CONFIRMATIONS = 3;
static constexpr size_t TOPOLOGY_LOCK_MAX_NODES = 32;
static constexpr size_t TOPOLOGY_LOCK_HISTORY_LIMIT = 8;
static constexpr uint32_t REFRESH_WAIT_MS = 20000;
static constexpr uint16_t MQTT_PORT = 1883;
static constexpr uint32_t MQTT_PROBE_INTERVAL_MS = 5 * 60 * 1000;
static constexpr uint32_t MQTT_OPERATION_TIMEOUT_MS = 20000;
static constexpr uint32_t MQTT_OPERATION_DEDUP_MS = 60 * 1000;
static constexpr uint32_t MQTT_VERIFICATION_TIMEOUT_MS = 180 * 1000;
static constexpr uint8_t MQTT_VERIFY_GENERATIONS = 2;
static constexpr size_t MQTT_PACKET_LIMIT_INTERNAL = 16 * 1024;
static constexpr size_t MQTT_PACKET_LIMIT_EXTERNAL = 32 * 1024;

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

struct BackhaulObservation {
  std::string ip;
  std::string parent_ip;
  std::string connection_type;
  std::string band;
  std::string parent_bssid;
  int channel = 0;
};

struct StatsAccumulator {
  std::set<std::string> backhaul_ids;
  std::set<std::string> live_macs;
  std::map<std::string, BackhaulObservation> backhauls;
  float backhaul_sum = 0;
  int weak = 0;
};

struct NodeObservation {
  std::string id;
  std::string name;
  std::string ip;
  std::string parent_id;
  std::string connection_type;
  std::string backhaul_band;
  std::string parent_bssid;
  int backhaul_channel = 0;
  bool authority = false;
  bool online = false;
};

struct Snapshot {
  std::vector<RawEntry> entries;
  std::vector<NodeObservation> nodes;
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

struct LockedParent {
  std::string node_id;
  std::string parent_id;
};

struct TopologyLockAction {
  std::string node_id;
  std::string node_name;
  std::string expected_parent_id;
  std::string expected_parent_name;
  std::string current_parent_id;
  std::string current_parent_name;
  std::string requested_at;
  uint32_t uptime_ms = 0;
  bool accepted = false;
};

struct WebSession {
  std::string token;
  uint32_t last_seen_ms = 0;
};

enum class ClientDetailsMode {
  AUTO,
  FULL,
  NODES_ONLY,
};

enum class MqttSteeringMode : uint8_t {
  AUTO = 0,
  FORCE_ON = 1,
  FORCE_OFF = 2,
};

struct MqttRadioTarget {
  std::string bssid;
  int channel = 0;
  bool valid = false;
  bool matched_uuid = false;
  bool saw_bssid = false;
  bool saw_channel = false;
  uint16_t devinfo_records = 0;
  std::string source;
};

struct MqttSteeringOperation {
  uint32_t id = 0;
  std::string child_id;
  std::string child_name;
  std::string parent_id;
  std::string parent_name;
  std::string previous_parent_id;
  std::string band;
  std::string state = "idle";
  std::string detail;
  std::string requested_at;
  uint32_t started_ms = 0;
  uint32_t published_generation = 0;
  uint32_t last_verified_generation = 0;
  uint8_t verification_generations = 0;
  uint8_t consecutive_matches = 0;
};

static std::string router_host;
static std::string router_password;
static std::string authorization;
static std::string dashboard_password;
static Snapshot snapshot;
static SemaphoreHandle_t snapshot_mutex = nullptr;
static SemaphoreHandle_t jnap_mutex = nullptr;
static SemaphoreHandle_t memory_mutex = nullptr;
static SemaphoreHandle_t topology_lock_mutex = nullptr;
static SemaphoreHandle_t web_session_mutex = nullptr;
static SemaphoreHandle_t mqtt_steering_mutex = nullptr;
static TaskHandle_t collector_task_handle = nullptr;
static TaskHandle_t mqtt_steering_task_handle = nullptr;
static httpd_handle_t server = nullptr;
static std::atomic<bool> force_refresh{false};
static std::atomic<bool> router_connected{false};
static std::map<std::string, uint32_t> restart_cooldowns;
static bool topology_lock_enabled = false;
static std::string topology_lock_saved_at;
static std::vector<LockedParent> topology_lock_mappings;
static std::map<std::string, uint8_t> topology_lock_mismatch_counts;
static std::vector<TopologyLockAction> topology_lock_history;
static std::string topology_lock_last_selected_node_id;
static uint64_t topology_lock_last_action_epoch = 0;
static bool topology_lock_last_action_unknown_time = false;
static uint32_t topology_lock_last_action_uptime_ms = 0;
static bool topology_lock_action_seen_this_boot = false;
static constexpr const char *TOPOLOGY_LOCK_NVS_NAMESPACE = "meshscope_lock";
static constexpr const char *TOPOLOGY_LOCK_NVS_KEY = "config";
static ClientDetailsMode requested_client_details = ClientDetailsMode::AUTO;
static ClientDetailsMode active_client_details = ClientDetailsMode::FULL;
static bool client_details_resolved = false;
static std::vector<WebSession> web_sessions;
static constexpr size_t WEB_SESSION_LIMIT = 4;
static constexpr uint32_t WEB_SESSION_IDLE_MS = 24 * 60 * 60 * 1000;
static constexpr const char *WEB_SESSION_COOKIE = "meshscope_session";
static constexpr const char *MQTT_NVS_NAMESPACE = "meshscope_mqtt";
static constexpr const char *MQTT_NVS_MODE_KEY = "mode";
static MqttSteeringMode mqtt_default_mode = MqttSteeringMode::AUTO;
static MqttSteeringMode mqtt_mode = MqttSteeringMode::AUTO;
static bool mqtt_available = false;
static std::string mqtt_capability_reason = "Waiting for the first probe";
static std::string mqtt_capability_proof;
static std::string mqtt_last_probe_at;
static uint32_t mqtt_last_probe_ms = 0;
static bool mqtt_probe_requested = false;
static bool mqtt_operation_pending = false;
static MqttSteeringOperation mqtt_operation;
static std::map<std::string, uint32_t> mqtt_child_cooldowns;
static uint32_t mqtt_next_operation_id = 0;

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

static const char *mqtt_mode_name(MqttSteeringMode mode) {
  switch (mode) {
    case MqttSteeringMode::FORCE_ON:
      return "force-on";
    case MqttSteeringMode::FORCE_OFF:
      return "force-off";
    default:
      return "auto";
  }
}

static MqttSteeringMode parse_mqtt_mode(const char *value) {
  if (strcmp(value ?: "", "force-on") == 0) {
    return MqttSteeringMode::FORCE_ON;
  }
  if (strcmp(value ?: "", "force-off") == 0) {
    return MqttSteeringMode::FORCE_OFF;
  }
  return MqttSteeringMode::AUTO;
}

static std::string ascii_lower(std::string value) {
  std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

static std::string ascii_upper(std::string value) {
  std::transform(
      value.begin(),
      value.end(),
      value.begin(),
      [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
  return value;
}

static bool same_node_id(const std::string &left, const std::string &right) {
  return ascii_lower(left) == ascii_lower(right);
}

static bool persist_mqtt_mode(MqttSteeringMode mode) {
  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open(MQTT_NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (result == ESP_OK) {
    result = nvs_set_u8(handle, MQTT_NVS_MODE_KEY, static_cast<uint8_t>(mode));
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
  }
  if (result != ESP_OK) {
    ESP_LOGW(TAG, "Unable to persist MQTT steering mode: %s", esp_err_to_name(result));
  }
  return result == ESP_OK;
}

static void load_mqtt_mode() {
  mqtt_mode = mqtt_default_mode;
  nvs_handle_t handle = 0;
  if (nvs_open(MQTT_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
  uint8_t stored = 0;
  const esp_err_t result = nvs_get_u8(handle, MQTT_NVS_MODE_KEY, &stored);
  nvs_close(handle);
  if (result == ESP_OK && stored <= static_cast<uint8_t>(MqttSteeringMode::FORCE_OFF)) {
    mqtt_mode = static_cast<MqttSteeringMode>(stored);
  }
  ESP_LOGI(
      TAG,
      "MQTT Parent steering mode=%s default=%s",
      mqtt_mode_name(mqtt_mode),
      mqtt_mode_name(mqtt_default_mode));
}

static size_t external_memory_size();
static bool private_ipv4(const std::string &value);
static void request_refresh();
static uint32_t uptime_ms();
static std::vector<NodeObservation> current_node_observations();

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

struct MqttWirePacket {
  uint8_t type = 0;
  uint8_t flags = 0;
  std::string body;
};

static size_t mqtt_packet_limit() {
  return external_memory_size() > 0
             ? MQTT_PACKET_LIMIT_EXTERNAL
             : MQTT_PACKET_LIMIT_INTERNAL;
}

static void mqtt_append_text(std::string &output, const std::string &value) {
  output.push_back(static_cast<char>((value.size() >> 8) & 0xff));
  output.push_back(static_cast<char>(value.size() & 0xff));
  output.append(value);
}

static void mqtt_append_varint(std::string &output, size_t value) {
  do {
    uint8_t byte = value % 128;
    value /= 128;
    if (value > 0) byte |= 0x80;
    output.push_back(static_cast<char>(byte));
  } while (value > 0);
}

static bool mqtt_send_all(int socket_fd, const char *data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    const int sent = lwip_send(socket_fd, data + offset, size - offset, 0);
    if (sent < 0 && errno == EINTR) continue;
    if (sent <= 0) return false;
    offset += static_cast<size_t>(sent);
  }
  return true;
}

static bool mqtt_recv_all(int socket_fd, char *data, size_t size) {
  size_t offset = 0;
  while (offset < size) {
    const int received = lwip_recv(socket_fd, data + offset, size - offset, 0);
    if (received < 0 && errno == EINTR) continue;
    if (received <= 0) return false;
    offset += static_cast<size_t>(received);
  }
  return true;
}

static bool mqtt_json_scalar(
    const std::string &document,
    size_t offset,
    const char *key,
    std::string &value) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t cursor = document.find(needle, offset);
  if (cursor == std::string::npos) return false;
  cursor = document.find(':', cursor + needle.size());
  if (cursor == std::string::npos) return false;
  cursor++;
  while (cursor < document.size() &&
         std::isspace(static_cast<unsigned char>(document[cursor]))) {
    cursor++;
  }
  if (cursor >= document.size()) return false;
  value.clear();
  if (document[cursor] == '"') {
    cursor++;
    while (cursor < document.size()) {
      const char character = document[cursor++];
      if (character == '"') return true;
      if (character == '\\') {
        if (cursor >= document.size()) return false;
        value.push_back(document[cursor++]);
      } else {
        value.push_back(character);
      }
      if (value.size() > 160) return false;
    }
    return false;
  }
  const size_t start = cursor;
  while (cursor < document.size() &&
         document[cursor] != ',' && document[cursor] != '}' &&
         !std::isspace(static_cast<unsigned char>(document[cursor]))) {
    cursor++;
  }
  if (cursor == start || cursor - start > 32) return false;
  value.assign(document, start, cursor - start);
  return true;
}

static bool mqtt_valid_bssid(std::string &value) {
  value = ascii_lower(value);
  if (value.size() != 17) return false;
  unsigned first_octet = 0;
  for (size_t index = 0; index < value.size(); index++) {
    if (index % 3 == 2) {
      if (value[index] != ':') return false;
    } else if (!std::isxdigit(static_cast<unsigned char>(value[index]))) {
      return false;
    }
  }
  if (sscanf(value.c_str(), "%2x", &first_octet) != 1 || (first_octet & 1) != 0) {
    return false;
  }
  return value != "00:00:00:00:00:00";
}

static bool mqtt_devinfo_from_packet(
    const MqttWirePacket &packet,
    const std::string &wanted_parent_id,
    bool &saw_devinfo,
    MqttRadioTarget *target,
    const std::string &band) {
  if (packet.type != 3 || packet.body.size() < 2) return false;
  const size_t topic_size =
      (static_cast<uint8_t>(packet.body[0]) << 8) |
      static_cast<uint8_t>(packet.body[1]);
  size_t payload_offset = 2 + topic_size;
  if (payload_offset > packet.body.size()) return false;
  const std::string topic = packet.body.substr(2, topic_size);
  if (topic.size() < 8 || topic.compare(topic.size() - 8, 8, "/DEVINFO") != 0) {
    return false;
  }
  const uint8_t qos = (packet.flags >> 1) & 0x03;
  if (qos > 0) {
    if (payload_offset + 2 > packet.body.size()) return false;
    payload_offset += 2;
  }
  std::string uuid;
  if (!mqtt_json_scalar(packet.body, payload_offset, "uuid", uuid) || uuid.empty()) {
    return false;
  }
  saw_devinfo = true;
  if (target != nullptr) target->devinfo_records++;
  if (target == nullptr || wanted_parent_id.empty() ||
      !same_node_id(uuid, wanted_parent_id)) {
    return true;
  }
  target->matched_uuid = true;
  const std::string prefix = band == "5GL" ? "userAp5GL" : "userAp5GH";
  std::string bssid;
  std::string channel_text;
  target->saw_bssid = mqtt_json_scalar(
      packet.body,
      payload_offset,
      (prefix + "_bssid").c_str(),
      bssid);
  target->saw_channel = mqtt_json_scalar(
      packet.body,
      payload_offset,
      (prefix + "_channel").c_str(),
      channel_text);
  if (!target->saw_bssid || !target->saw_channel || !mqtt_valid_bssid(bssid)) {
    return true;
  }
  char *end = nullptr;
  const long channel = strtol(channel_text.c_str(), &end, 10);
  if (end == channel_text.c_str() || *end != '\0' || channel < 1 || channel > 196) {
    return true;
  }
  target->bssid = bssid;
  target->channel = static_cast<int>(channel);
  target->valid = true;
  target->source = "fresh Parent DEVINFO";
  return true;
}

static bool mqtt_radio_from_observed_child(
    const MqttSteeringOperation &operation,
    MqttRadioTarget &target) {
  for (const auto &node : current_node_observations()) {
    if (!node.online || !same_node_id(node.parent_id, operation.parent_id) ||
        node.backhaul_band != operation.band ||
        node.backhaul_channel < 1 || node.backhaul_channel > 196) {
      continue;
    }
    std::string bssid = node.parent_bssid;
    if (!mqtt_valid_bssid(bssid)) continue;
    target.bssid = bssid;
    target.channel = node.backhaul_channel;
    target.valid = true;
    target.source = "current JNAP backhaul observation";
    return true;
  }
  return false;
}

class MqttWireSession {
 public:
  ~MqttWireSession() { close(); }

  bool connect_to_router(std::string &error) {
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *addresses = nullptr;
    char port[8];
    snprintf(port, sizeof(port), "%u", static_cast<unsigned>(MQTT_PORT));
    const int resolved = getaddrinfo(router_host.c_str(), port, &hints, &addresses);
    if (resolved != 0 || addresses == nullptr) {
      error = "Unable to resolve the local MQTT broker";
      if (addresses != nullptr) freeaddrinfo(addresses);
      return false;
    }
    socket_fd_ = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket_fd_ < 0) {
      freeaddrinfo(addresses);
      error = "Unable to allocate an MQTT socket";
      return false;
    }
    struct timeval timeout = {};
    timeout.tv_sec = 4;
    lwip_setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    lwip_setsockopt(socket_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // Bind the broker connection to Wi-Fi so an optional WireGuard interface
    // cannot steal Linksys LAN traffic when the tunnel is used for inbound UI.
    esp_netif_t *wifi = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t wifi_info = {};
    if (wifi != nullptr && esp_netif_get_ip_info(wifi, &wifi_info) == ESP_OK &&
        wifi_info.ip.addr != 0) {
      struct sockaddr_in local = {};
      local.sin_family = AF_INET;
      local.sin_addr.s_addr = wifi_info.ip.addr;
      local.sin_port = 0;
      if (lwip_bind(
              socket_fd_,
              reinterpret_cast<struct sockaddr *>(&local),
              sizeof(local)) != 0) {
        freeaddrinfo(addresses);
        error = "Unable to bind MQTT to the Wi-Fi interface";
        close();
        return false;
      }
    }
    const int connected = lwip_connect(
        socket_fd_, addresses->ai_addr, addresses->ai_addrlen);
    freeaddrinfo(addresses);
    if (connected != 0) {
      error = "Unable to connect to MQTT at " + router_host + ":1883";
      close();
      return false;
    }

    std::string body;
    mqtt_append_text(body, "MQTT");
    body.push_back(4);
    body.push_back(2);
    body.push_back(0);
    body.push_back(30);
    char client_id[24];
    snprintf(
        client_id,
        sizeof(client_id),
        "meshscope-%08x",
        static_cast<unsigned>(esp_random()));
    mqtt_append_text(body, client_id);
    if (!send_packet(0x10, body)) {
      error = "Unable to send MQTT CONNECT";
      return false;
    }
    MqttWirePacket response;
    if (!read_packet(response) || response.type != 2 || response.body.size() != 2 ||
        static_cast<uint8_t>(response.body[1]) != 0) {
      error = "The MQTT broker rejected the connection";
      return false;
    }
    return true;
  }

  bool subscribe_devinfo(std::string &error) {
    const uint16_t packet_id = next_packet_id();
    std::string body;
    body.push_back(static_cast<char>((packet_id >> 8) & 0xff));
    body.push_back(static_cast<char>(packet_id & 0xff));
    mqtt_append_text(body, "network/+/DEVINFO");
    body.push_back(0);
    if (!send_packet(0x82, body)) {
      error = "Unable to send the MQTT subscription";
      return false;
    }
    const uint32_t deadline = uptime_ms() + 5000;
    while (static_cast<int32_t>(deadline - uptime_ms()) > 0) {
      MqttWirePacket packet;
      if (!read_packet(packet)) continue;
      if (packet.type != 9) continue;
      if (packet.body.size() < 3 ||
          ((static_cast<uint8_t>(packet.body[0]) << 8) |
           static_cast<uint8_t>(packet.body[1])) != packet_id ||
          static_cast<uint8_t>(packet.body[2]) == 0x80) {
        error = "The MQTT DEVINFO subscription was denied by the router ACL";
        return false;
      }
      return true;
    }
    error = "The MQTT subscription timed out";
    return false;
  }

  bool publish(
      const std::string &topic,
      const std::string &payload,
      bool &saw_devinfo,
      MqttRadioTarget *target,
      const std::string &wanted_parent_id,
      const std::string &band,
      std::string &error) {
    const uint16_t packet_id = next_packet_id();
    std::string body;
    mqtt_append_text(body, topic);
    body.push_back(static_cast<char>((packet_id >> 8) & 0xff));
    body.push_back(static_cast<char>(packet_id & 0xff));
    body.append(payload);
    if (!send_packet(0x32, body)) {
      error = "Unable to publish to the Linksys MQTT broker";
      return false;
    }
    const uint32_t deadline = uptime_ms() + 6000;
    while (static_cast<int32_t>(deadline - uptime_ms()) > 0) {
      MqttWirePacket packet;
      if (!read_packet(packet)) continue;
      if (packet.type == 3) {
        mqtt_devinfo_from_packet(
            packet, wanted_parent_id, saw_devinfo, target, band);
        continue;
      }
      if (packet.type == 4 && packet.body.size() == 2 &&
          (((static_cast<uint8_t>(packet.body[0]) << 8) |
            static_cast<uint8_t>(packet.body[1])) == packet_id)) {
        return true;
      }
    }
    error = "The Linksys MQTT broker did not acknowledge the publish";
    return false;
  }

  bool wait_for_devinfo(
      const std::string &wanted_parent_id,
      const std::string &band,
      uint32_t timeout_ms,
      bool &saw_devinfo,
      MqttRadioTarget *target) {
    const uint32_t deadline = uptime_ms() + timeout_ms;
    while (static_cast<int32_t>(deadline - uptime_ms()) > 0) {
      MqttWirePacket packet;
      if (!read_packet(packet)) continue;
      mqtt_devinfo_from_packet(
          packet, wanted_parent_id, saw_devinfo, target, band);
      if (target != nullptr ? target->valid : saw_devinfo) return true;
    }
    return false;
  }

 private:
  int socket_fd_ = -1;
  uint16_t packet_id_ = 0;

  void close() {
    if (socket_fd_ >= 0) {
      const std::string empty;
      send_packet(0xE0, empty);
      lwip_close(socket_fd_);
      socket_fd_ = -1;
    }
  }

  uint16_t next_packet_id() {
    packet_id_ = packet_id_ == 65535 ? 1 : packet_id_ + 1;
    return packet_id_;
  }

  bool send_packet(uint8_t header, const std::string &body) {
    std::string packet;
    packet.reserve(body.size() + 5);
    packet.push_back(static_cast<char>(header));
    mqtt_append_varint(packet, body.size());
    packet.append(body);
    return mqtt_send_all(socket_fd_, packet.data(), packet.size());
  }

  bool read_packet(MqttWirePacket &packet) {
    char first = 0;
    if (!mqtt_recv_all(socket_fd_, &first, 1)) return false;
    size_t remaining = 0;
    size_t multiplier = 1;
    for (int index = 0; index < 4; index++) {
      char encoded = 0;
      if (!mqtt_recv_all(socket_fd_, &encoded, 1)) return false;
      remaining += (static_cast<uint8_t>(encoded) & 0x7f) * multiplier;
      if ((static_cast<uint8_t>(encoded) & 0x80) == 0) break;
      if (index == 3) return false;
      multiplier *= 128;
    }
    if (remaining > mqtt_packet_limit() ||
        remaining + 24 * 1024 > heap_caps_get_free_size(MALLOC_CAP_8BIT)) {
      ESP_LOGW(TAG, "MQTT packet rejected at %u bytes", static_cast<unsigned>(remaining));
      return false;
    }
    packet.type = static_cast<uint8_t>(first) >> 4;
    packet.flags = static_cast<uint8_t>(first) & 0x0f;
    packet.body.resize(remaining);
    return remaining == 0 ||
           mqtt_recv_all(socket_fd_, packet.body.data(), packet.body.size());
  }
};

static bool mqtt_refresh_devinfo(
    MqttWireSession &session,
    const std::string &wanted_parent_id,
    const std::string &band,
    bool &saw_devinfo,
    MqttRadioTarget *target,
    std::string &error) {
  static const char *const topics[] = {
      "network/status_resend_all",
      "network/DEVINFO/status_resend_all",
      "network/BH/status_resend_all",
  };
  for (const char *topic : topics) {
    if (!session.publish(
            topic,
            "",
            saw_devinfo,
            target,
            wanted_parent_id,
            band,
            error)) {
      return false;
    }
  }
  return true;
}

static bool mqtt_probe_roundtrip(std::string &error) {
  MqttWireSession session;
  if (!session.connect_to_router(error) || !session.subscribe_devinfo(error)) {
    return false;
  }
  bool saw_devinfo = false;
  if (!mqtt_refresh_devinfo(session, "", "5GH", saw_devinfo, nullptr, error)) {
    return false;
  }
  if (!saw_devinfo &&
      !session.wait_for_devinfo("", "5GH", 8000, saw_devinfo, nullptr)) {
    error = "The broker accepted the probe but returned no fresh DEVINFO";
    return false;
  }
  return saw_devinfo;
}

static bool mqtt_publish_parent_request(
    const MqttSteeringOperation &operation,
    MqttRadioTarget &target,
    std::string &error) {
  MqttWireSession session;
  if (!session.connect_to_router(error) || !session.subscribe_devinfo(error)) {
    return false;
  }
  bool saw_devinfo = false;
  if (!mqtt_refresh_devinfo(
          session,
          operation.parent_id,
          operation.band,
          saw_devinfo,
          &target,
          error)) {
    return false;
  }
  if (!target.valid) mqtt_radio_from_observed_child(operation, target);
  if (!target.valid) {
    session.wait_for_devinfo(
        operation.parent_id,
        operation.band,
        MQTT_OPERATION_TIMEOUT_MS,
        saw_devinfo,
        &target);
  }
  if (!target.valid) {
    if (!target.matched_uuid) {
      error = "No fresh DEVINFO matched the requested Parent after " +
              std::to_string(target.devinfo_records) + " records";
    } else {
      error = "The requested Parent DEVINFO did not contain a valid " +
              operation.band + " radio tuple (BSSID field " +
              (target.saw_bssid ? "present" : "missing") +
              ", channel field " +
              (target.saw_channel ? "present" : "missing") + ")";
    }
    return false;
  }
  if (!target.valid) {
    error = "The requested Parent did not report a valid " + operation.band + " radio";
    return false;
  }
  bool publish_allowed = false;
  if (mqtt_steering_mutex != nullptr &&
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    publish_allowed = mqtt_mode != MqttSteeringMode::FORCE_OFF &&
                      mqtt_operation.id == operation.id;
    if (publish_allowed) {
      mqtt_operation.state = "publishing";
      mqtt_operation.detail = "Publishing the exact Parent request to BH/config";
    }
    xSemaphoreGive(mqtt_steering_mutex);
  }
  if (!publish_allowed) {
    error = "MQTT Parent steering was turned off before publish";
    return false;
  }
  const std::string child_id = ascii_upper(operation.child_id);
  const std::string topic = "network/" + child_id + "/BH/config";
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) {
    error = "Unable to allocate the Parent steering payload";
    return false;
  }
  cJSON_AddStringToObject(root, "uuid", child_id.c_str());
  cJSON_AddStringToObject(root, "type", "set");
  cJSON_AddStringToObject(root, "TS", iso_timestamp().c_str());
  cJSON *data = cJSON_AddObjectToObject(root, "data");
  cJSON_AddStringToObject(data, "band", operation.band.c_str());
  cJSON_AddStringToObject(data, "bssid", target.bssid.c_str());
  cJSON_AddStringToObject(data, "channel", std::to_string(target.channel).c_str());
  char *serialized = cJSON_PrintUnformatted(root);
  const std::string payload = serialized ?: "";
  cJSON_free(serialized);
  cJSON_Delete(root);
  if (payload.empty()) {
    error = "Unable to allocate the Parent steering payload";
    return false;
  }
  return session.publish(
      topic,
      payload,
      saw_devinfo,
      nullptr,
      "",
      operation.band,
      error);
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

static bool constant_time_equal(
    const std::string &left,
    const std::string &right) {
  if (left.size() != right.size()) return false;
  unsigned char difference = 0;
  for (size_t index = 0; index < left.size(); index++) {
    difference |= static_cast<unsigned char>(left[index]) ^
                  static_cast<unsigned char>(right[index]);
  }
  return difference == 0;
}

static std::string request_session_token(httpd_req_t *request) {
  const size_t length = httpd_req_get_hdr_value_len(request, "Cookie");
  if (length == 0 || length > 1024) return {};
  std::vector<char> header(length + 1);
  if (httpd_req_get_hdr_value_str(
          request,
          "Cookie",
          header.data(),
          header.size()) != ESP_OK) {
    return {};
  }
  const std::string cookies(header.data());
  const std::string prefix = std::string(WEB_SESSION_COOKIE) + "=";
  size_t offset = 0;
  while (offset < cookies.size()) {
    while (offset < cookies.size() &&
           (cookies[offset] == ' ' || cookies[offset] == ';')) {
      offset++;
    }
    const size_t end = cookies.find(';', offset);
    const size_t size =
        (end == std::string::npos ? cookies.size() : end) - offset;
    if (size > prefix.size() &&
        cookies.compare(offset, prefix.size(), prefix) == 0) {
      return cookies.substr(offset + prefix.size(), size - prefix.size());
    }
    if (end == std::string::npos) break;
    offset = end + 1;
  }
  return {};
}

static void prune_web_sessions_locked(uint32_t now) {
  web_sessions.erase(
      std::remove_if(
          web_sessions.begin(),
          web_sessions.end(),
          [now](const WebSession &session) {
            return now - session.last_seen_ms >= WEB_SESSION_IDLE_MS;
          }),
      web_sessions.end());
}

static bool request_has_web_session(httpd_req_t *request) {
  const std::string token = request_session_token(request);
  if (token.empty() || web_session_mutex == nullptr ||
      xSemaphoreTake(web_session_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    return false;
  }
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  prune_web_sessions_locked(now);
  bool authorized = false;
  for (auto &session : web_sessions) {
    if (constant_time_equal(session.token, token)) {
      session.last_seen_ms = now;
      authorized = true;
      break;
    }
  }
  xSemaphoreGive(web_session_mutex);
  return authorized;
}

static std::string create_web_session() {
  unsigned char bytes[32] = {};
  esp_fill_random(bytes, sizeof(bytes));
  static constexpr char HEX[] = "0123456789abcdef";
  std::string token(sizeof(bytes) * 2, '0');
  for (size_t index = 0; index < sizeof(bytes); index++) {
    token[index * 2] = HEX[bytes[index] >> 4];
    token[index * 2 + 1] = HEX[bytes[index] & 0x0f];
  }
  if (web_session_mutex == nullptr ||
      xSemaphoreTake(web_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return {};
  }
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
  prune_web_sessions_locked(now);
  if (web_sessions.size() >= WEB_SESSION_LIMIT) {
    web_sessions.erase(web_sessions.begin());
  }
  web_sessions.push_back({token, now});
  xSemaphoreGive(web_session_mutex);
  return token;
}

static void remove_web_session(httpd_req_t *request) {
  const std::string token = request_session_token(request);
  if (token.empty() || web_session_mutex == nullptr ||
      xSemaphoreTake(web_session_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    return;
  }
  web_sessions.erase(
      std::remove_if(
          web_sessions.begin(),
          web_sessions.end(),
          [&token](const WebSession &session) {
            return constant_time_equal(session.token, token);
          }),
      web_sessions.end());
  xSemaphoreGive(web_session_mutex);
}

static bool authorize_web_request(httpd_req_t *request) {
  if (request_has_web_session(request)) return true;
  httpd_resp_set_status(request, "401 Unauthorized");
  httpd_resp_set_type(request, "application/json; charset=utf-8");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_send(
      request,
      "{\"error\":\"MeshScope dashboard login required.\",\"code\":\"authentication_required\"}",
      HTTPD_RESP_USE_STRLEN);
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
    if (id != nullptr) {
      accumulator.backhaul_ids.insert(id);
      BackhaulObservation &observed = accumulator.backhauls[id];
      observed.ip = json_string(item, "ipAddress") ?: "";
      observed.parent_ip = json_string(item, "parentIPAddress") ?: "";
      observed.connection_type = json_string(item, "connectionType") ?: "";
      const cJSON *wireless =
          cJSON_GetObjectItemCaseSensitive(item, "wirelessConnectionInfo");
      if (cJSON_IsObject(wireless)) {
        observed.band = json_string(wireless, "radioID") ?: "";
        observed.parent_bssid = json_string(wireless, "apBSSID") ?: "";
        const cJSON *channel =
            cJSON_GetObjectItemCaseSensitive(wireless, "channel");
        if (cJSON_IsNumber(channel)) {
          observed.channel = channel->valueint;
        } else if (cJSON_IsString(channel)) {
          observed.channel = static_cast<int>(strtol(channel->valuestring, nullptr, 10));
        }
      }
    }
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
    MeshStats &stats,
    std::vector<NodeObservation> &observed_nodes) {
  std::set<std::string> node_ids;
  int client_total = 0;
  int client_online = 0;
  observed_nodes.clear();
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
          NodeObservation observed;
          observed.id = id;
          observed.name = device_name(device);
          observed.authority = authority;
          observed.online = authority || accumulator.backhaul_ids.count(id) != 0;
          const cJSON *connections =
              cJSON_GetObjectItemCaseSensitive(device, "connections");
          if (cJSON_IsArray(connections)) {
            const cJSON *connection = nullptr;
            cJSON_ArrayForEach(connection, connections) {
              const char *ip = json_string(connection, "ipAddress");
              if (ip != nullptr && private_ipv4(ip)) {
                observed.ip = ip;
                break;
              }
            }
          }
          const auto backhaul = accumulator.backhauls.find(id);
          if (backhaul != accumulator.backhauls.end() &&
              private_ipv4(backhaul->second.ip)) {
            observed.ip = backhaul->second.ip;
          }
          if (backhaul != accumulator.backhauls.end()) {
            observed.connection_type = backhaul->second.connection_type;
            observed.backhaul_band = backhaul->second.band;
            observed.parent_bssid = backhaul->second.parent_bssid;
            observed.backhaul_channel = backhaul->second.channel;
          }
          if (authority && observed.ip.empty() && private_ipv4(router_host)) {
            observed.ip = router_host;
          }
          observed_nodes.push_back(std::move(observed));
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

  std::map<std::string, std::string> node_by_ip;
  for (const auto &node : observed_nodes) {
    if (!node.ip.empty()) node_by_ip[node.ip] = node.id;
  }
  int online_nodes = 0;
  for (auto &node : observed_nodes) {
    if (node.online) online_nodes++;
    if (node.authority) continue;
    const auto backhaul = accumulator.backhauls.find(node.id);
    if (backhaul == accumulator.backhauls.end()) continue;
    const auto parent = node_by_ip.find(backhaul->second.parent_ip);
    if (parent != node_by_ip.end()) node.parent_id = parent->second;
  }

  stats.nodes_total = static_cast<float>(node_ids.size());
  stats.nodes_online = static_cast<float>(online_nodes);
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

static uint32_t uptime_ms() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

static uint64_t wall_clock_epoch() {
  const time_t now = ::time(nullptr);
  return now >= 1700000000 ? static_cast<uint64_t>(now) : 0;
}

static const NodeObservation *find_observed_node(
    const std::vector<NodeObservation> &nodes,
    const std::string &id) {
  for (const auto &node : nodes) {
    if (node.id == id) return &node;
  }
  return nullptr;
}

static bool mqtt_operation_is_active(const MqttSteeringOperation &operation) {
  return operation.state == "queued" ||
         operation.state == "probing" ||
         operation.state == "discovering-target" ||
         operation.state == "publishing" ||
         operation.state == "verification-pending";
}

static bool mqtt_operation_active_for_node(const std::string &node_id) {
  bool active = false;
  if (mqtt_steering_mutex != nullptr &&
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    active = mqtt_operation_is_active(mqtt_operation) &&
             same_node_id(mqtt_operation.child_id, node_id);
    xSemaphoreGive(mqtt_steering_mutex);
  }
  return active;
}

static bool mqtt_topology_lock_conflict(
    const std::string &child_id,
    const std::string &parent_id,
    std::string &error) {
  if (topology_lock_mutex == nullptr ||
      xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    error = "Topology Lock is temporarily busy";
    return true;
  }
  bool conflict = false;
  if (topology_lock_enabled) {
    for (const auto &mapping : topology_lock_mappings) {
      if (!same_node_id(mapping.node_id, child_id)) continue;
      if (!same_node_id(mapping.parent_id, parent_id)) {
        conflict = true;
        error = "Topology Lock expects a different Parent for this Node";
      }
      break;
    }
  }
  xSemaphoreGive(topology_lock_mutex);
  return conflict;
}

static void mqtt_verify_operation(
    const std::vector<NodeObservation> &nodes,
    uint32_t generation) {
  if (mqtt_steering_mutex == nullptr ||
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    return;
  }
  if (mqtt_operation.state != "verification-pending" ||
      generation <= mqtt_operation.published_generation ||
      generation == mqtt_operation.last_verified_generation) {
    xSemaphoreGive(mqtt_steering_mutex);
    return;
  }
  mqtt_operation.last_verified_generation = generation;
  mqtt_operation.verification_generations++;
  const NodeObservation *child = nullptr;
  for (const auto &node : nodes) {
    if (same_node_id(node.id, mqtt_operation.child_id)) {
      child = &node;
      break;
    }
  }
  if (child != nullptr && child->online &&
      same_node_id(child->parent_id, mqtt_operation.parent_id)) {
    mqtt_operation.consecutive_matches++;
  } else {
    mqtt_operation.consecutive_matches = 0;
  }
  if (mqtt_operation.consecutive_matches >= MQTT_VERIFY_GENERATIONS) {
    mqtt_operation.state = "verified";
    mqtt_operation.detail = "The requested Parent matched two consecutive topology generations";
  } else if (uptime_ms() - mqtt_operation.started_ms >=
             MQTT_VERIFICATION_TIMEOUT_MS) {
    mqtt_operation.state = "failed";
    mqtt_operation.detail =
        "The requested Parent was not confirmed within 180 seconds";
  }
  xSemaphoreGive(mqtt_steering_mutex);
  App.wake_loop_threadsafe();
}

static uint32_t topology_lock_remaining_ms_locked() {
  if (topology_lock_action_seen_this_boot) {
    const uint32_t elapsed = uptime_ms() - topology_lock_last_action_uptime_ms;
    return elapsed >= TOPOLOGY_LOCK_ACTION_COOLDOWN_MS
               ? 0
               : TOPOLOGY_LOCK_ACTION_COOLDOWN_MS - elapsed;
  }
  if (topology_lock_last_action_epoch == 0) return 0;
  const uint64_t now = wall_clock_epoch();
  if (now == 0) return TOPOLOGY_LOCK_ACTION_COOLDOWN_MS;
  const uint64_t elapsed_seconds =
      now >= topology_lock_last_action_epoch
          ? now - topology_lock_last_action_epoch
          : 0;
  const uint64_t cooldown_seconds = TOPOLOGY_LOCK_ACTION_COOLDOWN_MS / 1000;
  return elapsed_seconds >= cooldown_seconds
             ? 0
             : static_cast<uint32_t>(
                   (cooldown_seconds - elapsed_seconds) * 1000ULL);
}

static bool persist_topology_lock_locked() {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return false;
  cJSON_AddNumberToObject(root, "version", 1);
  cJSON_AddBoolToObject(root, "enabled", topology_lock_enabled);
  cJSON_AddStringToObject(root, "lockedAt", topology_lock_saved_at.c_str());
  cJSON_AddNumberToObject(
      root,
      "lastActionEpoch",
      static_cast<double>(topology_lock_last_action_epoch));
  cJSON_AddBoolToObject(
      root,
      "lastActionUnknownTime",
      topology_lock_last_action_unknown_time);
  cJSON *nodes = cJSON_AddArrayToObject(root, "nodes");
  for (const auto &mapping : topology_lock_mappings) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "nodeId", mapping.node_id.c_str());
    cJSON_AddStringToObject(item, "parentId", mapping.parent_id.c_str());
    cJSON_AddItemToArray(nodes, item);
  }
  char *serialized = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (serialized == nullptr) return false;
  nvs_handle_t handle = 0;
  esp_err_t result = nvs_open(
      TOPOLOGY_LOCK_NVS_NAMESPACE,
      NVS_READWRITE,
      &handle);
  if (result == ESP_OK) {
    result = nvs_set_str(handle, TOPOLOGY_LOCK_NVS_KEY, serialized);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
  }
  cJSON_free(serialized);
  if (result != ESP_OK) {
    ESP_LOGW(TAG, "Unable to persist topology lock: %s", esp_err_to_name(result));
    return false;
  }
  return true;
}

static void load_topology_lock() {
  nvs_handle_t handle = 0;
  if (nvs_open(
          TOPOLOGY_LOCK_NVS_NAMESPACE,
          NVS_READONLY,
          &handle) != ESP_OK) {
    return;
  }
  size_t length = 0;
  esp_err_t result = nvs_get_str(
      handle,
      TOPOLOGY_LOCK_NVS_KEY,
      nullptr,
      &length);
  if (result != ESP_OK || length < 3 || length > 8192) {
    nvs_close(handle);
    return;
  }
  std::vector<char> data(length);
  result = nvs_get_str(
      handle,
      TOPOLOGY_LOCK_NVS_KEY,
      data.data(),
      &length);
  nvs_close(handle);
  if (result != ESP_OK) return;
  cJSON *root = cJSON_Parse(data.data());
  if (root == nullptr) return;
  std::vector<LockedParent> loaded;
  const cJSON *nodes = cJSON_GetObjectItemCaseSensitive(root, "nodes");
  const cJSON *item = nullptr;
  if (cJSON_IsArray(nodes)) {
    cJSON_ArrayForEach(item, nodes) {
      const char *node_id = json_string(item, "nodeId");
      const char *parent_id = json_string(item, "parentId");
      if (node_id == nullptr || parent_id == nullptr ||
          node_id[0] == '\0' || parent_id[0] == '\0' ||
          strlen(node_id) > 128 || strlen(parent_id) > 128 ||
          loaded.size() >= TOPOLOGY_LOCK_MAX_NODES) {
        loaded.clear();
        break;
      }
      loaded.push_back({node_id, parent_id});
    }
  }
  topology_lock_mappings = std::move(loaded);
  topology_lock_enabled =
      json_bool(root, "enabled") && !topology_lock_mappings.empty();
  topology_lock_saved_at = json_string(root, "lockedAt") ?: "";
  const cJSON *last_action =
      cJSON_GetObjectItemCaseSensitive(root, "lastActionEpoch");
  if (cJSON_IsNumber(last_action) && last_action->valuedouble > 0) {
    topology_lock_last_action_epoch =
        static_cast<uint64_t>(last_action->valuedouble);
  }
  topology_lock_last_action_unknown_time =
      json_bool(root, "lastActionUnknownTime");
  if (topology_lock_last_action_unknown_time) {
    topology_lock_action_seen_this_boot = true;
    topology_lock_last_action_uptime_ms = uptime_ms();
  }
  cJSON_Delete(root);
  ESP_LOGI(
      TAG,
      "Topology lock restored: enabled=%s nodes=%u",
      topology_lock_enabled ? "true" : "false",
      static_cast<unsigned>(topology_lock_mappings.size()));
}

static bool validate_topology_lock_mappings(
    const std::vector<NodeObservation> &nodes,
    const std::vector<LockedParent> &mappings,
    std::string &error) {
  std::map<std::string, const NodeObservation *> online;
  std::string authority_id;
  size_t expected_children = 0;
  for (const auto &node : nodes) {
    if (!node.online) continue;
    online[node.id] = &node;
    if (node.authority) {
      authority_id = node.id;
    } else {
      expected_children++;
    }
  }
  if (authority_id.empty()) {
    error = "The online gateway is unavailable.";
    return false;
  }
  if (mappings.empty() || mappings.size() != expected_children ||
      mappings.size() > TOPOLOGY_LOCK_MAX_NODES) {
    error = "The lock must define one parent for every online child node.";
    return false;
  }
  std::map<std::string, std::string> parents;
  for (const auto &mapping : mappings) {
    const auto child = online.find(mapping.node_id);
    const auto parent = online.find(mapping.parent_id);
    if (child == online.end() || parent == online.end()) {
      error = "Every locked node and parent must be online when the lock is applied.";
      return false;
    }
    if (child->second->authority) {
      error = "The gateway cannot be assigned a parent.";
      return false;
    }
    if (mapping.node_id == mapping.parent_id) {
      error = "A node cannot be its own parent.";
      return false;
    }
    if (!parents.emplace(mapping.node_id, mapping.parent_id).second) {
      error = "A node has more than one desired parent.";
      return false;
    }
  }
  for (const auto &node : online) {
    if (node.second->authority) continue;
    if (parents.count(node.first) == 0) {
      error = "The lock is missing an online child node.";
      return false;
    }
  }
  for (const auto &mapping : mappings) {
    std::set<std::string> seen{mapping.node_id};
    std::string cursor = mapping.parent_id;
    while (cursor != authority_id) {
      if (!seen.insert(cursor).second) {
        error = "The desired parent relationships contain a cycle.";
        return false;
      }
      const auto next = parents.find(cursor);
      if (next == parents.end()) {
        error = "Every desired parent path must lead to the gateway.";
        return false;
      }
      cursor = next->second;
    }
  }
  return true;
}

static void add_topology_lock_history_locked(TopologyLockAction action) {
  topology_lock_history.insert(
      topology_lock_history.begin(),
      std::move(action));
  if (topology_lock_history.size() > TOPOLOGY_LOCK_HISTORY_LIMIT) {
    topology_lock_history.resize(TOPOLOGY_LOCK_HISTORY_LIMIT);
  }
}

static cJSON *topology_lock_json(
    const std::vector<NodeObservation> &nodes) {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return nullptr;
  if (topology_lock_mutex == nullptr ||
      xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    cJSON_AddBoolToObject(root, "enabled", false);
    cJSON_AddStringToObject(root, "state", "busy");
    return root;
  }
  const uint32_t remaining_ms = topology_lock_remaining_ms_locked();
  cJSON_AddBoolToObject(root, "supported", true);
  cJSON_AddBoolToObject(root, "enabled", topology_lock_enabled);
  cJSON_AddStringToObject(
      root,
      "state",
      topology_lock_enabled ? "monitoring" : "unlocked");
  cJSON_AddStringToObject(root, "lockedAt", topology_lock_saved_at.c_str());
  cJSON_AddNumberToObject(
      root,
      "cooldownSeconds",
      TOPOLOGY_LOCK_ACTION_COOLDOWN_MS / 1000);
  cJSON_AddNumberToObject(
      root,
      "nextActionInSeconds",
      (remaining_ms + 999) / 1000);
  cJSON_AddNumberToObject(
      root,
      "confirmationsRequired",
      TOPOLOGY_LOCK_CONFIRMATIONS);
  cJSON_AddNumberToObject(
      root,
      "monitorIntervalSeconds",
      REFRESH_INTERVAL_MS / 1000);
  cJSON *summary = cJSON_AddObjectToObject(root, "summary");
  int correct = 0;
  int mismatch = 0;
  int blocked = 0;
  int offline = 0;
  cJSON *items = cJSON_AddArrayToObject(root, "nodes");
  for (const auto &mapping : topology_lock_mappings) {
    const NodeObservation *node = find_observed_node(nodes, mapping.node_id);
    const NodeObservation *parent = find_observed_node(nodes, mapping.parent_id);
    const uint8_t confirmations =
        topology_lock_mismatch_counts.count(mapping.node_id)
            ? topology_lock_mismatch_counts[mapping.node_id]
            : 0;
    const bool recovering =
        !topology_lock_history.empty() &&
        topology_lock_history.front().node_id == mapping.node_id &&
        topology_lock_history.front().accepted &&
        uptime_ms() - topology_lock_history.front().uptime_ms < RESTART_COOLDOWN_MS;
    const char *status = "correct";
    if (node == nullptr || !node->online) {
      status = recovering ? "recovering" : "node-offline";
      offline++;
    } else if (parent == nullptr || !parent->online) {
      status = "parent-offline";
      blocked++;
    } else if (node->parent_id == mapping.parent_id) {
      correct++;
    } else if (confirmations < TOPOLOGY_LOCK_CONFIRMATIONS) {
      status = "confirming";
      mismatch++;
    } else if (remaining_ms > 0) {
      status = "cooldown";
      mismatch++;
    } else {
      status = "restart-ready";
      mismatch++;
    }
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "nodeId", mapping.node_id.c_str());
    cJSON_AddStringToObject(
        item,
        "name",
        node != nullptr ? node->name.c_str() : mapping.node_id.c_str());
    cJSON_AddStringToObject(item, "expectedParentId", mapping.parent_id.c_str());
    cJSON_AddStringToObject(
        item,
        "expectedParentName",
        parent != nullptr ? parent->name.c_str() : mapping.parent_id.c_str());
    cJSON_AddStringToObject(
        item,
        "currentParentId",
        node != nullptr ? node->parent_id.c_str() : "");
    const NodeObservation *current_parent =
        node != nullptr ? find_observed_node(nodes, node->parent_id) : nullptr;
    cJSON_AddStringToObject(
        item,
        "currentParentName",
        current_parent != nullptr ? current_parent->name.c_str() : "");
    cJSON_AddBoolToObject(item, "nodeOnline", node != nullptr && node->online);
    cJSON_AddBoolToObject(
        item,
        "expectedParentOnline",
        parent != nullptr && parent->online);
    cJSON_AddStringToObject(item, "status", status);
    cJSON_AddNumberToObject(item, "confirmations", confirmations);
    cJSON_AddNumberToObject(
        item,
        "actionInSeconds",
        strcmp(status, "cooldown") == 0 ? (remaining_ms + 999) / 1000 : 0);
    cJSON_AddItemToArray(items, item);
  }
  cJSON_AddNumberToObject(summary, "total", topology_lock_mappings.size());
  cJSON_AddNumberToObject(summary, "correct", correct);
  cJSON_AddNumberToObject(summary, "mismatch", mismatch);
  cJSON_AddNumberToObject(summary, "blocked", blocked);
  cJSON_AddNumberToObject(summary, "offline", offline);
  cJSON *history = cJSON_AddArrayToObject(root, "history");
  for (const auto &action : topology_lock_history) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "nodeId", action.node_id.c_str());
    cJSON_AddStringToObject(item, "name", action.node_name.c_str());
    cJSON_AddStringToObject(
        item,
        "expectedParentId",
        action.expected_parent_id.c_str());
    cJSON_AddStringToObject(
        item,
        "expectedParentName",
        action.expected_parent_name.c_str());
    cJSON_AddStringToObject(
        item,
        "currentParentId",
        action.current_parent_id.c_str());
    cJSON_AddStringToObject(
        item,
        "currentParentName",
        action.current_parent_name.c_str());
    cJSON_AddStringToObject(item, "requestedAt", action.requested_at.c_str());
    cJSON_AddBoolToObject(item, "accepted", action.accepted);
    cJSON_AddItemToArray(history, item);
  }
  xSemaphoreGive(topology_lock_mutex);
  return root;
}

static void evaluate_topology_lock(
    const std::vector<NodeObservation> &nodes) {
  if (topology_lock_mutex == nullptr ||
      xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return;
  }
  if (!topology_lock_enabled) {
    xSemaphoreGive(topology_lock_mutex);
    return;
  }
  LockedParent selected;
  NodeObservation selected_node;
  NodeObservation selected_parent;
  const uint32_t remaining_ms = topology_lock_remaining_ms_locked();
  for (const auto &mapping : topology_lock_mappings) {
    const NodeObservation *node = find_observed_node(nodes, mapping.node_id);
    const NodeObservation *parent = find_observed_node(nodes, mapping.parent_id);
    if (node == nullptr || parent == nullptr || !node->online ||
        !parent->online || node->authority || node->ip.empty() ||
        node->parent_id == mapping.parent_id ||
        mqtt_operation_active_for_node(mapping.node_id)) {
      topology_lock_mismatch_counts[mapping.node_id] = 0;
      continue;
    }
    uint8_t &count = topology_lock_mismatch_counts[mapping.node_id];
    if (count < TOPOLOGY_LOCK_CONFIRMATIONS) count++;
  }
  if (remaining_ms == 0 && !topology_lock_mappings.empty()) {
    size_t start = 0;
    for (size_t index = 0; index < topology_lock_mappings.size(); index++) {
      if (topology_lock_mappings[index].node_id ==
          topology_lock_last_selected_node_id) {
        start = (index + 1) % topology_lock_mappings.size();
        break;
      }
    }
    for (size_t offset = 0; offset < topology_lock_mappings.size(); offset++) {
      const LockedParent &mapping =
          topology_lock_mappings[
              (start + offset) % topology_lock_mappings.size()];
      const NodeObservation *node = find_observed_node(nodes, mapping.node_id);
      const NodeObservation *parent = find_observed_node(nodes, mapping.parent_id);
      if (node == nullptr || parent == nullptr || !node->online ||
          !parent->online || node->authority || node->ip.empty() ||
          node->parent_id == mapping.parent_id ||
          mqtt_operation_active_for_node(mapping.node_id) ||
          topology_lock_mismatch_counts[mapping.node_id] <
              TOPOLOGY_LOCK_CONFIRMATIONS) {
        continue;
      }
      const auto manual_restart = restart_cooldowns.find(mapping.node_id);
      if (manual_restart != restart_cooldowns.end() &&
          uptime_ms() - manual_restart->second < RESTART_COOLDOWN_MS) {
        continue;
      }
      selected = mapping;
      selected_node = *node;
      selected_parent = *parent;
      topology_lock_last_selected_node_id = mapping.node_id;
      break;
    }
  }
  xSemaphoreGive(topology_lock_mutex);
  if (selected.node_id.empty()) return;

  if (memory_mutex == nullptr ||
      xSemaphoreTake(memory_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
    ESP_LOGW(TAG, "Topology lock could not reserve the restart workspace");
    return;
  }
  if (xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    xSemaphoreGive(memory_mutex);
    return;
  }
  bool still_armed = topology_lock_enabled &&
                     topology_lock_remaining_ms_locked() == 0;
  if (still_armed) {
    still_armed = false;
    for (const auto &mapping : topology_lock_mappings) {
      if (mapping.node_id == selected.node_id &&
          mapping.parent_id == selected.parent_id) {
        still_armed = true;
        break;
      }
    }
  }
  const auto manual_restart = restart_cooldowns.find(selected.node_id);
  if (still_armed && manual_restart != restart_cooldowns.end() &&
      uptime_ms() - manual_restart->second < RESTART_COOLDOWN_MS) {
    still_armed = false;
  }
  if (!still_armed) {
    xSemaphoreGive(topology_lock_mutex);
    xSemaphoreGive(memory_mutex);
    return;
  }
  topology_lock_action_seen_this_boot = true;
  topology_lock_last_action_uptime_ms = uptime_ms();
  topology_lock_last_action_epoch = wall_clock_epoch();
  topology_lock_last_action_unknown_time =
      topology_lock_last_action_epoch == 0;
  persist_topology_lock_locked();
  xSemaphoreGive(topology_lock_mutex);

  ESP_LOGW(
      TAG,
      "Topology lock restarting %s: current parent=%s expected parent=%s",
      selected_node.name.c_str(),
      selected_node.parent_id.c_str(),
      selected.parent_id.c_str());
  const JnapResult reboot = jnap_request(
      selected_node.ip,
      "core/Reboot",
      "{}",
      nullptr,
      1024);
  xSemaphoreGive(memory_mutex);
  const bool accepted = reboot.transport_ok && reboot.status == 200 &&
                        response_is_ok(reboot.body);
  if (xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    TopologyLockAction action;
    action.node_id = selected_node.id;
    action.node_name = selected_node.name;
    action.expected_parent_id = selected.parent_id;
    action.expected_parent_name = selected_parent.name;
    action.current_parent_id = selected_node.parent_id;
    const NodeObservation *current_parent =
        find_observed_node(nodes, selected_node.parent_id);
    action.current_parent_name =
        current_parent != nullptr ? current_parent->name : selected_node.parent_id;
    action.requested_at = iso_timestamp();
    action.uptime_ms = topology_lock_last_action_uptime_ms;
    action.accepted = accepted;
    add_topology_lock_history_locked(std::move(action));
    restart_cooldowns[selected_node.id] = topology_lock_last_action_uptime_ms;
    xSemaphoreGive(topology_lock_mutex);
  }
  if (accepted) {
    ESP_LOGI(TAG, "Topology lock restart accepted by %s", selected_node.name.c_str());
    request_refresh();
  } else {
    ESP_LOGW(TAG, "Topology lock restart was not acknowledged by %s", selected_node.name.c_str());
  }
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
              candidate.stats,
              candidate.nodes) &&
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

static void mqtt_set_capability(bool available, const std::string &reason) {
  if (mqtt_steering_mutex == nullptr ||
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return;
  }
  mqtt_available = available;
  mqtt_capability_reason = available ? "" : reason;
  mqtt_capability_proof =
      available ? "Fresh DEVINFO received after an authenticated ACL roundtrip" : "";
  mqtt_last_probe_at = iso_timestamp();
  mqtt_last_probe_ms = uptime_ms();
  mqtt_probe_requested = false;
  xSemaphoreGive(mqtt_steering_mutex);
  App.wake_loop_threadsafe();
}

static bool mqtt_mode_allows_probe() {
  bool allowed = false;
  if (mqtt_steering_mutex != nullptr &&
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    allowed = mqtt_mode != MqttSteeringMode::FORCE_OFF;
    xSemaphoreGive(mqtt_steering_mutex);
  }
  return allowed;
}

static void mqtt_fail_operation(
    uint32_t operation_id,
    const std::string &detail) {
  if (mqtt_steering_mutex != nullptr &&
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    if (mqtt_operation.id == operation_id) {
      mqtt_operation.state = "failed";
      mqtt_operation.detail = detail;
      mqtt_operation_pending = false;
    }
    xSemaphoreGive(mqtt_steering_mutex);
  }
  App.wake_loop_threadsafe();
}

static void mqtt_steering_worker(void *) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    if (!wifi_connected() || !router_connected.load(std::memory_order_acquire)) {
      continue;
    }

    MqttSteeringOperation operation;
    MqttSteeringMode mode = MqttSteeringMode::AUTO;
    bool run_operation = false;
    bool run_probe = false;
    bool capability_available = false;
    if (mqtt_steering_mutex != nullptr &&
        xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      mode = mqtt_mode;
      capability_available = mqtt_available;
      run_operation = mqtt_operation_pending;
      if (run_operation) {
        operation = mqtt_operation;
        mqtt_operation_pending = false;
      }
      const uint32_t elapsed = uptime_ms() - mqtt_last_probe_ms;
      run_probe = !run_operation && mode != MqttSteeringMode::FORCE_OFF &&
                  (mqtt_probe_requested || mqtt_last_probe_ms == 0 ||
                   elapsed >= MQTT_PROBE_INTERVAL_MS);
      xSemaphoreGive(mqtt_steering_mutex);
    }

    if (mode == MqttSteeringMode::FORCE_OFF) continue;
    if (run_probe) {
      std::string error;
      const bool available = mqtt_probe_roundtrip(error);
      mqtt_set_capability(available, error);
      continue;
    }
    if (!run_operation) continue;

    std::string conflict;
    if (mqtt_topology_lock_conflict(
            operation.child_id, operation.parent_id, conflict)) {
      mqtt_fail_operation(operation.id, conflict);
      continue;
    }

    if (mode == MqttSteeringMode::AUTO && !capability_available) {
      if (mqtt_steering_mutex != nullptr &&
          xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (mqtt_operation.id == operation.id) {
          mqtt_operation.state = "probing";
          mqtt_operation.detail = "Confirming the router MQTT ACL before steering";
        }
        xSemaphoreGive(mqtt_steering_mutex);
      }
      std::string probe_error;
      const bool available = mqtt_probe_roundtrip(probe_error);
      mqtt_set_capability(available, probe_error);
      if (!available) {
        mqtt_fail_operation(operation.id, probe_error);
        continue;
      }
    }

    if (!mqtt_mode_allows_probe()) {
      mqtt_fail_operation(operation.id, "MQTT Parent steering was turned off");
      continue;
    }
    if (mqtt_steering_mutex != nullptr &&
        xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      if (mqtt_operation.id == operation.id) {
        mqtt_operation.state = "discovering-target";
        mqtt_operation.detail = "Reading the requested Parent's fresh 5 GHz radio state";
      }
      xSemaphoreGive(mqtt_steering_mutex);
    }
    App.wake_loop_threadsafe();

    MqttRadioTarget target;
    std::string error;
    const bool published = mqtt_publish_parent_request(operation, target, error);
    if (!published) {
      mqtt_set_capability(false, error);
      mqtt_fail_operation(operation.id, error);
      continue;
    }

    uint32_t generation = 0;
    if (snapshot_mutex != nullptr &&
        xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      generation = snapshot.generation;
      xSemaphoreGive(snapshot_mutex);
    }
    if (mqtt_steering_mutex != nullptr &&
        xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      if (mqtt_operation.id == operation.id) {
        mqtt_operation.state = "verification-pending";
        mqtt_operation.detail =
            "The broker acknowledged the request; waiting for two topology generations";
        mqtt_operation.published_generation = generation;
        mqtt_operation.last_verified_generation = generation;
        mqtt_operation.verification_generations = 0;
        mqtt_operation.consecutive_matches = 0;
        mqtt_child_cooldowns[ascii_lower(operation.child_id)] = uptime_ms();
      }
      mqtt_available = true;
      mqtt_capability_reason.clear();
      mqtt_capability_proof =
          (target.source.empty() ? "Trusted Parent radio state" : target.source) +
          " and BH/config PUBACK completed";
      mqtt_last_probe_at = iso_timestamp();
      mqtt_last_probe_ms = uptime_ms();
      xSemaphoreGive(mqtt_steering_mutex);
    }
    request_refresh();
    App.wake_loop_threadsafe();
  }
}

static void collector_task(void *) {
  uint32_t generation = 0;
  // The no-PSRAM targets need one large, contiguous receive block for the
  // device list. Keep it for the lifetime of the collector task so repeated
  // refreshes cannot fragment that block into smaller heap allocations.
  std::string device_receive_workspace;
  std::string standard_receive_workspace;
  while (true) {
    std::vector<NodeObservation> lock_observations;
    bool snapshot_updated = false;
    uint32_t snapshot_generation = 0;
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
        lock_observations = candidate.nodes;
        snapshot = std::move(candidate);
        snapshot_generation = snapshot.generation;
        xSemaphoreGive(snapshot_mutex);
        snapshot_updated = true;
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
    if (snapshot_updated) {
      mqtt_verify_operation(lock_observations, snapshot_generation);
      evaluate_topology_lock(lock_observations);
      if (mqtt_steering_task_handle != nullptr) {
        xTaskNotifyGive(mqtt_steering_task_handle);
      }
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
  httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(
      request,
      "Content-Security-Policy",
      "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'");
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
  if (topology_lock_mutex != nullptr &&
      xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    cJSON_AddBoolToObject(root, "topologyLockEnabled", topology_lock_enabled);
    cJSON_AddNumberToObject(
        root,
        "topologyLockNextActionInSeconds",
        (topology_lock_remaining_ms_locked() + 999) / 1000);
    xSemaphoreGive(topology_lock_mutex);
  }
  if (mqtt_steering_mutex != nullptr &&
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    cJSON_AddStringToObject(root, "mqttParentSteeringMode", mqtt_mode_name(mqtt_mode));
    cJSON_AddBoolToObject(root, "mqttParentSteeringAvailable", mqtt_available);
    xSemaphoreGive(mqtt_steering_mutex);
  }
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
  cJSON *lock = topology_lock_json(snapshot.nodes);
  char *serialized_lock = cJSON_PrintUnformatted(lock);
  cJSON_Delete(lock);
  std::string prefix =
      "{\"router\":\"" + router_host +
      "\",\"meta\":{\"updatedAt\":\"" + snapshot.updated_at +
      "\",\"edgeAddress\":\"" + ip +
      "\",\"routerConnected\":" +
      (router_connected.load(std::memory_order_acquire) ? "true" : "false") +
      ",\"generation\":" + std::to_string(snapshot.generation) +
      ",\"clientDetails\":\"" +
      client_details_name(active_client_details) +
      "\",\"topologyLock\":" +
      (serialized_lock != nullptr ? serialized_lock : "{\"supported\":true,\"enabled\":false}") +
      "},\"rawJnap\":{";
  cJSON_free(serialized_lock);
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

static esp_err_t login_handler(httpd_req_t *request) {
  std::string body;
  if (!read_request_json(request, body, 512)) {
    return send_error_json(
        request,
        "400 Bad Request",
        "Enter the MeshScope dashboard password.");
  }
  cJSON *payload = cJSON_ParseWithLength(body.c_str(), body.size());
  const char *password =
      payload != nullptr ? json_string(payload, "password") : nullptr;
  const bool accepted =
      password != nullptr &&
      constant_time_equal(dashboard_password, std::string(password));
  cJSON_Delete(payload);
  if (!accepted) {
    return send_error_json(
        request,
        "401 Unauthorized",
        "That dashboard password was not accepted. Check the value in your local ESPHome YAML.");
  }
  const std::string token = create_web_session();
  if (token.empty()) {
    return send_error_json(
        request,
        "503 Service Unavailable",
        "The dashboard session store is temporarily unavailable.");
  }
  const std::string cookie =
      std::string(WEB_SESSION_COOKIE) + "=" + token +
      "; Path=/; HttpOnly; SameSite=Strict";
  httpd_resp_set_hdr(request, "Set-Cookie", cookie.c_str());
  return send_json(request, "{\"authenticated\":true}");
}

static esp_err_t logout_handler(httpd_req_t *request) {
  remove_web_session(request);
  httpd_resp_set_hdr(
      request,
      "Set-Cookie",
      "meshscope_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
  return send_json(request, "{\"authenticated\":false}");
}

static std::vector<NodeObservation> current_node_observations() {
  std::vector<NodeObservation> nodes;
  if (snapshot_mutex != nullptr &&
      xSemaphoreTake(snapshot_mutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
    if (snapshot.ready) nodes = snapshot.nodes;
    xSemaphoreGive(snapshot_mutex);
  }
  return nodes;
}

static esp_err_t send_cjson(httpd_req_t *request, cJSON *root) {
  char *body = cJSON_PrintUnformatted(root);
  const esp_err_t result = send_json(request, body ?: "{}");
  cJSON_free(body);
  cJSON_Delete(root);
  return result;
}

static cJSON *mqtt_parent_steering_json() {
  cJSON *root = cJSON_CreateObject();
  if (root == nullptr) return nullptr;
  if (mqtt_steering_mutex == nullptr ||
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    cJSON_AddBoolToObject(root, "supported", true);
    cJSON_AddBoolToObject(root, "available", false);
    cJSON_AddStringToObject(root, "state", "busy");
    return root;
  }
  const bool effective_enabled =
      mqtt_mode == MqttSteeringMode::FORCE_ON ||
      (mqtt_mode == MqttSteeringMode::AUTO && mqtt_available);
  uint32_t next_probe_seconds = 0;
  if (mqtt_mode != MqttSteeringMode::FORCE_OFF && mqtt_last_probe_ms != 0) {
    const uint32_t elapsed = uptime_ms() - mqtt_last_probe_ms;
    if (elapsed < MQTT_PROBE_INTERVAL_MS) {
      next_probe_seconds = (MQTT_PROBE_INTERVAL_MS - elapsed + 999) / 1000;
    }
  }
  cJSON_AddBoolToObject(root, "supported", true);
  cJSON_AddBoolToObject(root, "configurable", true);
  cJSON_AddStringToObject(root, "mode", mqtt_mode_name(mqtt_mode));
  cJSON_AddStringToObject(root, "defaultMode", mqtt_mode_name(mqtt_default_mode));
  cJSON_AddBoolToObject(root, "available", mqtt_available);
  cJSON_AddBoolToObject(root, "roundTrip", mqtt_available);
  cJSON_AddBoolToObject(root, "effectiveEnabled", effective_enabled);
  cJSON_AddStringToObject(
      root,
      "state",
      mqtt_mode == MqttSteeringMode::FORCE_OFF
          ? "disabled"
          : mqtt_mode == MqttSteeringMode::FORCE_ON
                ? (mqtt_available ? "available" : "forced")
                : mqtt_probe_requested ? "detecting"
                : mqtt_available ? "available"
                                 : mqtt_last_probe_ms == 0 ? "detecting"
                                                           : "unavailable");
  cJSON_AddStringToObject(root, "transport", "mqtt-1883");
  cJSON_AddStringToObject(root, "reason", mqtt_capability_reason.c_str());
  cJSON_AddStringToObject(root, "proof", mqtt_capability_proof.c_str());
  cJSON_AddStringToObject(root, "testedAt", mqtt_last_probe_at.c_str());
  cJSON_AddStringToObject(root, "lastProbedAt", mqtt_last_probe_at.c_str());
  cJSON_AddNumberToObject(root, "nextProbeInSeconds", next_probe_seconds);
  cJSON *broker = cJSON_AddObjectToObject(root, "broker");
  cJSON_AddStringToObject(broker, "host", router_host.c_str());
  cJSON_AddNumberToObject(broker, "port", MQTT_PORT);
  cJSON *operation = cJSON_AddObjectToObject(root, "operation");
  cJSON_AddNumberToObject(operation, "id", mqtt_operation.id);
  cJSON_AddStringToObject(operation, "state", mqtt_operation.state.c_str());
  cJSON_AddStringToObject(operation, "detail", mqtt_operation.detail.c_str());
  cJSON_AddStringToObject(operation, "requestedAt", mqtt_operation.requested_at.c_str());
  cJSON_AddStringToObject(operation, "childId", mqtt_operation.child_id.c_str());
  cJSON_AddStringToObject(operation, "childName", mqtt_operation.child_name.c_str());
  cJSON_AddStringToObject(operation, "parentId", mqtt_operation.parent_id.c_str());
  cJSON_AddStringToObject(operation, "parentName", mqtt_operation.parent_name.c_str());
  cJSON_AddStringToObject(operation, "band", mqtt_operation.band.c_str());
  cJSON_AddNumberToObject(
      operation, "verificationGenerations", mqtt_operation.verification_generations);
  cJSON_AddNumberToObject(
      operation, "consecutiveMatches", mqtt_operation.consecutive_matches);
  cJSON_AddNumberToObject(operation, "requiredMatches", MQTT_VERIFY_GENERATIONS);
  xSemaphoreGive(mqtt_steering_mutex);
  return root;
}

static bool mqtt_preflight(
    const std::vector<NodeObservation> &nodes,
    const std::string &child_id,
    const std::string &parent_id,
    NodeObservation &child,
    NodeObservation &parent,
    std::string &error) {
  bool found_child = false;
  bool found_parent = false;
  for (const auto &node : nodes) {
    if (same_node_id(node.id, child_id)) {
      child = node;
      found_child = true;
    }
    if (same_node_id(node.id, parent_id)) {
      parent = node;
      found_parent = true;
    }
  }
  if (!found_child || !found_parent) {
    error = "The selected Node or Parent is absent from the current topology";
    return false;
  }
  if (!child.online || !parent.online) {
    error = "Both the child Node and requested Parent must be online";
    return false;
  }
  if (child.authority) {
    error = "The primary Node cannot be steered as a child";
    return false;
  }
  if (same_node_id(child.id, parent.id)) {
    error = "A Node cannot be its own Parent";
    return false;
  }
  const std::string connection = ascii_lower(child.connection_type);
  if (connection.find("wired") != std::string::npos ||
      connection.find("ethernet") != std::string::npos) {
    error = "A wired-backhaul Node cannot be steered over BH/config";
    return false;
  }
  if (same_node_id(child.parent_id, parent.id)) {
    error = "The Node is already connected to the requested Parent";
    return false;
  }
  std::set<std::string> descendants;
  std::vector<std::string> pending{ascii_lower(child.id)};
  while (!pending.empty()) {
    const std::string current = pending.back();
    pending.pop_back();
    for (const auto &node : nodes) {
      if (!same_node_id(node.parent_id, current)) continue;
      const std::string candidate = ascii_lower(node.id);
      if (descendants.insert(candidate).second) pending.push_back(candidate);
    }
  }
  if (descendants.count(ascii_lower(parent.id)) != 0) {
    error = "The requested Parent is a descendant of the child Node";
    return false;
  }
  return !mqtt_topology_lock_conflict(child.id, parent.id, error);
}

static esp_err_t mqtt_parent_get_handler(httpd_req_t *request) {
  if (!authorize_web_request(request)) return ESP_OK;
  std::string refresh;
  if (query_value(request, "refresh", refresh) && refresh == "1" &&
      mqtt_steering_mutex != nullptr &&
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    if (mqtt_mode != MqttSteeringMode::FORCE_OFF) mqtt_probe_requested = true;
    xSemaphoreGive(mqtt_steering_mutex);
    if (mqtt_steering_task_handle != nullptr) {
      xTaskNotifyGive(mqtt_steering_task_handle);
    }
  }
  return send_cjson(request, mqtt_parent_steering_json());
}

static esp_err_t mqtt_parent_mode_handler(httpd_req_t *request) {
  if (!authorize_web_request(request)) return ESP_OK;
  std::string body;
  if (!read_request_json(request, body, 512)) {
    return send_error_json(request, "400 Bad Request", "A Parent steering mode is required.");
  }
  cJSON *payload = cJSON_ParseWithLength(body.c_str(), body.size());
  const char *mode_value = payload != nullptr ? json_string(payload, "mode") : nullptr;
  if (mode_value == nullptr ||
      (strcmp(mode_value, "auto") != 0 && strcmp(mode_value, "force-on") != 0 &&
       strcmp(mode_value, "force-off") != 0)) {
    cJSON_Delete(payload);
    return send_error_json(
        request,
        "400 Bad Request",
        "Mode must be auto, force-on, or force-off.");
  }
  const MqttSteeringMode requested = parse_mqtt_mode(mode_value);
  cJSON_Delete(payload);
  if (!persist_mqtt_mode(requested)) {
    return send_error_json(
        request,
        "500 Internal Server Error",
        "The Parent steering mode could not be saved.");
  }
  if (mqtt_steering_mutex == nullptr ||
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return send_error_json(
        request,
        "503 Service Unavailable",
        "Parent steering is temporarily busy.");
  }
  mqtt_mode = requested;
  mqtt_probe_requested = requested != MqttSteeringMode::FORCE_OFF;
  if (requested == MqttSteeringMode::FORCE_OFF) {
    mqtt_available = false;
    mqtt_capability_reason = "Disabled by configuration";
    mqtt_capability_proof.clear();
    if (mqtt_operation_pending ||
        (mqtt_operation_is_active(mqtt_operation) &&
         mqtt_operation.state != "verification-pending")) {
      mqtt_operation_pending = false;
      mqtt_operation.state = "cancelled";
      mqtt_operation.detail = "Cancelled because MQTT Parent steering was turned off";
    }
  }
  xSemaphoreGive(mqtt_steering_mutex);
  if (mqtt_steering_task_handle != nullptr) xTaskNotifyGive(mqtt_steering_task_handle);
  App.wake_loop_threadsafe();
  return send_cjson(request, mqtt_parent_steering_json());
}

static esp_err_t mqtt_steer_handler(httpd_req_t *request) {
  if (!authorize_web_request(request)) return ESP_OK;
  std::string body;
  if (!read_request_json(request, body, 1024)) {
    return send_error_json(request, "400 Bad Request", "The Parent steering request is invalid.");
  }
  cJSON *payload = cJSON_ParseWithLength(body.c_str(), body.size());
  const char *child_id = payload != nullptr ? json_string(payload, "childId") : nullptr;
  const char *parent_id = payload != nullptr ? json_string(payload, "parentId") : nullptr;
  const char *band = payload != nullptr ? json_string(payload, "band") : nullptr;
  if (child_id == nullptr || parent_id == nullptr || band == nullptr ||
      (strcmp(band, "5GH") != 0 && strcmp(band, "5GL") != 0) ||
      strlen(child_id) > 128 || strlen(parent_id) > 128) {
    cJSON_Delete(payload);
    return send_error_json(
        request,
        "400 Bad Request",
        "childId, parentId, and band (5GH or 5GL) are required.");
  }
  const std::string requested_child = child_id;
  const std::string requested_parent = parent_id;
  const std::string requested_band = band;
  cJSON_Delete(payload);
  NodeObservation child;
  NodeObservation parent;
  std::string error;
  if (!mqtt_preflight(
          current_node_observations(),
          requested_child,
          requested_parent,
          child,
          parent,
          error)) {
    return send_error_json(request, "409 Conflict", error.c_str());
  }
  if (mqtt_steering_mutex == nullptr ||
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return send_error_json(
        request,
        "503 Service Unavailable",
        "Parent steering is temporarily busy.");
  }
  if (mqtt_mode == MqttSteeringMode::FORCE_OFF) {
    xSemaphoreGive(mqtt_steering_mutex);
    return send_error_json(request, "409 Conflict", "MQTT Parent steering is forced off.");
  }
  if (mqtt_mode == MqttSteeringMode::AUTO && !mqtt_available) {
    xSemaphoreGive(mqtt_steering_mutex);
    return send_error_json(
        request,
        "409 Conflict",
        "Automatic detection has not confirmed the required router MQTT ACL.");
  }
  if (mqtt_operation_is_active(mqtt_operation) || mqtt_operation_pending) {
    xSemaphoreGive(mqtt_steering_mutex);
    return send_error_json(
        request,
        "409 Conflict",
        "Another Parent steering request is still in progress.");
  }
  const std::string cooldown_key = ascii_lower(child.id);
  const auto cooldown = mqtt_child_cooldowns.find(cooldown_key);
  if (cooldown != mqtt_child_cooldowns.end() &&
      uptime_ms() - cooldown->second < MQTT_OPERATION_DEDUP_MS) {
    xSemaphoreGive(mqtt_steering_mutex);
    return send_error_json(
        request,
        "409 Conflict",
        "This Node was steered recently. Wait for topology verification.");
  }
  mqtt_operation = {};
  mqtt_operation.id = ++mqtt_next_operation_id;
  mqtt_operation.child_id = child.id;
  mqtt_operation.child_name = child.name;
  mqtt_operation.parent_id = parent.id;
  mqtt_operation.parent_name = parent.name;
  mqtt_operation.previous_parent_id = child.parent_id;
  mqtt_operation.band = requested_band;
  mqtt_operation.state = "queued";
  mqtt_operation.detail = "Waiting for the background MQTT worker";
  mqtt_operation.requested_at = iso_timestamp();
  mqtt_operation.started_ms = uptime_ms();
  mqtt_operation_pending = true;
  xSemaphoreGive(mqtt_steering_mutex);
  if (mqtt_steering_task_handle != nullptr) xTaskNotifyGive(mqtt_steering_task_handle);
  cJSON *response = mqtt_parent_steering_json();
  char *serialized = cJSON_PrintUnformatted(response);
  const esp_err_t result = send_json(
      request,
      serialized ?: "{}",
      "202 Accepted");
  cJSON_free(serialized);
  cJSON_Delete(response);
  return result;
}

static esp_err_t topology_lock_get_handler(httpd_req_t *request) {
  if (!authorize_web_request(request)) return ESP_OK;
  return send_cjson(
      request,
      topology_lock_json(current_node_observations()));
}

static esp_err_t topology_lock_post_handler(httpd_req_t *request) {
  if (!authorize_web_request(request)) return ESP_OK;
  const size_t content_type_length =
      httpd_req_get_hdr_value_len(request, "Content-Type");
  std::vector<char> content_type(content_type_length + 1);
  if (content_type_length == 0 ||
      httpd_req_get_hdr_value_str(
          request,
          "Content-Type",
          content_type.data(),
          content_type.size()) != ESP_OK ||
      strstr(content_type.data(), "application/json") == nullptr) {
    return send_error_json(
        request,
        "415 Unsupported Media Type",
        "The request must use JSON.");
  }
  std::string body;
  if (!read_request_json(request, body, 8192)) {
    return send_error_json(
        request,
        "400 Bad Request",
        "The topology lock request is invalid.");
  }
  cJSON *payload = cJSON_ParseWithLength(body.c_str(), body.size());
  const cJSON *enabled =
      payload != nullptr
          ? cJSON_GetObjectItemCaseSensitive(payload, "enabled")
          : nullptr;
  if (!cJSON_IsBool(enabled)) {
    cJSON_Delete(payload);
    return send_error_json(
        request,
        "400 Bad Request",
        "The enabled flag is required.");
  }

  const std::vector<NodeObservation> observations = current_node_observations();
  if (cJSON_IsFalse(enabled)) {
    if (topology_lock_mutex == nullptr ||
        xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
      cJSON_Delete(payload);
      return send_error_json(
          request,
          "503 Service Unavailable",
          "The topology lock is temporarily busy.");
    }
    const bool previous_enabled = topology_lock_enabled;
    const std::string previous_saved_at = topology_lock_saved_at;
    const std::vector<LockedParent> previous_mappings =
        topology_lock_mappings;
    topology_lock_enabled = false;
    topology_lock_saved_at.clear();
    topology_lock_mappings.clear();
    topology_lock_mismatch_counts.clear();
    const bool saved = persist_topology_lock_locked();
    if (!saved) {
      topology_lock_enabled = previous_enabled;
      topology_lock_saved_at = previous_saved_at;
      topology_lock_mappings = previous_mappings;
    }
    xSemaphoreGive(topology_lock_mutex);
    cJSON_Delete(payload);
    if (!saved) {
      return send_error_json(
          request,
          "500 Internal Server Error",
          "The topology lock could not be saved.");
    }
    App.wake_loop_threadsafe();
    return send_cjson(request, topology_lock_json(observations));
  }

  const cJSON *nodes = cJSON_GetObjectItemCaseSensitive(payload, "nodes");
  if (!cJSON_IsArray(nodes) ||
      cJSON_GetArraySize(nodes) > TOPOLOGY_LOCK_MAX_NODES) {
    cJSON_Delete(payload);
    return send_error_json(
        request,
        "400 Bad Request",
        "A bounded node-to-parent list is required.");
  }
  std::vector<LockedParent> mappings;
  const cJSON *item = nullptr;
  cJSON_ArrayForEach(item, nodes) {
    const char *node_id = json_string(item, "nodeId");
    const char *parent_id = json_string(item, "parentId");
    if (node_id == nullptr || parent_id == nullptr ||
        node_id[0] == '\0' || parent_id[0] == '\0' ||
        strlen(node_id) > 128 || strlen(parent_id) > 128) {
      cJSON_Delete(payload);
      return send_error_json(
          request,
          "400 Bad Request",
          "Each lock entry needs a valid node and parent ID.");
    }
    mappings.push_back({node_id, parent_id});
  }
  cJSON_Delete(payload);
  std::string validation_error;
  if (!validate_topology_lock_mappings(
          observations,
          mappings,
          validation_error)) {
    return send_error_json(
        request,
        "409 Conflict",
        validation_error.c_str());
  }
  if (topology_lock_mutex == nullptr ||
      xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) {
    return send_error_json(
        request,
        "503 Service Unavailable",
        "The topology lock is temporarily busy.");
  }
  const bool previous_enabled = topology_lock_enabled;
  const std::string previous_saved_at = topology_lock_saved_at;
  const std::vector<LockedParent> previous_mappings =
      topology_lock_mappings;
  topology_lock_enabled = true;
  topology_lock_saved_at = iso_timestamp();
  topology_lock_mappings = std::move(mappings);
  const size_t applied_count = topology_lock_mappings.size();
  topology_lock_mismatch_counts.clear();
  const bool saved = persist_topology_lock_locked();
  if (!saved) {
    topology_lock_enabled = previous_enabled;
    topology_lock_saved_at = previous_saved_at;
    topology_lock_mappings = previous_mappings;
  }
  xSemaphoreGive(topology_lock_mutex);
  if (!saved) {
    return send_error_json(
        request,
        "500 Internal Server Error",
        "The topology lock could not be saved.");
  }
  App.wake_loop_threadsafe();
  ESP_LOGI(
      TAG,
      "Topology lock applied to %u child nodes",
      static_cast<unsigned>(applied_count));
  return send_cjson(request, topology_lock_json(observations));
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
  bool restart_blocked = false;
  if (topology_lock_mutex == nullptr ||
      xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    cJSON_Delete(payload);
    return send_error_json(
        request,
        "503 Service Unavailable",
        "The restart scheduler is temporarily busy.");
  } else {
    const auto cooldown = restart_cooldowns.find(node.id);
    restart_blocked =
        cooldown != restart_cooldowns.end() &&
        now - cooldown->second < RESTART_COOLDOWN_MS;
    if (!restart_blocked) restart_cooldowns[node.id] = now;
    xSemaphoreGive(topology_lock_mutex);
  }
  if (restart_blocked) {
    cJSON_Delete(payload);
    return send_error_json(request, "409 Conflict", "The node is already restarting. Wait for it to recover.");
  }
  if (memory_mutex == nullptr ||
      xSemaphoreTake(memory_mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
    if (topology_lock_mutex != nullptr &&
        xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      restart_cooldowns.erase(node.id);
      xSemaphoreGive(topology_lock_mutex);
    }
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
    if (topology_lock_mutex != nullptr &&
        xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
      restart_cooldowns.erase(node.id);
      xSemaphoreGive(topology_lock_mutex);
    }
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
  config.max_uri_handlers = 24;
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
  register_handler("/api/login", HTTP_POST, login_handler);
  register_handler("/api/logout", HTTP_POST, logout_handler);
  register_handler("/api/status", HTTP_GET, status_handler);
  register_handler("/api/topology", HTTP_GET, topology_handler);
  register_handler("/api/refresh", HTTP_POST, refresh_handler);
  register_handler("/api/connect", HTTP_POST, connect_handler);
  register_handler("/api/node-capabilities", HTTP_GET, capabilities_handler);
  register_handler("/api/restart-node", HTTP_POST, restart_handler);
  register_handler("/api/topology-lock", HTTP_GET, topology_lock_get_handler);
  register_handler("/api/topology-lock", HTTP_POST, topology_lock_post_handler);
  register_handler(
      "/api/mqtt-parent-steering", HTTP_GET, mqtt_parent_get_handler);
  register_handler(
      "/api/mqtt-parent-steering", HTTP_POST, mqtt_parent_mode_handler);
  register_handler("/api/steer-node-parent", HTTP_POST, mqtt_steer_handler);
  httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, not_found_handler);
  ESP_LOGI(TAG, "MeshScope HTTP server started on port 80");
}

static void setup(
    const char *host,
    const char *password_b64,
    const char *configured_dashboard_password,
    const char *client_details,
    const char *mqtt_parent_steering) {
  router_host = host ?: "";
  requested_client_details = parse_client_details(client_details);
  mqtt_default_mode = parse_mqtt_mode(mqtt_parent_steering);
  client_details_resolved = false;
  const std::string encoded_password = password_b64 ?: "";
  const bool router_password_ok =
      encoded_password != "SET_IN_LOCAL_CONFIG" &&
      base64_decode_string(encoded_password, router_password);
  authorization = base64_basic_auth("admin", router_password);
  dashboard_password = configured_dashboard_password ?: "";
  snapshot_mutex = xSemaphoreCreateMutex();
  jnap_mutex = xSemaphoreCreateMutex();
  memory_mutex = xSemaphoreCreateMutex();
  topology_lock_mutex = xSemaphoreCreateMutex();
  web_session_mutex = xSemaphoreCreateMutex();
  mqtt_steering_mutex = xSemaphoreCreateMutex();
  if (!router_password_ok || snapshot_mutex == nullptr || jnap_mutex == nullptr ||
      memory_mutex == nullptr || topology_lock_mutex == nullptr ||
      web_session_mutex == nullptr || mqtt_steering_mutex == nullptr ||
      authorization.empty() ||
      dashboard_password.size() < 8 ||
      dashboard_password == "SET_IN_LOCAL_CONFIG") {
    ESP_LOGE(TAG, "MeshScope initialization failed");
    return;
  }
  ESP_LOGI(
      TAG,
      "MeshScope ESPHome edge starting; assets=%u source=%s PSRAM=%u",
      static_cast<unsigned>(meshscope_web_assets::ASSET_COUNT),
      meshscope_web_assets::SOURCE_SHA256,
      static_cast<unsigned>(external_memory_size()));
  load_topology_lock();
  load_mqtt_mode();
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
  if (xTaskCreate(
          mqtt_steering_worker,
          "meshscope_mqtt",
          8192,
          nullptr,
          1,
          &mqtt_steering_task_handle) != pdPASS) {
    ESP_LOGE(TAG, "Unable to start MQTT Parent steering worker");
    mqtt_steering_task_handle = nullptr;
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

static bool topology_lock_active_copy() {
  bool value = false;
  if (topology_lock_mutex != nullptr &&
      xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    value = topology_lock_enabled;
    xSemaphoreGive(topology_lock_mutex);
  }
  return value;
}

static int topology_lock_issue_count_copy() {
  const std::vector<NodeObservation> nodes = current_node_observations();
  int issues = 0;
  if (topology_lock_mutex != nullptr &&
      xSemaphoreTake(topology_lock_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (topology_lock_enabled) {
      for (const auto &mapping : topology_lock_mappings) {
        const NodeObservation *node = find_observed_node(nodes, mapping.node_id);
        const NodeObservation *parent = find_observed_node(nodes, mapping.parent_id);
        if (node == nullptr || parent == nullptr || !node->online ||
            !parent->online || node->parent_id != mapping.parent_id) {
          issues++;
        }
      }
    }
    xSemaphoreGive(topology_lock_mutex);
  }
  return issues;
}

static std::string topology_lock_summary_copy() {
  const int issues = topology_lock_issue_count_copy();
  if (!topology_lock_active_copy()) return "Recovery off";
  if (issues == 0) return "Monitoring · all parents correct";
  char output[64];
  snprintf(
      output,
      sizeof(output),
      "Monitoring · %d issue%s",
      issues,
      issues == 1 ? "" : "s");
  return output;
}

static bool mqtt_parent_available_copy() {
  bool value = false;
  if (mqtt_steering_mutex != nullptr &&
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    value = mqtt_available && mqtt_mode != MqttSteeringMode::FORCE_OFF;
    xSemaphoreGive(mqtt_steering_mutex);
  }
  return value;
}

static std::string mqtt_parent_mode_copy() {
  std::string value = "auto";
  if (mqtt_steering_mutex != nullptr &&
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    value = mqtt_mode_name(mqtt_mode);
    xSemaphoreGive(mqtt_steering_mutex);
  }
  return value;
}

static std::string mqtt_parent_result_copy() {
  std::string value = "Waiting for the first MQTT capability probe";
  if (mqtt_steering_mutex != nullptr &&
      xSemaphoreTake(mqtt_steering_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (mqtt_operation.id != 0) {
      value = mqtt_operation.state;
      if (!mqtt_operation.detail.empty()) value += " · " + mqtt_operation.detail;
    } else if (mqtt_mode == MqttSteeringMode::FORCE_OFF) {
      value = "Disabled";
    } else if (mqtt_available) {
      value = "Available · " + mqtt_capability_proof;
    } else if (!mqtt_capability_reason.empty()) {
      value = "Unavailable · " + mqtt_capability_reason;
    }
    xSemaphoreGive(mqtt_steering_mutex);
  }
  return value;
}

}  // namespace meshscope_edge

inline void meshscope_edge_setup(
    const char *host,
    const char *password_b64,
    const char *dashboard_password,
    const char *client_details,
    const char *mqtt_parent_steering) {
  meshscope_edge::setup(
      host,
      password_b64,
      dashboard_password,
      client_details,
      mqtt_parent_steering);
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

inline bool meshscope_edge_topology_lock_active() {
  return meshscope_edge::topology_lock_active_copy();
}

inline float meshscope_edge_topology_lock_issues() {
  return static_cast<float>(
      meshscope_edge::topology_lock_issue_count_copy());
}

inline std::string meshscope_edge_topology_lock_summary() {
  return meshscope_edge::topology_lock_summary_copy();
}

inline bool meshscope_edge_mqtt_parent_available() {
  return meshscope_edge::mqtt_parent_available_copy();
}

inline std::string meshscope_edge_mqtt_parent_mode() {
  return meshscope_edge::mqtt_parent_mode_copy();
}

inline std::string meshscope_edge_mqtt_parent_result() {
  return meshscope_edge::mqtt_parent_result_copy();
}
