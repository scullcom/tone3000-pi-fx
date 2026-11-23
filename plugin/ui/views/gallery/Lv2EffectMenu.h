#pragma once

#include <memory>
#include <string>

#include "widgets/Popover.h"

namespace t3k::ui {

class Services;

// Shared LV2 picker for the gallery and block detail.
class Lv2EffectMenu {
public:
  enum class Placement { atTile, belowButton };

  Lv2EffectMenu(Services& services, juce::Component& anchor,
                Placement placement = Placement::atTile);
  ~Lv2EffectMenu();
  void show(const std::string& blockId, bool replace);

private:
  Services& services_;
  juce::Component& anchor_;
  Placement placement_;
  bool scanning_ = false;
  std::unique_ptr<Popover> panel_;
  JUCE_DECLARE_WEAK_REFERENCEABLE(Lv2EffectMenu)
};

}  // namespace t3k::ui
