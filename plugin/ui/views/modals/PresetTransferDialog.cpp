#include "PresetTransferDialog.h"

#include "PresetManager.h"
#include "core/Fonts.h"
#include "core/Paint.h"
#include "core/Theme.h"
#include "widgets/DragScroller.h"
#include "widgets/form/FormControls.h"

#include <algorithm>
#include <optional>

namespace t3k::ui {
namespace {

struct UsbVolume {
  juce::String device, uuid, label;
  juce::File mount;
};

std::optional<juce::String> runCommand(const juce::StringArray& command, int timeout = 4000) {
  juce::ChildProcess child;
  if (!child.start(command)) return {};
  if (!child.waitForProcessToFinish(timeout)) {
    child.kill();
    return {};
  }
  if (child.getExitCode() != 0) return {};
  return child.readAllProcessOutput();
}

bool containsSystemVolume(const juce::var& device) {
  const auto mount = device["mountpoint"].toString();
  if (mount == "/" || mount == "/boot" || mount == "/boot/firmware" || mount == "/home" || mount == "/usr")
    return true;
  const auto children = device["children"];
  if (const auto* rows = children.getArray())
    return std::any_of(rows->begin(), rows->end(), containsSystemVolume);
  return false;
}

void appendVolumes(const juce::var& device, std::vector<UsbVolume>& result) {
  const auto fs = device["fstype"].toString();
  const auto path = device["name"].toString();
  const auto mount = device["mountpoint"].toString();
  if (path.startsWith("/dev/") && fs.isNotEmpty() && fs != "swap" && fs != "crypto_LUKS" &&
      !fs.endsWith("_member")) {
    auto label = device["label"].toString();
    if (label.isEmpty()) label = path;
    result.push_back({path, device["uuid"].toString(), label,
                      juce::File::isAbsolutePath(mount) ? juce::File(mount) : juce::File{}});
  }
  const auto children = device["children"];
  if (const auto* rows = children.getArray())
    for (const auto& child : *rows) appendVolumes(child, result);
}

std::vector<UsbVolume> scanVolumes() {
  const auto output = runCommand({"/usr/bin/lsblk", "--json", "--tree", "--paths",
                                  "--output", "NAME,TRAN,FSTYPE,MOUNTPOINT,LABEL,UUID"});
  if (!output) return {};
  const auto json = juce::JSON::parse(*output);
  const auto devices = json["blockdevices"];
  std::vector<UsbVolume> volumes;
  if (const auto* rows = devices.getArray()) {
    for (const auto& device : *rows)
      if (device["tran"].toString() == "usb" && !containsSystemVolume(device)) appendVolumes(device, volumes);
  }
  return volumes;
}

bool sameDevice(const UsbVolume& a, const UsbVolume& b) {
  return a.device == b.device && a.uuid == b.uuid;
}

bool stillMounted(const UsbVolume& chosen) {
  const auto volumes = scanVolumes();
  return chosen.mount != juce::File{} && std::any_of(volumes.begin(), volumes.end(), [&](const auto& now) {
    return sameDevice(now, chosen) && now.mount == chosen.mount;
  });
}

juce::File mountVolume(const UsbVolume& selected) {
  const auto lookupMount = [&] {
    for (const auto& volume : scanVolumes())
      if (sameDevice(volume, selected)) return volume.mount;
    return juce::File{};
  };
  auto mount = lookupMount();
  if (mount != juce::File{}) return mount;
  runCommand({"/usr/bin/udisksctl", "mount", "--no-user-interaction", "--block-device", selected.device});
  return lookupMount();
}

constexpr int kRowHeight = 32;
constexpr int kPad = 20;

}  // namespace

class PresetTransferDialog::Card : public juce::Component {
public:
  Card(PresetTransferDialog& owner, juce::File presetDirectory, bool importing)
      : owner_(owner), presetDirectory_(std::move(presetDirectory)), importing_(importing) {
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);
    title_.setText(importing_ ? "Import presets" : "Export presets", juce::dontSendNotification);
    title_.setFont(Fonts::sans(19, true));
    title_.setColour(juce::Label::textColourId, theme::kWhite);
    title_.setBorderSize(juce::BorderSize<int>(0));
    status_.setFont(Fonts::sans(14));
    status_.setColour(juce::Label::textColourId, theme::kWhite);
    status_.setBorderSize(juce::BorderSize<int>(0));
    addAndMakeVisible(title_);
    addAndMakeVisible(status_);
    addAndMakeVisible(close_);
    addChildComponent(drivePicker_);
    addChildComponent(changeDrive_);
    addChildComponent(apply_);
    addChildComponent(scroll_);
    scroll_.setViewedComponent(&rows_, false);
    close_.onClick = [this] { if (owner_.onClose) owner_.onClose(); };
    changeDrive_.onClick = [this] { resetDrives(); };
    drivePicker_.setTitle("USB drive");
    drivePicker_.onChange = [this] {
      const int index = drivePicker_.getSelectedItemIndex();
      if (juce::isPositiveAndBelow(index, static_cast<int>(volumes_.size())))
        prepareVolume(volumes_[static_cast<size_t>(index)]);
    };
    apply_.onClick = [this] { transfer(); };
    setSize(580, 438);
    resetDrives();
  }

