#include "Processor.h"

#include <cmath>
#include <span>
#include <utility>

namespace {

// Guitarix 0.46.0 keeps these labels in graphiceq.dsp, bark_freq_grid.h,
// and the ShimmiZita editor. The LV2 port names only carry the symbols.
constexpr const char* kGraphicBands[] = {
    "Below 31.25 Hz", "62.5 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz",
    "2 kHz", "4 kHz", "8 kHz", "16 kHz", "Above 16 kHz"};
constexpr const char* kBarkBands[] = {
    "50 Hz", "150 Hz", "250 Hz", "350 Hz", "450 Hz", "570 Hz", "700 Hz", "840 Hz",
    "1 kHz", "1.17 kHz", "1.37 kHz", "1.6 kHz", "1.85 kHz", "2.15 kHz", "2.5 kHz",
    "2.9 kHz", "3.4 kHz", "4 kHz", "4.8 kHz", "5.8 kHz", "7 kHz", "8.5 kHz",
    "10.5 kHz", "13.5 kHz"};
constexpr const char* kShimmiFrequencies[] = {"Low Frequency", "High Frequency"};

constexpr auto kGuitarixUri = "http://guitarix.sourceforge.net/plugins/";

struct PortLabel {
  const char* symbol;
  const char* label;
};

juce::String numberedLabel(const juce::String& symbol, juce::String heading,
                           char first, char last,
                           std::span<const PortLabel> labels) {
  if (symbol.length() < 2) return {};
  const auto number = symbol[symbol.length() - 1];
  if (number < first || number > last) return {};
  const auto stem = symbol.dropLastCharacters(1);
  for (const auto& [port, label] : labels) {
    if (stem == port) return heading + symbol.getLastCharacters(1) + " " + label;
  }
  return {};
}

// These aliases are based on Guitarix 0.46's own editors, MOD labels and DSP.
// Match the URI, stable symbol and old name so newer descriptive metadata wins.
juce::String guitarixPortLabel(const juce::String& effect, const juce::String& symbol,
                              const juce::String& name) {
  if (effect == "gx_studiopre_st#studiopre_st") {
    constexpr PortLabel controls[] = {{"bright", "Bright"}, {"volume", "Volume"},
                                     {"bass", "Bass"}, {"middle", "Middle"},
                                     {"treble", "Treble"}, {"master", "Master"}};
    const bool right = symbol.endsWith("_r");
    if (!right && !symbol.endsWith("_l")) return {};
    // The old TTL gives both Master ports the left-channel name.
    if (!name.equalsIgnoreCase(symbol) && !(symbol == "master_r" && name.equalsIgnoreCase("master_l")))
      return {};
    const auto stem = symbol.dropLastCharacters(2);
    for (const auto& [port, label] : controls) {
      if (stem == port) return juce::String(label) + (right ? " (Right)" : " (Left)");
    }
    return {};
  }

  if (effect == "gx_mbcompressor_#_mbcompressor_" || effect == "gx_mbdelay_#_mbdelay_" ||
      effect == "gx_mbdistortion_#_mbdistortion_" || effect == "gx_mbecho_#_mbecho_" ||
      effect == "gx_mbreverb_#_mbreverb_") {
    if (symbol == "CROSSOVER_B1_B2" && (name == symbol || name == "LOW SHELF"))
      return "Crossover 1/2";
    if (name != symbol) return {};
    constexpr PortLabel crossovers[] = {{"CROSSOVER_B2_B3", "Crossover 2/3"},
                                       {"CROSSOVER_B3_B4", "Crossover 3/4"},
                                       {"CROSSOVER_B4_B5", "Crossover 4/5"}};
    for (const auto& [port, label] : crossovers) {
      if (symbol == port) return label;
    }
    constexpr PortLabel bands[] = {
        {"ATTACK", "Attack"}, {"DAMP", "Damping"}, {"DELAY", "Delay"},
        {"DRIVE", "Drive"}, {"FEEDBACK", "Feedback"}, {"GAIN", "Level"},
        {"MAKEUP", "Makeup Gain"}, {"MAKEUPTHRESHOLD", "Threshold Correction"},
        {"MODE", "Mode"}, {"OFFSET", "Offset"}, {"PERCENT", "Feedback"},
        {"RATIO", "Ratio"}, {"RELEASE", "Release"}, {"ROOMSIZE", "Room Size"},
        {"TIME", "Tempo"}, {"WET_DRY", "Dry/Wet"}};
    return numberedLabel(symbol, "Band ", '1', '5', bands);
  }

  if (effect == "gx_livelooper_#_livelooper_" && name == symbol) {
    constexpr PortLabel tracks[] = {{"clip", "Cut"}, {"clips", "Clip"}, {"speed", "Speed"},
                                   {"level", "Level"}, {"play", "Play"}, {"rec", "Record"},
                                   {"reset", "Reset"}};
    return numberedLabel(symbol, "Track ", '1', '4', tracks);
  }

  if (symbol == "WET_DRY" && name == symbol) return "Dry/Wet";

  struct EffectLabels {
    const char* effect;
    std::span<const PortLabel> ports;
  };
  static constexpr PortLabel digitalDelay[] = {
      {"HOWPASS", "Low Pass"}, {"NOTES", "Note Subdivision"}, {"SYNC", "Tempo Sync"}};
  static constexpr PortLabel duckDelay[] = {{"RELESE", "Release"}, {"PINGPONG", "Ping-Pong"}};
  static constexpr PortLabel detune[] = {{"MIDDLELOW", "Low Mid"}, {"MIDDLETREBLE", "High Mid"}};
  static constexpr PortLabel octave[] = {{"OCTAVE1", "Octave 1"}, {"OCTAVE2", "Octave 2"}};
  static constexpr PortLabel tremolo[] = {{"FREQ0", "Rate 1"}, {"FREQ1", "Rate 2"},
                                         {"FREQ2", "Rate 3"}, {"FREQ3", "Rate 4"},
                                         {"SWITCHFREQ", "Switch Rate"}};
  static constexpr PortLabel room[] = {{"PREDELAYMS", "Pre-delay"}, {"ROOMSIZE", "Room Size"},
                                      {"DRYWET", "Dry/Wet"}};
  static constexpr PortLabel zita[] = {
      {"EQ1_FREQ", "EQ 1 Frequency"}, {"EQ2_FREQ", "EQ 2 Frequency"},
      {"EQ1_LEVEL", "EQ 1 Level"}, {"EQ2_LEVEL", "EQ 2 Level"}, {"IN_DELAY", "Input Delay"},
      {"LOW_RT60", "Low Decay"}, {"MID_RT60", "Mid Decay"}, {"LF_X", "Low Crossover"},
      {"HF_DAMPING", "High-Frequency Damping"}, {"DRY_WET_MIX", "Dry/Wet Mix"}};
  static constexpr PortLabel shimmer[] = {
      {"CONTROL", "Modulation Source"}, {"DEPTH", "Modulation Depth"}, {"DRYWET", "Dry/Wet"},
      {"ENVELOPE", "Envelope Time"}, {"PSDRYWET", "Pitch Mix"}, {"SHIFT", "Pitch Shift"},
      {"SPEED", "LFO Speed"}, {"T60DS", "Low Decay"}, {"T60M", "High Decay"}};
  static constexpr PortLabel metal[] = {{"PREGAIN", "Pre Gain"}, {"GAIN1", "Level"}};
  static constexpr PortLabel tape[] = {{"wowdepth", "Wow Depth"}, {"wowfreq", "Wow Frequency"}};
  static constexpr EffectLabels effects[] = {
      {"gx_digital_delay_#_digital_delay_", digitalDelay},
      {"gx_digital_delay_st_#_digital_delay_st_", digitalDelay},
      {"gx_duck_delay_#_duck_delay_", duckDelay}, {"gx_duck_delay_st_#_duck_delay_st_", duckDelay},
      {"gx_detune_#_detune_", detune}, {"gx_oc_2_#_oc_2_", octave},
      {"gx_switched_tremolo_#_switched_tremolo_", tremolo},
      {"gx_room_simulator_#_room_simulator_", room}, {"gx_zita_rev1_stereo#_zita_rev1_stereo", zita},
      {"gx_shimmizita_#_shimmizita_", shimmer}, {"gxmetal_amp#metal_amp", metal},
      {"gxmetal_head#metal_head", metal}, {"gxtape#tape", tape}, {"gxtape_st#tape", tape}};

  if (effect == "gxtilttone#tilttone" && symbol == "body" && name == "Bpdy") return "Body";
  if (!name.equalsIgnoreCase(symbol)) return {};
  for (const auto& [plugin, ports] : effects) {
    if (effect != plugin) continue;
    for (const auto& [port, label] : ports) {
      if (symbol == port) return label;
    }
    break;
  }
  return {};
}

void labelGuitarixParameters(const juce::String& uri, juce::var& parameters) {
  if (!uri.startsWith(kGuitarixUri)) return;
  const auto effect = uri.fromFirstOccurrenceOf(kGuitarixUri, false, false);
  std::span<const char* const> labels;
  const char* prefix = "G";
  const char* unit = "dB";
  if (effect == "gx_graphiceq_#_graphiceq_") {
    labels = kGraphicBands;
  } else if (effect == "gx_barkgraphiceq_#_barkgraphiceq_") {
    labels = kBarkBands;
  } else if (effect == "gx_shimmizita_#_shimmizita_") {
    labels = kShimmiFrequencies;
    prefix = "F";
    unit = "Hz";
  }

  if (auto* rows = parameters.getArray()) {
    for (auto& row : *rows) {
      auto* control = row.getDynamicObject();
      const auto id = row["id"].toString();
      if (control == nullptr || !id.startsWith("sym:")) continue;
      const auto symbol = id.substring(4);
      const auto name = row["name"].toString();
      const int index = labels.empty() ? -1 : name.substring(1).getIntValue() - 1;
      if (index >= 0 && static_cast<size_t>(index) < labels.size() &&
          name == prefix + juce::String(index + 1) && symbol == name) {
        control->setProperty("name", labels[static_cast<size_t>(index)]);
        if (row["unit"].toString().isEmpty()) control->setProperty("unit", unit);
      } else if (const auto label = guitarixPortLabel(effect, symbol, name); label.isNotEmpty()) {
        control->setProperty("name", label);
      }
    }
  }
}

}  // namespace

