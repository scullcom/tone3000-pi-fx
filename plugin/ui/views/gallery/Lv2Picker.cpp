#include "GalleryTile.h"

#include "core/Fonts.h"
#include "core/Icons.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/DragScroller.h"
#include "widgets/IconButton.h"
#include "widgets/MenuRow.h"
#include "widgets/form/FormControls.h"

#include <algorithm>

namespace t3k::ui {

namespace {

constexpr auto kLv2Favourites = "t3k.lv2Favourites";
class EffectMenuRow final : public juce::Component {
public:
  EffectMenuRow(const juce::String& name, bool favourite,
                std::function<void()> select, std::function<void()> editFavourite)
      : effect_(name, std::nullopt, MenuRow::kContext),
        favourite_(favourite ? Icon::Trash2 : Icon::Plus, 26) {
    effect_.onClick = std::move(select);
    favourite_.setTitle((favourite ? "Remove from favourites: " : "Add to favourites: ") + name);
    favourite_.onClick = std::move(editFavourite);
    addAndMakeVisible(effect_);
    addAndMakeVisible(favourite_);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(3, 0);
    auto action = area.removeFromRight(28);
    favourite_.setBounds(action.withSizeKeepingCentre(26, 26));
    effect_.setBounds(area);
  }

private:
  MenuRow effect_;
  IconButton favourite_;
};

juce::StringArray getLv2Favourites(UiPrefs& prefs) {
  prefs.sync();
  const auto saved = prefs.getJson(kLv2Favourites);
  juce::StringArray favourites;
  if (const auto* entries = saved.getArray()) {
    for (const auto& entry : *entries)
      if (entry.isString()) favourites.addIfNotAlreadyThere(entry.toString());
  } else {
    favourites = juce::StringArray::fromTokens(
        juce::SystemStats::getEnvironmentVariable("T3K_LV2_FAVORITES", {}), ";", "");
  }
  favourites.trim();
  favourites.removeEmptyStrings();
  return favourites;
}

void saveFavourites(UiPrefs& prefs, const juce::StringArray& favourites) {
  juce::Array<juce::var> saved;
  for (const auto& favourite : favourites) saved.add(favourite);
  prefs.setJson(kLv2Favourites, saved);
}

class Lv2Picker final : public Popover {
public:
  Lv2Picker(GalleryTile& tile, juce::var effects, UiPrefs& prefs,
            std::function<void(const juce::var&)> select)
      : tile_(tile), effects_(std::move(effects)), prefs_(prefs), select_(std::move(select)) {
    setTitle("LV2 effects");
    title_.setFont(Fonts::sans(15, true));
    title_.setColour(juce::Label::textColourId, theme::kWhite);
    addAndMakeVisible(title_);

    back_.setTitle("Back to Favourites");
    back_.onClick = [this] { show(false); };
    addChildComponent(back_);
    search_.setTitle("Search installed effects");
    search_.setFont(Fonts::sans(14));
    search_.setTextToShowWhenEmpty("Search installed effects", theme::kGray);
    search_.setColour(juce::TextEditor::backgroundColourId, theme::kSurfaceRaised);
    search_.setColour(juce::TextEditor::textColourId, theme::kWhite);
    search_.setColour(juce::TextEditor::outlineColourId, theme::kBorder);
    search_.onTextChange = [this] { refresh(); };
    search_.onEscapeKey = [this] { dismiss(); };
    addChildComponent(search_);

    other_.onClick = [this] { show(true); };
    addChildComponent(other_);
    viewport_.setViewedComponent(&results_, false);
    addAndMakeVisible(viewport_);
    empty_.setColour(juce::Label::textColourId, theme::kGray);
    addChildComponent(empty_);
  }

  void show(bool otherEffects) {
    auto* host = tile_.findParentComponentOfClass<OverlayHost>();
    if (host == nullptr) return;
    close();
    showingOther_ = otherEffects;
    favourites_ = getLv2Favourites(prefs_);
    title_.setText(otherEffects ? "Other installed effects" : "Favourites",
                   juce::dontSendNotification);
    back_.setVisible(otherEffects);
    search_.setVisible(otherEffects);
    search_.setText({}, false);
    refresh();

    const auto& overlay = host->overlayLayer();
    const auto available = tile_.getLocalArea(&overlay, overlay.getLocalBounds());
    const int rowsHeight = juce::jmax(1, static_cast<int>(rows_.size())) * MenuRow::kContext.height;
    const int header = kTitleHeight + (otherEffects ? kSearchHeight + kGap : 0);
    const int footer = other_.isVisible() ? kButtonHeight + kGap : 0;
    const int height = 2 * kInset + header + (otherEffects ? 250 : juce::jmin(250, rowsHeight)) + footer;
    setSize(juce::jmin(250, available.getWidth()), juce::jmin(height, available.getHeight()));
    resized();
    openAt(tile_, tile_.getLocalBounds().getCentre().translated(ContextMenu::kCursorOffset,
                                                               ContextMenu::kCursorOffset));
    if (otherEffects && isShowing()) search_.grabKeyboardFocus();
  }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    paint::fill(g, box, theme::kPanelCorner, theme::kPanelBg);
    paint::border(g, box, theme::kPanelCorner, theme::kBorder);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(kInset);
    auto header = area.removeFromTop(kTitleHeight);
    if (showingOther_) {
      back_.setBounds(header.removeFromLeft(26).withSizeKeepingCentre(24, 24));
      search_.setBounds(area.removeFromTop(kSearchHeight));
      area.removeFromTop(kGap);
    }
    title_.setBounds(header);
    if (other_.isVisible()) {
      other_.setBounds(area.removeFromBottom(kButtonHeight));
      area.removeFromBottom(kGap);
    }
    viewport_.setBounds(area);
    empty_.setBounds(area.removeFromTop(MenuRow::kContext.height));
    layoutRows();
  }

private:
  static constexpr int kInset = Popover::kBorder + ContextMenu::kPad;
  static constexpr int kTitleHeight = 30;
  static constexpr int kSearchHeight = 30;
  static constexpr int kButtonHeight = 33;
  static constexpr int kGap = 6;