  ~Card() override { scroll_.setViewedComponent(nullptr, false); }

  bool keyPressed(const juce::KeyPress& key) override {
    if (key != juce::KeyPress::escapeKey) return false;
    if (close_.isEnabled() && owner_.onClose) owner_.onClose();
    return true;
  }

  void paint(juce::Graphics& g) override {
    const auto box = getLocalBounds().toFloat();
    paint::fill(g, box, theme::kPanelCorner, theme::kSurfaceRaised);
    paint::border(g, box, theme::kPanelCorner, theme::kBorder);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(kPad);
    title_.setBounds(area.removeFromTop(32));
    status_.setBounds(area.removeFromTop(56));
    drivePicker_.setBounds(area.removeFromTop(drivePicker_.isVisible() ? 34 : 0));
    auto buttons = area.removeFromBottom(36);
    close_.setBounds(buttons.removeFromRight(94));
    buttons.removeFromRight(8);
    apply_.setBounds(buttons.removeFromRight(142));
    changeDrive_.setBounds(buttons.removeFromLeft(126));
    area.removeFromBottom(8);
    scroll_.setBounds(area);
    rows_.setSize(scroll_.getMaximumVisibleWidth(), rowCount_ * kRowHeight);
    auto rowArea = rows_.getLocalBounds();
    for (const auto& group : groups_) {
      group.heading->setBounds(rowArea.removeFromTop(kRowHeight));
      for (const auto& preset : group.presets)
        preset->setBounds(rowArea.removeFromTop(kRowHeight).withTrimmedLeft(24));
    }
  }

private:
  struct GroupRows {
    juce::String id;
    std::unique_ptr<juce::ToggleButton> heading;
    std::vector<std::unique_ptr<juce::ToggleButton>> presets;
    juce::StringArray ids;
  };

  void status(const juce::String& message) {
    status_.setText(message, juce::dontSendNotification);
  }

  void resetDrives() {
    ++scanGeneration_;
    groups_.clear();
    rowCount_ = 0;
    volumes_.clear();
    selected_ = {};
    drivePicker_.setVisible(false);
    changeDrive_.setVisible(false);
    apply_.setVisible(false);
    scroll_.setVisible(false);
    status("Insert a USB drive.");
    scan(scanGeneration_);
  }

  void scan(int generation) {
    const auto safe = juce::Component::SafePointer<Card>(this);
    juce::Thread::launch([safe, generation] {
      auto volumes = scanVolumes();
      juce::MessageManager::callAsync([safe, generation, volumes = std::move(volumes)]() mutable {
        if (!safe || safe->scanGeneration_ != generation) return;
        if (volumes.size() == 1 && !safe->drivePicker_.isVisible()) {
          safe->prepareVolume(volumes.front());
          return;
        }
        safe->showDrives(std::move(volumes));
        juce::Timer::callAfterDelay(1200, [safe, generation] {
          if (safe && safe->scanGeneration_ == generation) safe->scan(generation);
        });
      });
    });
  }

