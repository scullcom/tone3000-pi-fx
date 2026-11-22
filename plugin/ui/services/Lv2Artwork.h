#pragma once

#include <juce_graphics/juce_graphics.h>

#include "UiPrefs.h"

namespace t3k::ui::lv2Artwork {

juce::String preferenceKey(const juce::String& uri);
juce::Image load(const UiPrefs& prefs, const juce::String& uri);
bool hasCustom(const UiPrefs& prefs, const juce::String& uri);
juce::Result setCustom(UiPrefs& prefs, const juce::String& uri, const juce::File& source);
juce::Result removeCustom(UiPrefs& prefs, const juce::String& uri);

}  // namespace t3k::ui::lv2Artwork
