#include "RelayTask.h"

#include <Arduino.h>
#include <InkAgentStore.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "InkAgentSettings.h"
#include "WifiCredentialStore.h"
#include "network/InkAgentClient.h"

namespace {

enum class Kind : uint8_t { Recall, SyncApps };

// One queued job. Strings are copied because the activity that supplied them is
// usually destroyed before this runs.
struct Job {
  Kind kind = Kind::Recall;
  std::string book, author, chapter, text;
  int pct = -1;
  int regressions = -1;
  int speedPct = -1;
};

QueueHandle_t queue = nullptr;
SemaphoreHandle_t relayMutex = nullptr;
volatile bool running = false;
uint32_t lastRecallMs = 0;

// A reading session is the natural spacing for a question, but a reader who
// dips in and out every few minutes should not generate a call each time.
constexpr uint32_t kRecallThrottleMs = 30 * 60 * 1000;

// The heap the EPUB was holding does not come back the instant a job is queued:
// the activity is destroyed after onExit returns. Wait for it rather than
// failing a fetch that would have worked a second later.
constexpr int kHeapWaitAttempts = 15;
constexpr uint32_t kHeapWaitStepMs = 1000;

constexpr uint32_t kWifiWaitMs = 20000;

bool waitForHeap() {
  for (int i = 0; i < kHeapWaitAttempts; i++) {
    if (InkAgentClient::heapAllowsTls()) return true;
    vTaskDelay(pdMS_TO_TICKS(kHeapWaitStepMs));
  }
  char heap[48];
  InkAgentClient::heapSummary(heap, sizeof(heap));
  LOG_ERR("RELAY", "gave up waiting for heap (%s)", heap);
  InkAgentClient::sdLog("background: no heap for TLS");
  return false;
}

// Brings Wi-Fi up from the last network that worked. Returns true when the
// radio is usable; `owned` says whether this call raised it, and therefore
// whether it should be taken back down afterwards.
bool ensureWifi(bool& owned) {
  owned = false;
  if (WiFi.status() == WL_CONNECTED) return true;

  WIFI_STORE.loadFromFile();
  const std::string ssid = WIFI_STORE.getLastConnectedSsid();
  if (ssid.empty()) {
    LOG_DBG("RELAY", "no remembered network, skipping");
    return false;
  }
  const auto cred = WIFI_STORE.findCredential(ssid);

  WiFi.mode(WIFI_STA);
  if (cred && !cred->password.empty()) {
    WiFi.begin(ssid.c_str(), cred->password.c_str());
  } else {
    WiFi.begin(ssid.c_str());
  }
  owned = true;

  const uint32_t deadline = millis() + kWifiWaitMs;
  while (millis() < deadline) {
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setSleep(false);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  LOG_ERR("RELAY", "could not join %s in background", ssid.c_str());
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  owned = false;
  return false;
}

void dropWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void runRecall(const Job& job) {
  inkagent::EngageRequest req;
  req.app = "recall";
  req.book = job.book.c_str();
  req.author = job.author.c_str();
  req.chapter = job.chapter.c_str();
  req.pct = job.pct;
  req.text = job.text.c_str();
  req.regressions = job.regressions;
  req.speedRatioPct = job.speedPct;

  // No text buffer: nothing is on screen to show it. The screen is cached and
  // the sleep canvas picks it up whenever the device next sleeps.
  const auto r = InkAgentClient::engage(req, InkAgentClient::ENGAGE_CACHE, nullptr, 0);
  if (r.revoked) {
    INKAGENT_STORE.clearPairing();
    INKAGENT_STORE.saveToFile();
    InkAgentClient::sdLog("background: relay revoked this device");
    return;
  }
  if (r.noProvider) {
    InkAgentClient::sdLog("background: no provider configured");
    return;
  }
  InkAgentClient::sdLog(r.ok ? "background: question cached" : "background: engage failed");
}

void runSyncApps() {
  const auto r = InkAgentClient::syncApps();
  if (r.revoked) {
    INKAGENT_STORE.clearPairing();
    INKAGENT_STORE.saveToFile();
    return;
  }
  char line[64];
  snprintf(line, sizeof(line), "background: apps %s (%d written, %d failed)",
           r.unchanged ? "unchanged" : (r.ok ? "synced" : "failed"), r.written, r.failed);
  InkAgentClient::sdLog(line);
}

void taskLoop(void*) {
  for (;;) {
    Job* job = nullptr;
    if (xQueueReceive(queue, &job, portMAX_DELAY) != pdTRUE || job == nullptr) continue;

    running = true;
    bool ownedWifi = false;
    if (waitForHeap() && RelayTask::acquireRelay(30000)) {
      if (ensureWifi(ownedWifi)) {
        if (job->kind == Kind::Recall) {
          runRecall(*job);
        } else {
          runSyncApps();
        }
      }
      if (ownedWifi) dropWifi();
      RelayTask::releaseRelay();
    }
    delete job;
    running = false;
  }
}

bool enqueue(Job* job) {
  if (queue == nullptr) {
    delete job;
    return false;
  }
  if (xQueueSend(queue, &job, 0) != pdTRUE) {
    LOG_DBG("RELAY", "queue full, dropping job");
    delete job;
    return false;
  }
  return true;
}

// Common gate: the feature is opt-in, and an unpaired device has nowhere to go.
bool allowed() {
  if (!SETTINGS.agentBackgroundFetch) return false;
  INKAGENT_STORE.ensureLoaded();
  return INKAGENT_STORE.isPaired();
}

}  // namespace

void RelayTask::begin() {
  if (queue != nullptr) return;
  queue = xQueueCreate(2, sizeof(Job*));
  // Recursive: the task holds this for a whole job while each request inside
  // takes it again. A plain mutex would deadlock on the second take.
  relayMutex = xSemaphoreCreateRecursiveMutex();
  if (queue == nullptr || relayMutex == nullptr) {
    LOG_ERR("RELAY", "could not create queue/mutex");
    return;
  }
  // 4096 like the other network paths: TLS and JSON both live on this stack.
  TaskHandle_t handle = nullptr;
  xTaskCreate(&taskLoop, "RelayTask", 4096, nullptr, 1, &handle);
  if (handle == nullptr) LOG_ERR("RELAY", "could not create task");
}

bool RelayTask::submitRecall(const inkagent::EngageRequest& req) {
  if (!allowed() || req.text == nullptr || req.text[0] == '\0') return false;
  const uint32_t nowMs = millis();
  if (lastRecallMs != 0 && nowMs - lastRecallMs < kRecallThrottleMs) return false;

  auto* job = new (std::nothrow) Job();
  if (job == nullptr) return false;
  job->kind = Kind::Recall;
  job->book = req.book ? req.book : "";
  job->author = req.author ? req.author : "";
  job->chapter = req.chapter ? req.chapter : "";
  job->text = req.text;
  job->pct = req.pct;
  job->regressions = req.regressions;
  job->speedPct = req.speedRatioPct;

  if (!enqueue(job)) return false;
  lastRecallMs = nowMs == 0 ? 1 : nowMs;
  return true;
}

bool RelayTask::submitAppSync() {
  if (!allowed()) return false;
  auto* job = new (std::nothrow) Job();
  if (job == nullptr) return false;
  job->kind = Kind::SyncApps;
  return enqueue(job);
}

bool RelayTask::busy() { return running; }

bool RelayTask::acquireRelay(const uint32_t waitMs) {
  if (relayMutex == nullptr) return true;  // task never started; nothing to race
  return xSemaphoreTakeRecursive(relayMutex, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

void RelayTask::releaseRelay() {
  if (relayMutex != nullptr) xSemaphoreGiveRecursive(relayMutex);
}
