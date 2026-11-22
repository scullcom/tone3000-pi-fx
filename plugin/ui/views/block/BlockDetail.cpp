#include "BlockDetail.h"

#include "core/Help.h"
#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "services/Lv2Artwork.h"
#include "widgets/form/FormControls.h"
#include "widgets/form/SelectField.h"

#include <map>

namespace t3k::ui {

namespace {
// The block across both lanes, and whether an enabled+loaded NAM follows it
// in its lane (the DSP's lastNamIndex scan: with calibration on, such a
// block hands off at calibrated output level instead of normalizing).
struct Found {
  const ChainItem* item = nullptr;
  bool namDownstream = false;
};

Found find(const ChainState& state, const std::string& blockId) {
  auto scan = [&](const std::vector<ChainItem>& lane) -> std::optional<Found> {
    for (size_t i = 0; i < lane.size(); ++i) {
      if (lane[i].isInsert || lane[i].blockId != blockId) continue;
      Found f{&lane[i], false};
      for (size_t j = i + 1; j < lane.size(); ++j) {
        const auto& b = lane[j];
        if (b.isTone() && b.tone.isNam() && b.loaded && b.params.enabled) f.namDownstream = true;
      }
      return f;
    }
    return std::nullopt;
  };
  if (auto f = scan(state.chain)) return *f;
  if (state.chainRight)
    if (auto f = scan(*state.chainRight)) return *f;
  return {};
}
}  // namespace

class BlockDetail::Lv2Controls : public juce::Component {
public:
  Lv2Controls(Services& services, const std::string& id) : services_(services), id_(id) {
    title_.setFont(Fonts::sans(17, true));
    status_.setFont(Fonts::sans(13));
    title_.setColour(juce::Label::textColourId, theme::kWhite);
    status_.setColour(juce::Label::textColourId, theme::kGray);
    addAndMakeVisible(title_);
    addAndMakeVisible(status_);
    addChildComponent(retry_);
    addAndMakeVisible(chooseImage_);
    addChildComponent(removeImage_);
    chooseImage_.onClick = [this] { chooseImage(); };
    removeImage_.onClick = [this] {
      const auto result = lv2Artwork::removeCustom(services_.prefs, uri_);
      if (result.failed()) services_.toast.show(result.getErrorMessage());
      syncArtwork();
    };
    retry_.onClick = [this] { services_.chain.retryModelLoad(id_); };
  }

  void sync(const ChainItem& block) {
    title_.setText(block.tone.title, juce::dontSendNotification);
    title_.setTooltip(block.lv2Uri);
    if (uri_ != block.lv2Uri) {
      uri_ = block.lv2Uri;
      controls_.clear();
      syncArtwork();
    }
    retry_.setVisible(block.loadFailed && !block.modelLoading && services_.backend.supportsLv2());

    const auto parameters = services_.backend.getLv2Parameters(id_);
    const auto* rows = parameters.getArray();
    const auto count = rows != nullptr ? rows->size() : 0;
    if (static_cast<int>(controls_.size()) != count) {
      controls_.clear();
      controls_.reserve(static_cast<size_t>(count));
      for (int i = 0; i < count; ++i) {
        auto row = std::make_unique<Row>(services_, id_, (*rows)[i]);
        addAndMakeVisible(*row);
        controls_.push_back(std::move(row));
      }
    }
    for (int i = 0; i < count; ++i)
      controls_[static_cast<size_t>(i)]->sync((*rows)[i]);

    status_.setText(block.modelLoading ? "Loading LV2 effect..."
                    : block.loadFailed ? block.lv2Error
                    : count == 0 ? "No adjustable parameters" : juce::String(),
                    juce::dontSendNotification);
    status_.setVisible(status_.getText().isNotEmpty());
    const int height = 96 + (status_.isVisible() ? 30 : 0)
                           + (retry_.isVisible() ? 38 : 0) + count * kRowHeight;
    setSize(getWidth(), height);
    resized();
  }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
    paint::border(g, box, theme::kPanelCorner, theme::kBorder);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(16, 8);
    title_.setBounds(area.removeFromTop(40));
    auto artwork = area.removeFromTop(32);
    chooseImage_.setBounds(artwork.removeFromLeft(juce::jmin(142, artwork.getWidth() / 2)));
    artwork.removeFromLeft(8);
    removeImage_.setBounds(artwork.removeFromLeft(juce::jmin(132, artwork.getWidth())));
    area.removeFromTop(8);
    if (status_.isVisible()) status_.setBounds(area.removeFromTop(30));
    if (retry_.isVisible()) retry_.setBounds(area.removeFromTop(30).removeFromLeft(96));
    if (retry_.isVisible()) area.removeFromTop(8);
    for (auto& row : controls_) row->setBounds(area.removeFromTop(kRowHeight));
  }

private:
  void syncArtwork() {
    const bool custom = lv2Artwork::hasCustom(services_.prefs, uri_);
    chooseImage_.setLabel(custom ? "Replace image" : "Choose image...");
    chooseImage_.setEnabled(services_.prefs.settingsFile() != juce::File{});
    removeImage_.setVisible(custom);
  }

