#include "ToneTile.h"

#include "GalleryGeometry.h"
#include "core/Help.h"
#include "core/Fonts.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "services/Lv2Artwork.h"

namespace t3k::ui {

namespace {
constexpr int kImageFadeMs = 200;
constexpr float kDimmedImage = 0.35f;
const juce::Colour kStrip = juce::Colours::black.withAlpha(0.35f);
}  // namespace

void ToneTile::Strip::paint(juce::Graphics& g) {
  g.setColour(kStrip);
  g.fillRect(getLocalBounds().withHeight(kChromeHeight));
}

ToneTile::ToneTile(Services& services, const ChainItem& block, int size)
    : GalleryTile(services, block.blockId, size),
      block_(block),
      image_(services.images),
      power_(Icon::Power, ChromeIconButton::Tone::power, help::Key::blockPower),
      swap_(Icon::ArrowLeftRight, ChromeIconButton::Tone::plain, help::Key::swapTone),
      remove_(Icon::Trash2, ChromeIconButton::Tone::plain, help::Key::removeBlock),
      glow_(services.meters, MeterStore::blockOutId(block.blockId)),
      led_(services.meters, MeterStore::blockOutId(block.blockId)) {
  image_.setCornerRadius(gallery::kTileCorner);
  image_.setGlow(glow_.glow());
  glow_.onChange = [this] { image_.setGlow(glow_.glow()); };
  addAndMakeVisible(image_);
  addChildComponent(dots_);
  addChildComponent(retry_);
  retry_.onRetry = [this] { this->services().modelLoads.retry(blockId()); };

  addAndMakeVisible(chrome_);
  for (auto* b : {&power_, &swap_, &remove_}) chrome_.addAndMakeVisible(*b);
  power_.onClick = [this] { togglePower(); };
  swap_.onClick = [this] {
    if (block_.isLv2)
      pickLv2Effect(true);
    else if (onSwap)
      onSwap(blockId());
  };
  remove_.onClick = [this] { this->services().chain.removeBlock(blockId()); };

  addAndMakeVisible(ledSlot_);
  ledSlot_.addChildComponent(led_);
  ledSlot_.setSize(BlockLed::kSize, BlockLed::kSize);
  addMouseListener(&hover_, true);
  services.pointer.addListener(this);

  services.prefs.addListener(this);
  setBlock(block);
  setHovered(false);
  resized();  // the base set the size before these children existed
}

ToneTile::~ToneTile() {
  services().prefs.removeListener(this);
  services().pointer.removeListener(this);
  removeMouseListener(&hover_);
}

void ToneTile::setBlock(const ChainItem& block) {
  block_ = block;
  enabled_ = block.params.enabled;
  setHelpText(help::toneTile(block.tone.title));
  setTitle(block.tone.title);
  if (block.isLv2) {
    const auto key = lv2Artwork::preferenceKey(block.lv2Uri);
    if (key != effectArtKey_) {
      effectArtKey_ = key;
      effectArt_ = lv2Artwork::load(services().prefs, block.lv2Uri);
    }
  } else {
    effectArtKey_.clear();
    effectArt_ = {};
    image_.setTone(block.tone.image, block.tone.gear, block.tone.local, kGlyphSize);
  }
  swap_.setVisible(!block.isLv2 || services().backend.supportsLv2());
  swap_.setEnabled(!block.isLv2 || !block.modelLoading);
  if (block.isLv2) {
    swap_.setTitle("Swap LV2 effect");
    swap_.setHelpText("Choose a different LV2 effect for this slot.");
  }
  syncState();
}

// A model download/prepare is in flight: `modelLoading` covers switches
// (the previous model keeps playing, so `loaded` stays true) and `!loaded`
// covers fresh blocks that have nothing to play yet.
void ToneTile::syncState() {
  const bool busy = block_.modelLoading || (!block_.loaded && !block_.loadFailed);
  const bool armed = dropArmed();
  image_.setVisible(!armed && !block_.isLv2);
  imageFade_.animateTo(enabled_ && !busy && !block_.loadFailed ? 1.0f : kDimmedImage, kImageFadeMs);
  dots_.setVisible(!armed && busy && !block_.loadFailed);
  retry_.setVisible(!armed && !block_.isLv2 && block_.loadFailed);
  chrome_.setVisible(!armed);
  ledSlot_.setVisible(!armed);
  power_.setOn(enabled_);
  repaint();
}

void ToneTile::prefChanged(const juce::String& key) {
  if (key != effectArtKey_) return;
  effectArt_ = lv2Artwork::load(services().prefs, block_.lv2Uri);
  repaint();
}

void ToneTile::togglePower() {
  enabled_ = !enabled_;
  power_.setOn(enabled_);
  imageFade_.animateTo(enabled_ ? 1.0f : kDimmedImage, kImageFadeMs);
  this->services().chain.setBlockParam(blockId(), "enabled", enabled_);
}

void ToneTile::open() {
  if (onOpen) onOpen(blockId());
}

std::vector<ContextMenu::Item> ToneTile::menuItems() {
  std::vector<ContextMenu::Item> items{
      {"Copy", Icon::Copy, help::Key::copyBlock,
       [this] { this->services().chain.copyBlock(blockId()); }},
  };
  if (!block_.isLv2)
    for (auto& item : localLoadItems()) items.push_back(std::move(item));
  return items;
}

void ToneTile::dropArmedChanged(bool) { syncState(); }

void ToneTile::travellingChanged(bool) { setHovered(hovered_); }

void ToneTile::pointerMoved(const juce::MouseEvent& e, bool leaving) {
  // Leaving onto one of our own children is still hovering the tile.
  setHovered(!leaving || getLocalBounds().contains(e.getEventRelativeTo(this).getPosition()));
}

void ToneTile::setHovered(bool hovered) {
  hovered_ = hovered;
  const bool shown = services().pointer.coarse() || hovered_ || travelling();
  chrome_.setAlpha(shown ? 1.0f : 0.0f);
  chrome_.setInterceptsMouseClicks(false, shown);
}

void ToneTile::resized() {
  const auto box = getLocalBounds();
  image_.setBounds(box);
  dots_.setBounds(box.withSizeKeepingCentre(LoadingDots::kWidth, LoadingDots::kDot + 2 * LoadingDots::kMargin));
  retry_.setBounds(box.withSizeKeepingCentre(retry_.getWidth(), retry_.getHeight()));

  chrome_.setBounds(box.withHeight(kChromeHeight));
  const int y = kChromePad;
  power_.setBounds(kChromePad, y, theme::kIconBoxSize, theme::kIconBoxSize);
  remove_.setBounds(getWidth() - kChromePad - theme::kIconBoxSize, y, theme::kIconBoxSize,
                    theme::kIconBoxSize);
  swap_.setBounds(remove_.getX() - kChromeGap - theme::kIconBoxSize, y, theme::kIconBoxSize,
                  theme::kIconBoxSize);

  ledSlot_.setTopLeftPosition(getWidth() - kLedInset - BlockLed::kSize,
                              getHeight() - kLedInset - BlockLed::kSize);
}

void ToneTile::paint(juce::Graphics& g) {
  paint::fill(g, getLocalBounds().toFloat(), gallery::kTileCorner, theme::kSurface);
  if (block_.isLv2) {
    auto box = getLocalBounds().reduced(8).withTrimmedTop(kChromeHeight);
    auto status = box.removeFromBottom(22);
    g.setColour(theme::kWhite.withAlpha(enabled_ ? 1.0f : 0.45f));
    g.setFont(Fonts::sans(14, true));
    if (effectArt_.isValid() && !dropArmed()) {
      auto title = box.removeFromBottom(24);
      g.drawFittedText(block_.tone.title, title, juce::Justification::centred, 1);
      g.setOpacity(enabled_ && !block_.loadFailed ? 1.0f : kDimmedImage);
      g.drawImage(effectArt_, box.reduced(6).toFloat(), juce::RectanglePlacement::centred);
      g.setOpacity(1.0f);
    } else {
      g.drawFittedText(block_.tone.title, box, juce::Justification::centred, 3);
    }
    g.setColour(theme::kGray);
    g.setFont(Fonts::mono(10));
    const auto label = block_.loadFailed
                           ? (block_.lv2Error == "Effect is missing" ? "MISSING LV2" : "UNAVAILABLE")
                           : "LV2 EFFECT";
    g.drawText(label, status, juce::Justification::centred);
  }
  if (dropArmed()) {
    const float s = gallery::kFileDropGlyphSize;
    Icons::draw(g, Icon::Upload, getLocalBounds().toFloat().withSizeKeepingCentre(s, s), theme::kGray);
  }
}

void ToneTile::paintOverChildren(juce::Graphics& g) {
  if (dropArmed())
    paint::dashedBorder(g, getLocalBounds().toFloat(), gallery::kTileCorner,
                        gallery::kFileDropBorder, gallery::kAddTileBorderWidth);
}

}  // namespace t3k::ui