Lv2Effect::Lv2Effect(std::unique_ptr<juce::AudioPluginInstance> primary,
                     std::unique_ptr<juce::AudioPluginInstance> second)
    : plugin(std::move(primary)), right(std::move(second)) {
  midi.ensureSize(2048);
  for (int i = 0; i < plugin->getParameters().size(); ++i) {
    if (auto* param = plugin->getHostedParameter(i); param != nullptr && !param->isMetaParameter())
      ports.emplace(param->getParameterID(), i);
  }
}

Lv2Effect::~Lv2Effect() = default;

bool Lv2Effect::hasParameter(const juce::String& id) const {
  return ports.count(id) != 0;
}

void Lv2Effect::setParameter(const juce::String& id, float value) {
  if (const auto it = ports.find(id); it != ports.end()) {
    plugin->getParameters()[it->second]->setValue(value);
    if (right)
      right->getParameters()[it->second]->setValue(value);
  }
}

void Lv2Effect::applySettings(const Settings& settings) {
  for (const auto& [id, index] : ports) {
    const auto value = settings.find(id);
    setParameter(id, value != settings.end() ? value->second
                                             : plugin->getParameters()[index]->getDefaultValue());
  }
}

juce::var Lv2Effect::parameters() const {
  juce::Array<juce::var> result;
  for (int index = 0; index < plugin->getParameters().size(); ++index) {
    const auto* p = plugin->getHostedParameter(index);
    if (p == nullptr || !hasParameter(p->getParameterID()))
      continue;
    auto row = std::make_unique<juce::DynamicObject>();
    const auto value = p->getValue();
    row->setProperty("id", p->getParameterID());
    row->setProperty("name", p->getName(128));
    row->setProperty("value", value);
    row->setProperty("text", p->getText(value, 32));
    row->setProperty("unit", p->getLabel());
    row->setProperty("min", p->getText(0.0f, 32).getDoubleValue());
    row->setProperty("max", p->getText(1.0f, 32).getDoubleValue());
    row->setProperty("toggle", p->isBoolean());
    row->setProperty("steps", p->isDiscrete() ? p->getNumSteps() : 0);

    juce::Array<juce::var> options;
    if (!p->isBoolean()) {
      for (const auto& label : p->getAllValueStrings()) {
        auto option = std::make_unique<juce::DynamicObject>();
        option->setProperty("label", label);
        option->setProperty("value", p->getValueForText(label));
        options.add(juce::var(option.release()));
      }
    }
    row->setProperty("options", options);
    result.add(juce::var(row.release()));
  }
  return result;
}

