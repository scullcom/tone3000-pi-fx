#pragma once

#include "widgets/ModalLayer.h"

namespace t3k::ui {

class PresetTransferDialog : public ModalLayer {
public:
  PresetTransferDialog(Backdrop backdrop, juce::File presetDirectory, bool importing);
  ~PresetTransferDialog() override;

  std::function<void()> onClose;
  std::function<void()> onImported;

private:
  class Card;
  std::unique_ptr<Card> card_;
};

}  // namespace t3k::ui