  void chooseImage() {
    if (chooser_) return;
    chooser_ = std::make_unique<juce::FileChooser>("Choose an effect image", juce::File{},
                                                  "*.png;*.jpg;*.jpeg");
    const auto safe = juce::Component::SafePointer<Lv2Controls>(this);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                         juce::FileBrowserComponent::canSelectFiles,
                         [safe, uri = uri_](const juce::FileChooser& chooser) {
      if (!safe) return;
      const auto file = chooser.getResult();
      if (file != juce::File{}) {
        const auto result = lv2Artwork::setCustom(safe->services_.prefs, uri, file);
        if (result.failed()) safe->services_.toast.show(result.getErrorMessage());
        safe->syncArtwork();
      }
      juce::MessageManager::callAsync([safe] { if (safe) safe->chooser_.reset(); });
    });
  }

  static constexpr int kRowHeight = 48;

  class Row : public juce::Component {
  public:
    Row(Services& services, const std::string& blockId, const juce::var& parameter) {
      const auto name = parameter["name"].toString();
      const auto id = "lv2:" + parameter["id"].toString();
      const auto write = [&services, blockId, id](double value) {
        services.chain.setBlockParam(blockId, id, value);
      };
      label_.setText(name, juce::dontSendNotification);
      label_.setTooltip(name);
      label_.setFont(Fonts::sans(13));
      label_.setColour(juce::Label::textColourId, theme::kWhite);
      addAndMakeVisible(label_);

      if (static_cast<bool>(parameter["toggle"])) {
        toggle_ = std::make_unique<PillToggle>();
        toggle_->onChange = [write](bool on) { write(on ? 1.0 : 0.0); };
        toggle_->setTitle(name);
        addAndMakeVisible(*toggle_);
      } else if (const auto* options = parameter["options"].getArray(); options && !options->isEmpty()) {
        select_ = std::make_unique<SelectField>(name);
        std::vector<SelectField::Option> choices;
        for (const auto& option : *options) {
          const auto text = option["label"].toString();
          choices.push_back({text, text, {}});
          optionValues_.emplace(text, static_cast<double>(option["value"]));
        }
        select_->setOptions(std::move(choices));
        select_->onChange = [this, write](const juce::String& choice) {
          if (const auto it = optionValues_.find(choice); it != optionValues_.end())
            write(it->second);
        };
        addAndMakeVisible(*select_);
      } else {
        slider_ = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                 juce::Slider::TextBoxRight);
        slider_->setTitle(name);
        slider_->setTextBoxStyle(juce::Slider::TextBoxRight, true, 86, 24);
        slider_->setScrollWheelEnabled(false);
        slider_->setViewportIgnoreDragFlag(true);
        slider_->setColour(juce::Slider::trackColourId, theme::kGray);
        slider_->setColour(juce::Slider::thumbColourId, theme::kWhite);
        slider_->setColour(juce::Slider::textBoxTextColourId, theme::kWhite);
        slider_->setColour(juce::Slider::textBoxOutlineColourId, theme::kBorder);
        const int steps = parameter["steps"];
        slider_->setRange(0.0, 1.0, steps > 1 ? 1.0 / (steps - 1) : 0.0);
        const auto low = static_cast<double>(parameter["min"]);
        const auto high = static_cast<double>(parameter["max"]);
        const auto unit = parameter["unit"].toString();
        slider_->textFromValueFunction = [low, high, unit, steps](double value) {
          const auto text = juce::String(juce::jmap(value, low, high), steps > 1 ? 0 : 3)
                                .trimCharactersAtEnd(steps > 1 ? "" : "0")
                                .trimCharactersAtEnd(".");
          return unit.isEmpty() ? text : text + " " + unit;
        };
        slider_->updateText();
        slider_->onValueChange = [this, write] { write(slider_->getValue()); };
        addAndMakeVisible(*slider_);
      }
    }

    void sync(const juce::var& parameter) {
      const auto value = static_cast<double>(parameter["value"]);
      if (toggle_) toggle_->setValue(value > 0.5);
      if (select_) select_->setValue(parameter["text"].toString());
      if (slider_ && slider_->getThumbBeingDragged() < 0)
        slider_->setValue(value, juce::dontSendNotification);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(2, 4);
      label_.setBounds(area.removeFromLeft((area.getWidth() * 2) / 5));
      area.removeFromLeft(8);
      if (toggle_)
        toggle_->setBounds(area.removeFromRight(PillToggle::kWidth)
                               .withSizeKeepingCentre(PillToggle::kWidth, PillToggle::kHeight));
      if (select_) select_->setBounds(area.withSizeKeepingCentre(area.getWidth(), 34));
      if (slider_) slider_->setBounds(area);
    }

  private:
    juce::Label label_;
    std::unique_ptr<PillToggle> toggle_;
    std::unique_ptr<SelectField> select_;
    std::unique_ptr<juce::Slider> slider_;
    std::map<juce::String, double> optionValues_;
  };

  Services& services_;
  std::string id_;
  juce::String uri_;
  std::unique_ptr<juce::FileChooser> chooser_;
  FormButton chooseImage_{"Choose image...", FormButton::outlined()};
  FormButton removeImage_{"Remove image", FormButton::outlined()};
  juce::Label title_, status_;
  FormButton retry_{"Retry", FormButton::outlined()};
  std::vector<std::unique_ptr<Row>> controls_;
};