  void showDrives(std::vector<UsbVolume> volumes) {
    const bool changed = volumes.size() != volumes_.size() ||
        !std::equal(volumes.begin(), volumes.end(), volumes_.begin(), sameDevice);
    if (!changed) return;
    volumes_ = std::move(volumes);
    drivePicker_.clear(juce::dontSendNotification);
    int index = 1;
    for (const auto& volume : volumes_)
      drivePicker_.addItem(volume.label + " (" + volume.device + ")", index++);
    drivePicker_.setTextWhenNothingSelected("Choose a USB drive");
    drivePicker_.setVisible(!volumes_.empty());
    status(volumes_.empty() ? "Insert a USB drive." : "Choose the USB drive to use.");
    resized();
  }

  void prepareVolume(UsbVolume volume) {
    ++scanGeneration_;
    drivePicker_.setVisible(false);
    status("Opening " + volume.label + "...");
    const auto safe = juce::Component::SafePointer<Card>(this);
    juce::Thread::launch([safe, volume = std::move(volume), importing = importing_, dir = presetDirectory_]() mutable {
      volume.mount = mountVolume(volume);
      std::vector<PresetManager::TransferGroup> groups;
      if (volume.mount != juce::File{})
        groups = importing ? PresetManager::usbBackupGroups(volume.mount) : PresetManager(dir).transferGroups();
      const bool existing = volume.mount != juce::File{} && PresetManager::usbBackupFolder(volume.mount).exists();
      juce::MessageManager::callAsync([safe, volume = std::move(volume), groups = std::move(groups), existing]() mutable {
        if (safe) safe->showPresets(std::move(volume), std::move(groups), existing);
      });
    });
  }

  void showPresets(UsbVolume volume, const std::vector<PresetManager::TransferGroup>& groups, bool existing) {
    selected_ = std::move(volume);
    changeDrive_.setVisible(true);
    if (selected_.mount == juce::File{}) {
      status("Could not mount " + selected_.label + ". Check the drive and try again.");
      resized();
      return;
    }
    if (groups.empty()) {
      status(importing_ ? "No presets found in /Tone3000-pi-fx/presets on this drive."
                        : "You have no user presets or banks to export.");
      resized();
      return;
    }
    status(importing_ ? "Select what to import from " + selected_.label + ". Matching preset names in each bank will be overwritten."
                      : "Select what to export to " + selected_.label + ". " +
                          (existing ? "This replaces the existing backup." : "The backup goes in /Tone3000-pi-fx/presets."));
    apply_.setLabel(importing_ ? "Import selected" : existing ? "Replace backup" : "Export selected");
    apply_.setVisible(true);
    scroll_.setVisible(true);
    for (const auto& group : groups) {
      GroupRows row;
      row.id = group.id;
      const auto index = groups_.size();
      row.heading = makeTick(group.name);
      row.heading->onClick = [this, index] {
        auto& rows = groups_[index];
        for (auto& preset : rows.presets)
          preset->setToggleState(rows.heading->getToggleState(), juce::dontSendNotification);
        updateApply();
      };
      for (const auto& preset : group.presets) {
        auto tick = makeTick(preset.name);
        tick->onClick = [this, index] {
          auto& rows = groups_[index];
          rows.heading->setToggleState(std::all_of(rows.presets.begin(), rows.presets.end(),
              [](const auto& item) { return item->getToggleState(); }), juce::dontSendNotification);
          updateApply();
        };
        row.ids.add(preset.id);
        row.presets.push_back(std::move(tick));
      }
      rowCount_ += 1 + static_cast<int>(row.presets.size());
      groups_.push_back(std::move(row));
    }
    updateApply();
    resized();
    scroll_.setViewPosition(0, 0);
  }