bool Lv2Effect::createsStereo() const {
  return plugin->getTotalNumOutputChannels() == 2;
}

void Lv2Effect::run(juce::AudioPluginInstance& instance, int channels, int frames) {
  juce::AudioBuffer<float> view(scratch.getArrayOfWritePointers(), channels, frames);
  midi.clear();
  instance.processBlock(view, midi);
}

void Lv2Effect::process(float* const* audio, int channels, int frames, bool dualMono) {
  const int inputs = plugin->getTotalNumInputChannels();
  const int outputs = plugin->getTotalNumOutputChannels();
  for (int offset = 0; offset < frames; offset += maxFrames) {
    const int size = std::min(frames - offset, maxFrames);
    if (channels == 2 && right && (dualMono || inputs == 1)) {
      for (int ch = 0; ch < 2; ++ch) {
        scratch.copyFrom(0, 0, audio[ch] + offset, size);
        if (inputs == 2)
          scratch.copyFrom(1, 0, scratch, 0, 0, size);
        else if (outputs == 2)
          scratch.clear(1, 0, size);
        run(ch == 0 ? *plugin : *right, std::max(inputs, outputs), size);
        if (outputs == 2) {
          scratch.applyGain(0, 0, size, 0.5f);
          scratch.addFrom(0, 0, scratch, 1, 0, size, 0.5f);
        }
        juce::FloatVectorOperations::copy(audio[ch] + offset, scratch.getReadPointer(0), size);
      }
      continue;
    }

    scratch.copyFrom(0, 0, audio[0] + offset, size);
    if (inputs == 2)
      scratch.copyFrom(1, 0, audio[channels - 1] + offset, size);
    else if (outputs == 2) {
      if (channels == 2) {
        scratch.applyGain(0, 0, size, 0.5f);
        scratch.addFrom(0, 0, audio[1] + offset, size, 0.5f);
      }
      scratch.clear(1, 0, size);
    }

    run(*plugin, std::max(inputs, outputs), size);
    if (channels == 1 && outputs == 2) {
      scratch.applyGain(0, 0, size, 0.5f);
      scratch.addFrom(0, 0, scratch, 1, 0, size, 0.5f);
    }
    for (int ch = 0; ch < channels; ++ch)
      juce::FloatVectorOperations::copy(audio[ch] + offset,
                                        scratch.getReadPointer(std::min(ch, outputs - 1)), size);
  }
}

