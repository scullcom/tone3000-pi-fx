#include "PresetStore.h"

namespace t3k::ui {

PresetStore::PresetStore(Backend& backend, ChainStore& chain) : backend_(backend), chain_(chain) {
  refresh();
}

void PresetStore::refresh() {
  const auto list = backend_.getPresetList();
  presets_ = parsePresetList(list);
  banks_.clear();
  const auto bankArray = list["banks"];
  if (const auto* banks = bankArray.getArray())
    for (const auto& bank : *banks)
      banks_.push_back({bank["id"].toString(), bank["name"].toString()});
  listeners.call([this](Listener& l) { l.presetsChanged(presets_); });
}

template <typename Fn>
auto PresetStore::run(Fn&& fn) -> decltype(fn()) {
  auto result = fn();
  refresh();
  chain_.refresh();
  return result;
}

std::optional<PresetInfo> PresetStore::save(const juce::String& name) {
  const auto res = run([&] { return backend_.savePreset(name); });
  if (!res.isObject()) return std::nullopt;
  PresetInfo info;
  info.id = res.getProperty("id", "").toString();
  info.name = res.getProperty("name", "").toString();
  info.factory = false;
  return info.id.isEmpty() ? std::nullopt : std::optional<PresetInfo>(info);
}

bool PresetStore::load(const juce::String& id) {
  return run([&] { return backend_.loadPreset(id); });
}

bool PresetStore::rename(const juce::String& id, const juce::String& name) {
  return run([&] { return backend_.renamePreset(id, name); });
}

bool PresetStore::remove(const juce::String& id) {
  return run([&] { return backend_.deletePreset(id); });
}

bool PresetStore::move(const juce::String& id, int delta) {
  return run([&] { return backend_.movePreset(id, delta); });
}

bool PresetStore::createPresetBank(const juce::String& name) {
  return run([&] { return backend_.createPresetBank(name); });
}

bool PresetStore::renamePresetBank(const juce::String& id, const juce::String& name) {
  return run([&] { return backend_.renamePresetBank(id, name); });
}

bool PresetStore::deletePresetBank(const juce::String& id) {
  return run([&] { return backend_.deletePresetBank(id); });
}

bool PresetStore::selectPresetBank(const juce::String& id) {
  return run([&] { return backend_.selectPresetBank(id); });
}

bool PresetStore::stepPresetBank(int delta) {
  return run([&] { return backend_.stepPresetBank(delta); });
}

bool PresetStore::movePresetToBank(const juce::String& presetId, const juce::String& bankId, int index) {
  return run([&] { return backend_.movePresetToBank(presetId, bankId, index); });
}

bool PresetStore::copyPresetToBank(const juce::String& presetId, const juce::String& bankId) {
  return run([&] { return backend_.copyPresetToBank(presetId, bankId); });
}

}  // namespace t3k::ui