  std::unique_ptr<juce::ToggleButton> makeTick(const juce::String& label) {
    auto tick = std::make_unique<juce::ToggleButton>(label);
    tick->setColour(juce::ToggleButton::textColourId, theme::kWhite);
    rows_.addAndMakeVisible(*tick);
    return tick;
  }

  PresetManager::TransferSelection selection() const {
    PresetManager::TransferSelection result;
    for (const auto& group : groups_) {
      if (group.heading->getToggleState()) result.banks.add(group.id);
      for (int i = 0; i < group.ids.size(); ++i)
        if (group.presets[static_cast<size_t>(i)]->getToggleState()) result.presets.add(group.ids[i]);
    }
    return result;
  }

  void updateApply() {
    const auto picked = selection();
    apply_.setEnabled(!picked.presets.isEmpty() || !picked.banks.isEmpty());
  }

  void transfer() {
    const auto picked = selection();
    close_.setEnabled(false);
    apply_.setEnabled(false);
    changeDrive_.setEnabled(false);
    scroll_.setEnabled(false);
    status(importing_ ? "Importing presets..." : "Writing backup. Keep the USB drive connected.");
    const auto safe = juce::Component::SafePointer<Card>(this);
    juce::Thread::launch([safe, picked, volume = selected_, importing = importing_, dir = presetDirectory_] {
      const auto mounted = [&] { return stillMounted(volume); };
      PresetManager store(dir);
      auto result = importing ? store.importUsbBackup(volume.mount, picked, mounted)
                              : store.exportUsbBackup(volume.mount, picked, mounted);
      if (!importing && result.error.isEmpty() &&
          (!mounted() || !runCommand({"/usr/bin/sync", "-f", "--", volume.mount.getFullPathName()}, 30000))) {
        result = {0, "Could not confirm the backup was flushed. Reconnect the drive and check the backup."};
      }
      juce::MessageManager::callAsync([safe, importing, result] {
        if (!safe) return;
        if (importing && safe->owner_.onImported) safe->owner_.onImported();
        if (!safe) return;
        const auto count = juce::String(result.presets) + (result.presets == 1 ? " preset" : " presets");
        if (result.error.isEmpty()) {
          safe->status(importing ? "Imported " + count + "." : "Exported " + count + " to /Tone3000-pi-fx/presets.");
          safe->close_.setLabel("Done");
          safe->apply_.setVisible(false);
          safe->scroll_.setVisible(false);
          safe->changeDrive_.setVisible(false);
        } else {
          safe->status(result.error + (result.presets > 0 ? " Imported " + count + " before stopping." : ""));
          safe->scroll_.setEnabled(true);
          safe->changeDrive_.setEnabled(true);
          safe->updateApply();
        }
        safe->close_.setEnabled(true);
        safe->resized();
      });
    });
  }

  PresetTransferDialog& owner_;
  const juce::File presetDirectory_;
  const bool importing_;
  int scanGeneration_ = 0, rowCount_ = 0;
  std::vector<UsbVolume> volumes_;
  UsbVolume selected_;
  juce::Label title_, status_;
  juce::ComboBox drivePicker_;
  FormButton close_{"Cancel", FormButton::outlined()};
  FormButton changeDrive_{"Change drive", FormButton::outlined()};
  FormButton apply_{"", FormButton::cta()};
  juce::Component rows_;
  DragScroller scroll_{DragScroller::Axis::vertical};
  std::vector<GroupRows> groups_;
};

PresetTransferDialog::PresetTransferDialog(Backdrop backdrop, juce::File presetDirectory, bool importing)
    : ModalLayer(std::move(backdrop)), card_(std::make_unique<Card>(*this, std::move(presetDirectory), importing)) {
  setTitle(importing ? "Import presets" : "Export presets");
  setContent(*card_);
}

PresetTransferDialog::~PresetTransferDialog() = default;

}  // namespace t3k::ui