bool TONE3000Processor::supportsLv2() const {
#if T3K_ENABLE_LV2_HOST
  return isStandalone();
#else
  return false;
#endif
}

#if T3K_ENABLE_LV2_HOST
namespace {
bool isSupported(const juce::PluginDescription& desc) {
  return !desc.isInstrument && desc.numInputChannels >= 1 && desc.numInputChannels <= 2
         && desc.numOutputChannels >= 1 && desc.numOutputChannels <= 2;
}
}
#endif

void TONE3000Processor::scanLv2Effects(Lv2Effect::ScanCallback callback) {
#if T3K_ENABLE_LV2_HOST
  if (supportsLv2()) {
    loadingThreadPool.addJob(std::function<void()>([callback = std::move(callback)] {
      juce::LV2PluginFormatHeadless format;
      std::map<juce::String, juce::String> names;
      auto bundles = format.searchPathsForPlugins({}, false);
      bundles.removeDuplicates(false);
      for (const auto& bundle : bundles) {
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile(descriptions, bundle);
        for (const auto* desc : descriptions)
          if (isSupported(*desc))
            names.emplace(desc->fileOrIdentifier, desc->name);
      }
      juce::Array<juce::var> effects;
      for (const auto& [uri, name] : names) {
        auto item = std::make_unique<juce::DynamicObject>();
        item->setProperty("uri", uri);
        item->setProperty("name", name);
        effects.add(juce::var(item.release()));
      }
      juce::MessageManager::callAsync([callback, effects] { callback(juce::var(effects)); });
    }));
    return;
  }
#endif
  callback({});
}

std::string TONE3000Processor::addLv2Effect(const std::string& insertId, const juce::String& uri,
                                           const juce::String& name) {
  if (!supportsLv2() || uri.isEmpty())
    return {};

  juce::ScopedLock lock(chainMutex);
  for (auto& laneBlocks : lanes) {
    for (auto& slot : laneBlocks) {
      if (slot->id != insertId || slot->type != ChainBlockType::INSERT)
        continue;
      pushChainHistory();
      auto effect = std::make_unique<ChainBlock>(juce::Uuid().toString().toStdString(),
                                                  ChainBlockType::LV2);
      effect->lv2Uri = uri;
      effect->lv2Name = name;
      prepareBlockForChainRate(*effect);
      const auto id = effect->id;
      slot = std::move(effect);
      queueLv2Load(*slot);
      alignBranchLaneLengths();
      bumpChainRevision();
      return id;
    }
  }
  return {};
}

bool TONE3000Processor::swapLv2Effect(const std::string& blockId, const juce::String& uri,
                                    Lv2Effect::LoadCallback finished) {
  if (!supportsLv2() || uri.isEmpty()) return false;

  juce::ScopedLock lock(chainMutex);
  auto* block = findBlockById(blockId);
  if (block == nullptr || block->type != ChainBlockType::LV2 || block->modelLoading) return false;
  if (block->lv2Uri == uri && block->loaded) return true;
  queueLv2Load(*block, uri, std::move(finished));
  bumpChainRevision();
  return true;
}