BlockDetail::BlockDetail(Services& services, const std::string& blockId)
    : services_(services),
      blockId_(blockId),
      scroller_(std::make_unique<DragScroller>(DragScroller::Axis::vertical)),
      back_(std::make_unique<BackLink>("BLOCK", help::Key::backToChain)) {
  back_->onClick = [this] {
    if (onBack) onBack();
  };
  column_.addAndMakeVisible(*back_);
  scroller_->setViewedComponent(&column_, false);
  scroller_->setFocusMargin(kBackGap);  // a focused control clears the row above and below it
  addAndMakeVisible(*scroller_);
  services_.chain.addListener(this);
  sync();
}

BlockDetail::~BlockDetail() { services_.chain.removeListener(this); }

void BlockDetail::chainChanged(const ChainState&) { sync(); }

void BlockDetail::componentMovedOrResized(juce::Component&, bool, bool wasResized) {
  if (wasResized) layout();
}

void BlockDetail::sync() {
  const auto found = find(services_.chain.state(), blockId_);
  if (found.item == nullptr) {
    // Drop a block that vanished: left alone it could reopen a dead view.
    if (onBack) onBack();
    return;
  }
  if (found.item->isLv2) {
    card_.reset();
    if (!lv2_) {
      lv2_ = std::make_unique<Lv2Controls>(services_, blockId_);
      column_.addAndMakeVisible(*lv2_);
    }
    lv2_->sync(*found.item);
    layout();
    return;
  }
  lv2_.reset();
  if (!card_) {
    card_ = std::make_unique<BlockCard>(services_, *found.item, found.namDownstream);
    card_->onSwap = [this] {
      if (onSwap) onSwap(blockId_);
    };
    card_->onInfoVisible = [this](bool open) {
      infoOpen_ = open;
      layout();
    };
    card_->addComponentListener(this);
    column_.addAndMakeVisible(*card_);
  } else {
    card_->setBlock(*found.item, found.namDownstream);
  }
  layout();
}

void BlockDetail::resized() { layout(); }

void BlockDetail::layout() {
  scroller_->setBounds(getLocalBounds());
  if (lv2_) {
    const auto width = juce::jmin(BlockCard::kWidth, getWidth());
    const auto x = (getWidth() - width) / 2;
    back_->setTopLeftPosition(x, kPadY);
    lv2_->setBounds(x, back_->getBottom() + kBackGap, width, lv2_->getHeight());
    column_.setSize(getWidth(), juce::jmax(getHeight(), lv2_->getBottom() + kPadY));
    return;
  }
  if (!card_) return;
  const int x = (getWidth() - BlockCard::kWidth) / 2;
  // The back link keeps its place under the header; the card fills the
  // design column exactly, so a stretched column (PluginRoot::setStretch)
  // centres it in the room below the link. Info view: the pads live in the
  // scroll content; otherwise the column is pinned under the shared pad and
  // never scrolls.
  back_->setTopLeftPosition(x, kPadY);
  const int cardTop = back_->getBottom() + kBackGap;
  const int room = getHeight() - cardTop - kPadY - card_->getHeight();
  card_->setTopLeftPosition(x, cardTop + std::max(0, room) / 2);
  column_.setSize(getWidth(), infoOpen_ ? std::max(card_->getBottom() + kPadY, getHeight()) : getHeight());
  if (!infoOpen_) scroller_->setViewPosition(0, 0);
}

}  // namespace t3k::ui
