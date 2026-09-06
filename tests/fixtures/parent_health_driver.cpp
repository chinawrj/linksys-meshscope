// Host-only dependencies for the extracted production functions. The test
// invokes the actual firmware implementation, not a reimplemented algorithm.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "production_types.h"

constexpr uint8_t MQTT_VERIFY_GENERATIONS = 2;
constexpr uint8_t PARENT_STEERING_FAILURE_THRESHOLD = 2;
constexpr int pdTRUE = 1;
#define pdMS_TO_TICKS(value) (value)
#define ESP_LOGW(...) ((void) 0)
#define ESP_LOGE(...) ((void) 0)
static void *topology_lock_mutex = reinterpret_cast<void *>(1);
static void *memory_mutex = reinterpret_cast<void *>(2);
static void *mqtt_steering_task_handle = reinterpret_cast<void *>(3);
static void (*on_memory_lock)() = nullptr;
static int xSemaphoreTake(void *lock, int) {
  if (lock == memory_mutex && on_memory_lock != nullptr) on_memory_lock();
  return pdTRUE;
}
static void xSemaphoreGive(void *) {}
static int notifications = 0, saves = 0, reboots = 0;
static void xTaskNotifyGive(void *) { notifications++; }
struct Application { void wake_loop_threadsafe() {} } App;
static std::map<std::string, ParentSteeringHealth> parent_steering_health;
static ParentRestartRequest parent_restart_request;
static uint32_t cooldown = 0;
static bool enabled = true;
static bool mqtt_mode_allows_probe() { return enabled; }
static uint32_t parent_health_restart_remaining_ms_locked() { return cooldown; }
static uint32_t parent_node_restart_remaining_ms_locked(const std::string &) { return 0; }
static bool persist_parent_steering_health_locked() { saves++; return true; }
static std::string iso_timestamp() { return "2026-09-06T13:00:00Z"; }
static std::string ascii_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return value;
}
static bool same_node_id(const std::string &a, const std::string &b) {
  return !a.empty() && !b.empty() && ascii_lower(a) == ascii_lower(b);
}
static bool wired_connection_type(const std::string &type) {
  return ascii_lower(type) == "wired" || ascii_lower(type) == "ethernet";
}
struct MqttSteeringOperation {
  std::string method = "bh-config", channel_mode = "exact";
  std::string child_id = "child", parent_id = "parent";
  std::string child_name, parent_name, band, target_bssid, target_source;
  uint32_t id = 4;
  int target_channel = 161;
  struct { std::string command_topic = "network/child/BH/config"; bool config_echoed = true; } backhaul_evidence;
};
static std::vector<NodeObservation> current_nodes;
static std::vector<NodeObservation> current_node_observations() { return current_nodes; }
static std::atomic<bool> router_connected{true};
static std::map<std::string, uint32_t> restart_cooldowns;
static uint32_t uptime_ms() { return 100; }
static uint64_t wall_clock_epoch() { return 1788699600; }
static bool parent_health_restart_seen_this_boot = false;
static uint32_t parent_health_last_restart_uptime_ms = 0;
static uint64_t parent_health_last_restart_epoch = 0;
static bool topology_lock_action_seen_this_boot = false, topology_lock_last_action_unknown_time = false;
static uint32_t topology_lock_last_action_uptime_ms = 0;
static uint64_t topology_lock_last_action_epoch = 0;
static void persist_topology_lock_locked() {}
static void request_refresh() {}
struct JnapResult { bool transport_ok = true; int status = 200; std::string body = "OK"; };
static JnapResult jnap_request(const std::string &, const std::string &action,
                              const std::string &, void *, int) {
  assert(action == "core/Reboot"); reboots++; return {};
}
static bool response_is_ok(const std::string &value) { return value == "OK"; }
static void evaluate_parent_steering_health(const std::vector<NodeObservation> &,
                                          uint32_t = 0, bool = true);
#include "production_functions.h"

static ParentSteeringHealth &reset() {
  parent_steering_health.clear(); parent_restart_request = {};
  saves = notifications = reboots = 0; enabled = true; cooldown = 0;
  router_connected = true;
  NodeObservation child, parent;
  child.id = "child"; child.parent_id = "parent";
  child.online = true; child.connection_type = "Wireless";
  parent.id = "parent"; parent.online = true; parent.ip = "192.0.2.1";
  current_nodes = {child, parent};
  auto &h = parent_steering_health["child"];
  h.child_id = "child"; h.target_parent_id = "parent";
  h.consecutive_failures = 2; h.total_failures = 13; h.successful_moves = 4;
  h.last_operation_id = 4; h.last_failure_at = "old-failure"; h.last_success_at = "old-success";
  h.last_target_bssid = "02:00:00:00:00:01"; h.last_command_echoed = true;
  h.state = "blocked";
  return h;
}