void TONE3000Processor::queueLv2Load(ChainBlock& block, const juce::String& replacementUri,
                                     Lv2Effect::LoadCallback finished) {
  const bool swapping = replacementUri.isNotEmpty();
  if (!swapping) {
    block.loaded = false;
    block.modelLoading = false;
    block.loadFailed = true;
    block.lv2Error = "LV2 hosting unavailable in this build";
  }
#if T3K_ENABLE_LV2_HOST
  if (!supportsLv2())
    return;

  block.modelLoading = true;
  if (!swapping) {
    block.loadFailed = false;
    block.lv2Error.clear();
  }
  const auto ticket = ++nextLv2Load;
  block.lv2Load = ticket;
  const auto uri = swapping ? replacementUri : block.lv2Uri;
  const auto id = block.id;
  std::weak_ptr<int> foregroundLifetime = lv2Lifetime;
  loadingThreadPool.addJob(std::function<void()>([this, foregroundLifetime, uri, id, ticket, swapping,
                                                       finished = std::move(finished)] {
    auto format = std::make_shared<juce::LV2PluginFormatHeadless>();
    juce::OwnedArray<juce::PluginDescription> descriptions;
    // Presets can request installed plugin URIs only. Never interpret them as bundle paths.
    if (!juce::File::isAbsolutePath(uri) && format->fileMightContainThisPluginType(uri))
      format->findAllTypesForFile(descriptions, uri);
    const auto description = descriptions.isEmpty() ? juce::PluginDescription() : *descriptions[0];

    // JUCE's plugin creation API runs on the message thread. Discovery runs in the pool
    // and neither creation nor preparation holds the audio chain lock.
    juce::MessageManager::callAsync([this, foregroundLifetime, format, description, id, ticket, uri, swapping, finished] {
      if (foregroundLifetime.expired())
        return;
      {
        juce::ScopedLock lock(chainMutex);
        auto* target = findBlockById(id);
        if (!target || target->type != ChainBlockType::LV2 || target->lv2Load != ticket)
          return;
      }

      juce::String error;
      std::shared_ptr<Lv2Effect> effect;
      if (description.fileOrIdentifier.isEmpty()) {
        error = "Effect is missing";
      } else if (!isSupported(description)) {
        error = "Effect needs a mono or stereo audio input and output";
      } else {
        auto primary = format->createInstanceFromDescription(description, kChainBaseSampleRate,
                                                              Lv2Effect::maxFrames, error);
        auto second = primary ? format->createInstanceFromDescription(description, kChainBaseSampleRate,
                                                                       Lv2Effect::maxFrames, error)
                              : nullptr;
        if (primary && second) {
          primary->prepareToPlay(kChainBaseSampleRate, Lv2Effect::maxFrames);
          second->prepareToPlay(kChainBaseSampleRate, Lv2Effect::maxFrames);
          effect = std::make_shared<Lv2Effect>(std::move(primary), std::move(second));
        }
      }

      if (effect)
        error.clear();
      else if (error.isEmpty())
        error = "Couldn't open effect";
      std::shared_ptr<Lv2Effect> retired;
      {
        juce::ScopedLock lock(chainMutex);
        auto* target = findBlockById(id);
        if (!target || target->type != ChainBlockType::LV2 || target->lv2Load != ticket)
          return;
        target->modelLoading = false;
        if (effect) {
          if (swapping) {
            // Commit only a working replacement. Undo captures the old URI and settings.
            pushChainHistory();
            target->lv2Uri = uri;
            target->lv2Settings.clear();
          }
          effect->applySettings(target->lv2Settings);
          target->lv2Name = description.name;
          target->loaded = true;
          target->loadFailed = false;
          target->lv2Error.clear();
          target->wetFadeGain.setCurrentAndTargetValue(0.0f);
          retired = std::exchange(target->lv2Effect, std::move(effect));
        } else if (!swapping) {
          target->loaded = false;
          target->loadFailed = true;
          target->lv2Error = error;
        }
        bumpChainRevision();
      }
      if (finished) finished(error);
    });
  }));
#else
  juce::ignoreUnused(finished);
#endif
}

juce::var TONE3000Processor::getLv2Parameters(const std::string& blockId) {
  std::shared_ptr<Lv2Effect> effect;
  juce::String uri;
  {
    juce::ScopedLock lock(chainMutex);
    if (const auto* block = findBlockById(blockId)) {
      effect = block->lv2Effect;
      uri = block->lv2Uri;
    }
  }
  if (!effect) return {};
  auto parameters = effect->parameters();
  labelGuitarixParameters(uri, parameters);
  return parameters;
}
