#include "InkAgentSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <InkAgentStore.h>

#include <memory>
#include <string>

#include "DataSentActivity.h"
#include "InkAgentPairActivity.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "network/InkAgentClient.h"
#include "network/RelayTask.h"

namespace fui = freeink::ui;

namespace {
const StrId menuNames[InkAgentSettingsActivity::MENU_ITEMS] = {StrId::STR_ASK_RELAY_URL, StrId::STR_ASK_PAIRING,
                                                               StrId::STR_SYNC_APPS, StrId::STR_DATA_SENT,
                                                               StrId::STR_ASK_RESET_RELAY};
}  // namespace

InkAgentSettingsActivity::InkAgentSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("InkAgentSettings", renderer, mappedInput) {
  INKAGENT_STORE.loadFromFile();
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

int InkAgentSettingsActivity::listCount() const { return MENU_ITEMS; }

const char* InkAgentSettingsActivity::headerTitle() const { return tr(STR_ASK_BOOK); }

void InkAgentSettingsActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index == 0) {
    // Relay URL. Prefill with the effective URL so a LAN address is one edit away.
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ASK_RELAY_URL),
                                                                   INKAGENT_STORE.getRelayUrl(), 128, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (result.isCancelled) return;
                             const auto& kb = std::get<KeyboardResult>(result.data);
                             const std::string url = (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                             if (url != INKAGENT_STORE.getRelayUrl()) {
                               // A different relay means a different account: the old token is useless there.
                               INKAGENT_STORE.clearPairing();
                             }
                             INKAGENT_STORE.setRelayUrl(url);
                             INKAGENT_STORE.saveToFile();
                           });
  } else if (index == 1) {
    if (INKAGENT_STORE.isPaired()) {
      INKAGENT_STORE.clearPairing();
      INKAGENT_STORE.saveToFile();
      requestUpdate();
    } else {
      startActivityForResult(std::make_unique<InkAgentPairActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { requestUpdate(); });
    }
  } else if (index == 2) {
    syncApps();
  } else if (index == 3) {
    startActivityForResult(std::make_unique<DataSentActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) { requestUpdate(); });
  } else if (index == 4) {
    INKAGENT_STORE.setRelayUrl("");
    INKAGENT_STORE.clearPairing();
    INKAGENT_STORE.saveToFile();
    requestUpdate();
  }
}

void InkAgentSettingsActivity::syncApps() {
  if (!INKAGENT_STORE.isPaired()) {
    syncStatus_ = tr(STR_ASK_NOT_PAIRED);
    requestUpdate();
    return;
  }
  if (!InkAgentClient::heapAllowsTls()) {
    // Said plainly rather than attempted and failed inside TLS, where the only
    // evidence would be a line in the SD log.
    syncStatus_ = tr(STR_SYNC_APPS_NO_MEMORY);
    requestUpdate();
    return;
  }

  // With the background task available, hand it over and return immediately:
  // the screen stays live and the result lands on the card. Falls through to
  // the blocking path only when the task never started.
  if (RelayTask::submitAppSync()) {
    syncStatus_ = tr(STR_SYNC_APPS_QUEUED);
    requestUpdate();
    return;
  }

  // Paint "syncing" before blocking: this holds the task for as long as the
  // relay takes, and a frozen screen with no explanation is the worst version
  // of a slow network.
  syncStatus_ = tr(STR_SYNC_APPS_WORKING);
  requestUpdateAndWait();

  const auto r = InkAgentClient::syncApps();
  char buf[64];
  if (r.revoked) {
    INKAGENT_STORE.clearPairing();
    INKAGENT_STORE.saveToFile();
    syncStatus_ = tr(STR_ASK_NOT_PAIRED);
  } else if (r.unchanged) {
    syncStatus_ = tr(STR_SYNC_APPS_UNCHANGED);
  } else if (r.ok) {
    snprintf(buf, sizeof(buf), "%d %s", r.written, tr(STR_SYNC_APPS_INSTALLED));
    syncStatus_ = buf;
  } else {
    syncStatus_ = tr(STR_SYNC_APPS_FAILED);
  }
  requestUpdate();
}

void InkAgentSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Row 0: effective URL, scheme stripped for space.
  std::string url = INKAGENT_STORE.getRelayUrl();
  const auto schemeEnd = url.find("://");
  if (schemeEnd != std::string::npos) url.erase(0, schemeEnd + 3);
  rowValues_[0] = url;
  // Row 1: who owns this reader, or a hint that pairing happens from the reader menu.
  rowValues_[1] = INKAGENT_STORE.isPaired()
                      ? (INKAGENT_STORE.getOwner().empty() ? tr(STR_ASK_PAIRED) : INKAGENT_STORE.getOwner())
                      : tr(STR_ASK_NOT_PAIRED);
  rowItems_[1].label = INKAGENT_STORE.isPaired() ? tr(STR_ASK_UNPAIR) : tr(STR_ASK_PAIR_NOW);
  // Row 2: apps from the relay, with whatever the last sync reported.
  rowValues_[2] = syncStatus_;
  rowValues_[3].clear();
  rowValues_[4] = INKAGENT_DEFAULT_RELAY;
  const auto defSchemeEnd = rowValues_[4].find("://");
  if (defSchemeEnd != std::string::npos) rowValues_[4].erase(0, defSchemeEnd + 3);

  for (int i = 0; i < MENU_ITEMS; i++) rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
