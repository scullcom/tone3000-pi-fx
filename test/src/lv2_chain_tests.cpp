#include "chain_test_helpers.h"

#include <optional>

namespace {

constexpr int blockSize = 512;

struct PresetDirectory {
  juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("t3k-lv2-test-" + juce::Uuid().toString());
  ~PresetDirectory() { dir.deleteRecursively(); }
};

std::unique_ptr<ChainTestProcessor> makeProcessor(const juce::File& presets) {
  juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
  auto proc = std::make_unique<ChainTestProcessor>();
  juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
  proc->setPresetStoreForTesting(presets);
  proc->setPlayConfigDetails(2, 2, kFs, blockSize);
  proc->prepareToPlay(kFs, blockSize);
  return proc;
}

juce::ValueTree makeRig() {
  juce::ValueTree snapshot("ChainSnapshot");
  juce::ValueTree blocks("ChainBlocks");
  blocks.appendChild(makeNamBlockTree("amp", 1, 100), nullptr);
  blocks.appendChild(makeIrBlockTree("cab", 2, 200), nullptr);
  snapshot.appendChild(blocks, nullptr);
  return snapshot;
}

juce::var rowFor(TONE3000Processor& processor, const juce::String& id) {
  const auto state = processor.getChainState(-1);
  for (const auto& row : *state["chain"].getArray())
    if (row["blockId"].toString() == id)
      return row;
  return {};
}

std::string emptySlot(TONE3000Processor& processor) {
  const auto state = processor.getChainState(-1);
  for (const auto& row : *state["chain"].getArray())
    if (row["kind"].toString() == "insert")
      return row["blockId"].toString().toStdString();
  return {};
}

template <typename Predicate>
bool waitUntil(Predicate ready) {
  for (int i = 0; i < 500; ++i) {
    if (ready()) return true;
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  }
  return false;
}

TEST(Lv2ChainTest, MissingEffectPassesAudioAndRoundTripsPreset) {
  PresetDirectory presets;
  auto proc = makeProcessor(presets.dir);
  auto rig = makeRig();
  proc->restoreFromTree(rig);
  ASSERT_TRUE(waitForChainLoaded(*proc));

  const auto input = makeNoise(140 * blockSize, 718, 0.1f);
  const auto expected = processStereo(*proc, input);

  juce::ValueTree lost("ChainBlock");
  lost.setProperty("type", "lv2", nullptr);
  lost.setProperty("id", "lost-effect", nullptr);
  lost.setProperty("lv2Uri", "urn:tone3000:test:absent-lv2", nullptr);
  lost.setProperty("lv2Name", "Unavailable Test Delay", nullptr);
  juce::ValueTree parameters("Lv2Parameters");
  juce::ValueTree feedback("Param");
  feedback.setProperty("id", "sym:feedback", nullptr);
  feedback.setProperty("value", 0.625f, nullptr);
  parameters.appendChild(feedback, nullptr);
  lost.appendChild(parameters, nullptr);
  rig.getChildWithName("ChainBlocks").appendChild(lost, nullptr);

  auto missing = makeProcessor(presets.dir);
  missing->restoreFromTree(rig);
  ASSERT_TRUE(waitUntil([&] {
    return static_cast<bool>(rowFor(*missing, "lost-effect")["loadFailed"]);
  }));
  ASSERT_TRUE(waitForChainLoaded(*missing));
  const auto missingRow = rowFor(*missing, "lost-effect");
  EXPECT_EQ(missingRow["lv2Name"].toString(), "Unavailable Test Delay");
  EXPECT_EQ(missingRow["kind"].toString(), "lv2");
  EXPECT_FALSE(static_cast<bool>(missingRow["loaded"]));
  const auto actual = processStereo(*missing, input);
  EXPECT_LE(settledMaxChannelDiff(expected.first, actual.first), 1e-6f);
  EXPECT_LE(settledMaxChannelDiff(expected.second, actual.second), 1e-6f);

  const auto presetId = missing->savePreset("LV2 missing effect")["id"].toString();
  ASSERT_TRUE(presetId.isNotEmpty());
  missing.reset();
  auto reopened = makeProcessor(presets.dir);
  ASSERT_TRUE(reopened->loadPreset(presetId));
  ASSERT_TRUE(waitUntil([&] {
    return static_cast<bool>(rowFor(*reopened, "lost-effect")["loadFailed"]);
  }));
  const auto roundTrip = reopened->savePreset("Round trip")["id"].toString();
  PresetManager manager(presets.dir);
  const auto persisted = manager.load(roundTrip).getChildWithName("ChainSnapshot")
                             .getChildWithName("ChainBlocks")
                             .getChildWithProperty("id", "lost-effect");
  EXPECT_EQ(persisted["lv2Uri"].toString(), "urn:tone3000:test:absent-lv2");
  const auto value = persisted.getChildWithName("Lv2Parameters")
                         .getChildWithProperty("id", "sym:feedback")["value"];
  EXPECT_FLOAT_EQ(static_cast<float>(value), 0.625f);
}

TEST(Lv2ChainTest, GuitarixFrequencyLabelsKeepTheirPortIds) {
  PresetDirectory presets;
  auto proc = makeProcessor(presets.dir);
  if (!proc->supportsLv2()) GTEST_SKIP() << "Requires a Linux LV2 host build";

  const auto effects = std::make_shared<std::optional<juce::var>>();
  proc->scanLv2Effects([effects](juce::var found) { effects->emplace(std::move(found)); });
  ASSERT_TRUE(waitUntil([&] { return effects->has_value(); }));
  const auto* installed = effects->value().getArray();
  ASSERT_NE(installed, nullptr);
  struct Control {
    const char* pluginId;
    const char* port;
    const char* label;
    const char* unit;
  };
  const Control cases[] = {
      {"gx_graphiceq_#_graphiceq_", "sym:G2", "62.5 Hz", "dB"},
      {"gx_barkgraphiceq_#_barkgraphiceq_", "sym:G24", "13.5 kHz", "dB"},
      {"gx_shimmizita_#_shimmizita_", "sym:F1", "Low Frequency", "Hz"},
  };
  int checked = 0;
  for (const auto& control : cases) {
    const juce::String uri = juce::String("http://guitarix.sourceforge.net/plugins/") + control.pluginId;
    const auto effect = std::find_if(installed->begin(), installed->end(), [&](const auto& row) {
      return row["uri"].toString() == uri;
    });
    if (effect == installed->end()) continue;
    const auto id = proc->addLv2Effect(emptySlot(*proc), uri, (*effect)["name"].toString());
    ASSERT_FALSE(id.empty());
    ASSERT_TRUE(waitUntil([&] { return !static_cast<bool>(rowFor(*proc, id)["modelLoading"]); }));
    const auto parameters = proc->getLv2Parameters(id);
    const auto* rows = parameters.getArray();
    ASSERT_NE(rows, nullptr);
    const auto port = std::find_if(rows->begin(), rows->end(), [&](const auto& row) {
      return row["id"].toString() == control.port;
    });
    ASSERT_NE(port, rows->end());
    EXPECT_EQ((*port)["name"].toString(), control.label);
    EXPECT_EQ((*port)["unit"].toString(), control.unit);
    ++checked;
  }
  if (checked == 0) GTEST_SKIP() << "Requires at least one of the Guitarix effects under test";
}

TEST(Lv2ChainTest, NativeDelayAndReverbRestoreSettingsAndProduceStereo) {
  PresetDirectory presets;
  auto proc = makeProcessor(presets.dir);
  if (!proc->supportsLv2()) GTEST_SKIP() << "Requires a Linux LV2 host build";

  const auto effects = std::make_shared<std::optional<juce::var>>();
  proc->scanLv2Effects([effects](juce::var found) { effects->emplace(std::move(found)); });
  ASSERT_TRUE(waitUntil([&] { return effects->has_value(); }));
  const auto find = [&](const juce::String& fragment) -> juce::var {
    if (const auto* entries = effects->value().getArray())
      for (const auto& entry : *entries)
        if (entry["uri"].toString().containsIgnoreCase(fragment) ||
            entry["name"].toString().containsIgnoreCase(fragment))
          return entry;
    return {};
  };
  const auto delay = find("art_delay_stereo");
  const auto reverb = find("dragonfly hall");
  if (delay.isVoid() || reverb.isVoid())
    GTEST_SKIP() << "Requires Dragonfly Hall Reverb and LSP Artistic Delay Stereo";

  proc->restoreFromTree(makeRig());
  ASSERT_TRUE(waitForChainLoaded(*proc));
  const auto add = [&](const juce::var& effect) {
    return proc->addLv2Effect(emptySlot(*proc), effect["uri"].toString(),
                              effect["name"].toString());
  };
  const auto delayId = add(delay);
  const auto reverbId = add(reverb);
  ASSERT_FALSE(delayId.empty());
  ASSERT_FALSE(reverbId.empty());
  ASSERT_TRUE(waitUntil([&] {
    return !static_cast<bool>(rowFor(*proc, delayId)["modelLoading"]) &&
           !static_cast<bool>(rowFor(*proc, reverbId)["modelLoading"]);
  }));
  ASSERT_TRUE(static_cast<bool>(rowFor(*proc, delayId)["loaded"]))
      << rowFor(*proc, delayId)["lv2Error"].toString().toStdString();
  ASSERT_TRUE(static_cast<bool>(rowFor(*proc, reverbId)["loaded"]))
      << rowFor(*proc, reverbId)["lv2Error"].toString().toStdString();

  const auto state = proc->getChainState(-1);
  std::vector<std::string> order;
  for (const auto& row : *state["chain"].getArray())
    order.push_back(row["blockId"].toString().toStdString());
  ASSERT_GE(order.size(), 4u);
  ASSERT_EQ(order[2], delayId);
  ASSERT_EQ(order[3], reverbId);
  std::swap(order[2], order[3]);
  ASSERT_TRUE(proc->reorderChainBlocks(order));
  ASSERT_TRUE(proc->undoChain());

  const auto delayControls = proc->getLv2Parameters(delayId);
  const auto* delayRows = delayControls.getArray();
  ASSERT_NE(delayRows, nullptr);
  const auto feedback = std::find_if(delayRows->begin(), delayRows->end(), [](const auto& row) {
    return row["name"].toString().containsIgnoreCase("feedback") &&
           !static_cast<bool>(row["toggle"]) && static_cast<int>(row["steps"]) == 0;
  });
  ASSERT_NE(feedback, delayRows->end());
  const auto feedbackId = (*feedback)["id"].toString();
  ASSERT_TRUE(proc->setBlockParam(delayId, "lv2:" + feedbackId, 0.125));

  const auto controls = proc->getLv2Parameters(reverbId);
  const auto* rows = controls.getArray();
  ASSERT_NE(rows, nullptr);
  juce::String decay;
  for (const auto& row : *rows)
    if (row["name"].toString().containsIgnoreCase("decay")) {
      decay = row["id"].toString();
      break;
    }
  ASSERT_TRUE(decay.isNotEmpty());
  ASSERT_TRUE(proc->setBlockParam(reverbId, "lv2:" + decay, 0.625));

  const auto input = makeNoise(280 * blockSize, 730, 0.1f);
  const auto stereo = processStereo(*proc, input);
  EXPECT_GT(settledMaxChannelDiff(stereo.first, stereo.second), 1e-4f);
  ASSERT_TRUE(proc->setBlockParam(reverbId, "enabled", 0.0));
  ASSERT_TRUE(proc->setBlockParam(delayId, "enabled", 0.0));
  const auto bypassed = processStereo(*proc, input);
  EXPECT_LE(settledMaxChannelDiff(bypassed.first, bypassed.second), 1e-6f);
  ASSERT_TRUE(proc->setBlockParam(reverbId, "enabled", 1.0));
  ASSERT_TRUE(proc->setBlockParam(delayId, "enabled", 1.0));

  const auto presetId = proc->savePreset("Amp Cab Delay Reverb")["id"].toString();
  ASSERT_TRUE(presetId.isNotEmpty());
  proc.reset();
  auto reopened = makeProcessor(presets.dir);
  ASSERT_TRUE(reopened->loadPreset(presetId));
  ASSERT_TRUE(waitUntil([&] { return static_cast<bool>(rowFor(*reopened, reverbId)["loaded"]) &&
                                            static_cast<bool>(rowFor(*reopened, delayId)["loaded"]); }));
  const auto restored = reopened->getLv2Parameters(reverbId);
  const auto* restoredRows = restored.getArray();
  ASSERT_NE(restoredRows, nullptr);
  const auto restoredDecay = std::find_if(restoredRows->begin(), restoredRows->end(),
                                          [&](auto row) { return row["id"].toString() == decay; });
  ASSERT_NE(restoredDecay, restoredRows->end());
  EXPECT_NEAR(static_cast<double>((*restoredDecay)["value"]), 0.625, 1e-5);

  const auto restoredDelay = reopened->getLv2Parameters(delayId);
  const auto* restoredDelayRows = restoredDelay.getArray();
  ASSERT_NE(restoredDelayRows, nullptr);
  const auto restoredFeedback = std::find_if(
      restoredDelayRows->begin(), restoredDelayRows->end(),
      [&](const auto& row) { return row["id"].toString() == feedbackId; });
  ASSERT_NE(restoredFeedback, restoredDelayRows->end());
  EXPECT_NEAR(static_cast<double>((*restoredFeedback)["value"]), 0.125, 1e-5);

  ASSERT_TRUE(reopened->setBlockParam(delayId, "enabled", 0.0));
  const auto originalUri = delay["uri"].toString();
  const auto replacementUri = reverb["uri"].toString();
  auto error = std::make_shared<juce::String>();
  ASSERT_TRUE(reopened->swapLv2Effect(delayId, "urn:tone3000:test:missing-replacement",
                                      [error](const juce::String& message) { *error = message; }));
  ASSERT_TRUE(waitUntil([&] { return error->isNotEmpty(); }));
  EXPECT_EQ(rowFor(*reopened, delayId)["lv2Uri"].toString(), originalUri);
  EXPECT_TRUE(static_cast<bool>(rowFor(*reopened, delayId)["loaded"]));

  ASSERT_TRUE(reopened->swapLv2Effect(delayId, replacementUri, {}));
  ASSERT_TRUE(waitUntil([&] { return rowFor(*reopened, delayId)["lv2Uri"].toString() == replacementUri; }));
  const auto swappedState = reopened->getChainState(-1);
  const auto& swappedOrder = *swappedState["chain"].getArray();
  ASSERT_GE(swappedOrder.size(), 4);
  EXPECT_EQ(swappedOrder[2]["blockId"].toString().toStdString(), delayId);
  EXPECT_EQ(swappedOrder[3]["blockId"].toString().toStdString(), reverbId);
  EXPECT_FALSE(static_cast<bool>(swappedOrder[2]["params"]["enabled"]));

  ASSERT_TRUE(reopened->undoChain());
  ASSERT_TRUE(waitUntil([&] { return static_cast<bool>(rowFor(*reopened, delayId)["loaded"]); }));
  EXPECT_EQ(rowFor(*reopened, delayId)["lv2Uri"].toString(), originalUri);
  const auto undone = reopened->getLv2Parameters(delayId);
  ASSERT_NE(undone.getArray(), nullptr);
  const auto feedbackAfterUndo = std::find_if(undone.getArray()->begin(), undone.getArray()->end(),
      [&](const auto& row) { return row["id"].toString() == feedbackId; });
  ASSERT_NE(feedbackAfterUndo, undone.getArray()->end());
  EXPECT_NEAR(static_cast<double>((*feedbackAfterUndo)["value"]), 0.125, 1e-5);
  ASSERT_TRUE(reopened->redoChain());
  ASSERT_TRUE(waitUntil([&] { return static_cast<bool>(rowFor(*reopened, delayId)["loaded"]); }));
  EXPECT_EQ(rowFor(*reopened, delayId)["lv2Uri"].toString(), replacementUri);
}

}  // namespace