int main() {
  // YardFront reproduction: history persists, alert resolves after exactly
  // two newer observations, with no MQTT-success inflation or NVS write loop.
  auto &h = reset();
  parent_restart_request = {"child", "parent", 4, true};
  evaluate_parent_steering_health(current_nodes, 10);
  assert(h.state == "confirming" && h.recovery_matches == 1);
  assert(!parent_restart_request.pending && notifications == 0 && saves == 0);
  for (auto generation : {0u, 9u, 10u}) evaluate_parent_steering_health(current_nodes, generation);
  assert(h.recovery_matches == 1 && h.consecutive_failures == 2);
  evaluate_parent_steering_health(current_nodes, 11);
  assert(h.state == "recovered" && h.consecutive_failures == 0 && saves == 1);
  assert(h.total_failures == 13 && h.successful_moves == 4);
  assert(h.last_failure_at == "old-failure" && h.last_success_at == "old-success");
  assert(h.last_command_echoed && h.last_target_bssid == "02:00:00:00:00:01");
  assert(h.last_recovered_at == iso_timestamp());
  for (uint32_t generation = 12; generation < 100; generation++) evaluate_parent_steering_health(current_nodes, generation);
  assert(saves == 1 && reboots == 0);

  // A mismatch, offline/missing child/parent, ambiguous Ethernet, or degraded
  // /failed collection breaks the consecutive-evidence chain.
  for (int scenario = 0; scenario < 8; scenario++) {
    auto &health = reset();
    evaluate_parent_steering_health(current_nodes, 1);
    auto invalid = current_nodes;
    if (scenario == 0) invalid[0].parent_id = "other";
    if (scenario == 1) invalid[0].online = false;
    if (scenario == 2) invalid[1].online = false;
    if (scenario == 3) invalid.erase(invalid.begin());
    if (scenario == 4) invalid.pop_back();
    if (scenario == 5) invalid[0].connection_type = "Ethernet";
    if (scenario == 6) invalid[0].authority = true;
    evaluate_parent_steering_health(invalid, 2, scenario != 7);
    assert(health.consecutive_failures == 2 && health.recovery_matches == 0);
    evaluate_parent_steering_health(current_nodes, 3);
    assert(health.state == "confirming" && health.consecutive_failures == 2);
    evaluate_parent_steering_health(current_nodes, 4);
    assert(health.state == "recovered" && health.consecutive_failures == 0);
  }

  // Passive recovery also works with automation disabled and one failure.
  auto &off = reset(); enabled = false; off.consecutive_failures = 1;
  evaluate_parent_steering_health(current_nodes, 1);
  evaluate_parent_steering_health(current_nodes, 2);
  assert(off.state == "recovered" && reboots == 0);

  // New failed operations reset partial evidence, while a verified operation
  // retains its separate success counter semantics.
  auto &fresh = reset();
  evaluate_parent_steering_health(current_nodes, 1);
  MqttSteeringOperation operation;
  record_parent_steering_outcome(operation, false, current_nodes);
  assert(fresh.recovery_matches == 0 && fresh.consecutive_failures == 3);
  evaluate_parent_steering_health(current_nodes, 2);
  assert(fresh.consecutive_failures == 3);
  record_parent_steering_outcome(operation, true, current_nodes);
  assert(fresh.consecutive_failures == 0 && fresh.successful_moves == 5);

  // A late worker cannot reintroduce blocked status after recovery or act for
  // a superseded operation, even if the child disconnects before it runs.
  for (int scenario = 0; scenario < 4; scenario++) {
    auto &health = reset(); current_nodes[0].parent_id = "other";
    if (scenario == 0) { health.state = "recovered"; health.consecutive_failures = 0; }
    if (scenario == 1) health.state = "confirming";
    if (scenario == 2) health.last_operation_id = 5;
    if (scenario == 3) health.target_parent_id = "new-target";
    const auto before = health.state;
    parent_restart_request = {"child", "parent", 4, true};
    assert(process_parent_restart_request());
    assert(reboots == 0 && !parent_restart_request.pending && health.state == before);
  }

  // Genuine unresolved failures still use the original guarded restart path.
  auto &unresolved = reset(); current_nodes[0].parent_id = "other";
  evaluate_parent_steering_health(current_nodes, 1);
  assert(parent_restart_request.pending);
  assert(process_parent_restart_request());
  assert(reboots == 1 && unresolved.parent_restart_count == 1);
  assert(unresolved.state == "parent-restarting");

  reset(); current_nodes[0].parent_id = "other";
  parent_restart_request = {"child", "parent", 4, true}; router_connected = false;
  assert(process_parent_restart_request()); assert(reboots == 0);

  auto &during_wait = reset(); current_nodes[0].parent_id = "other";
  parent_restart_request = {"child", "parent", 4, true};
  on_memory_lock = []() {
    current_nodes[0].parent_id = "parent";
    evaluate_parent_steering_health(current_nodes, 1);
    evaluate_parent_steering_health(current_nodes, 2);
  };
  assert(process_parent_restart_request());
  assert(reboots == 0 && during_wait.state == "recovered" && !parent_restart_request.pending);
}
