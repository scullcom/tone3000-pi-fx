#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <map>
#include <memory>

class Lv2Effect {
public:
  using Settings = std::map<juce::String, float>;
  using ScanCallback = std::function<void(juce::var)>;
  using LoadCallback = std::function<void(const juce::String& error)>;

  static constexpr int maxFrames = 512;

  Lv2Effect(std::unique_ptr<juce::AudioPluginInstance> primary,
            std::unique_ptr<juce::AudioPluginInstance> second);
  ~Lv2Effect();

  juce::var parameters() const;
  bool hasParameter(const juce::String& id) const;
  void applySettings(const Settings& settings);
  void setParameter(const juce::String& id, float value);
  void process(float* const* audio, int channels, int frames, bool dualMono);
  bool createsStereo() const;

private:
  std::unique_ptr<juce::AudioPluginInstance> plugin;
  std::unique_ptr<juce::AudioPluginInstance> right;
  juce::AudioBuffer<float> scratch{2, maxFrames};
  juce::MidiBuffer midi;
  std::map<juce::String, int> ports;

  void run(juce::AudioPluginInstance& instance, int channels, int frames);
};
