#include "InkAgentSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <InkAgentStore.h>

#include <memory>
#include <string>

#include "MappedInputManager.h"
#include "InkAgentPairActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
const StrId menuNames[InkAgentSettingsActivity::MENU_ITEMS] = {StrId::STR_ASK_RELAY_URL, StrId::STR_ASK_PAIRING,
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
    INKAGENT_STORE.setRelayUrl("");
    INKAGENT_STORE.clearPairing();
    INKAGENT_STORE.saveToFile();
    requestUpdate();
  }
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
  rowValues_[2] = INKAGENT_DEFAULT_RELAY;
  const auto defSchemeEnd = rowValues_[2].find("://");
  if (defSchemeEnd != std::string::npos) rowValues_[2].erase(0, defSchemeEnd + 3);

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