  void refresh() {
    rows_.clear();
    const auto query = search_.getText().trim();
    bool hasOtherEffects = false;
    for (const auto& effect : *effects_.getArray()) {
      const auto uri = effect["uri"].toString();
      const bool favourite = favourites_.contains(uri);
      hasOtherEffects |= !favourite;
      const auto name = effect["name"].toString();
      if (favourite == showingOther_ || (query.isNotEmpty() && !name.containsIgnoreCase(query)))
        continue;
      auto row = std::make_unique<EffectMenuRow>(name, favourite,
          [this, effect] {
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Lv2Picker>(this), effect] {
              if (!safe || !safe->isOpen()) return;
              auto select = safe->select_;
              safe->dismiss();
              select(effect);
            });
          },
          [this, uri, favourite] {
            // Change pages after the row's button callback has returned.
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Lv2Picker>(this), uri, favourite] {
              if (!safe || !safe->isOpen()) return;
              auto favourites = getLv2Favourites(safe->prefs_);
              if (favourite)
                favourites.removeString(uri);
              else
                favourites.addIfNotAlreadyThere(uri);
              saveFavourites(safe->prefs_, favourites);
              if (safe) safe->show(false);
            });
          });
      results_.addAndMakeVisible(*row);
      rows_.push_back(std::move(row));
    }
    other_.setVisible(!showingOther_ && hasOtherEffects);
    empty_.setText(showingOther_ ? "No matching effects" : "No favourites yet",
                   juce::dontSendNotification);
    empty_.setVisible(rows_.empty());
    layoutRows();
    viewport_.setViewPosition(0, 0);
  }

  void layoutRows() {
    results_.setSize(viewport_.getWidth(), static_cast<int>(rows_.size()) * MenuRow::kContext.height);
    auto area = results_.getLocalBounds();
    for (auto& row : rows_) row->setBounds(area.removeFromTop(MenuRow::kContext.height));
  }

  GalleryTile& tile_;
  juce::var effects_;
  UiPrefs& prefs_;
  std::function<void(const juce::var&)> select_;
  juce::StringArray favourites_;
  bool showingOther_ = false;
  juce::Label title_, empty_;
  IconButton back_{Icon::ArrowLeft, 24};
  FormButton other_{"Other installed effects", FormButton::outlined()};
  juce::TextEditor search_;
  juce::Component results_;
  DragScroller viewport_{DragScroller::Axis::vertical, DragScroller::Keys::none};
  std::vector<std::unique_ptr<EffectMenuRow>> rows_;
};

}  // namespace

void GalleryTile::pickLv2Effect(bool replace) {
  if (scanningEffects_ || !services().backend.supportsLv2()) return;
  scanningEffects_ = true;
  services().toast.show("Scanning installed LV2 effects");
  const auto safe = juce::Component::SafePointer<GalleryTile>(this);
  services().backend.scanLv2Effects([safe, replace](juce::var list) {
    if (!safe) return;
    safe->scanningEffects_ = false;
    auto* effects = list.getArray();
    if (effects == nullptr || effects->isEmpty()) {
      safe->services().toast.show("No installed LV2 audio effects found");
      return;
    }
    std::sort(effects->begin(), effects->end(), [](const juce::var& a, const juce::var& b) {
      return a["name"].toString().compareNatural(b["name"].toString()) < 0;
    });
    auto picker = std::make_unique<Lv2Picker>(*safe.getComponent(), list, safe->services().prefs,
        [safe, replace](const juce::var& effect) {
          if (!safe) return;
          auto& services = safe->services();
          const auto id = safe->blockId();
          const auto uri = effect["uri"].toString();
          if (replace) {
            const auto finished = [safe](const juce::String& error) {
              if (safe && error.isNotEmpty())
                safe->services().toast.show("Couldn't replace effect: " + error);
            };
            if (!services.chain.swapLv2Effect(id, uri, finished))
              services.toast.show("That effect can't be replaced right now");
          } else if (services.chain.addLv2Effect(id, uri, effect["name"].toString()).empty()) {
            services.toast.show("That slot is no longer available");
          }
        });
    auto& panel = *picker;
    safe->effectPicker_ = std::move(picker);
    panel.show(false);
  });
}

}  // namespace t3k::ui
