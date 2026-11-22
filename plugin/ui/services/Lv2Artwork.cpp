#include "Lv2Artwork.h"

#include <juce_cryptography/juce_cryptography.h>

namespace t3k::ui::lv2Artwork {

namespace {
constexpr int kMaxTileImageSize = 512;
constexpr juce::int64 kMaxImportBytes = 16 * 1024 * 1024;

juce::String imageId(const juce::String& uri) {
  return juce::SHA256(uri.toRawUTF8(), uri.getNumBytesAsUTF8()).toHexString();
}

juce::File artworkFile(const UiPrefs& prefs, const juce::String& uri, bool custom) {
  const auto settings = prefs.settingsFile();
  if (settings == juce::File{} || uri.isEmpty()) return {};
  return settings.getSiblingFile("lv2-artwork")
      .getChildFile(custom ? "custom" : "guitarix")
      .getChildFile(imageId(uri) + ".png");
}
}  // namespace

juce::String preferenceKey(const juce::String& uri) {
  return "t3k.lv2Artwork." + imageId(uri);
}

juce::Image load(const UiPrefs& prefs, const juce::String& uri) {
  const auto custom = juce::ImageFileFormat::loadFrom(artworkFile(prefs, uri, true));
  return custom.isValid() ? custom
                          : juce::ImageFileFormat::loadFrom(artworkFile(prefs, uri, false));
}

bool hasCustom(const UiPrefs& prefs, const juce::String& uri) {
  return artworkFile(prefs, uri, true).existsAsFile();
}

juce::Result setCustom(UiPrefs& prefs, const juce::String& uri, const juce::File& source) {
  if (!source.existsAsFile() || source.getSize() > kMaxImportBytes)
    return juce::Result::fail("Choose a PNG or JPEG smaller than 16 MB");
  juce::FileInputStream input(source);
  if (!input.openedOk()) return juce::Result::fail("Could not open the image");
  auto* format = juce::ImageFileFormat::findImageFormatForStream(input);
  if (format == nullptr || (format->getFormatName() != "PNG" && format->getFormatName() != "JPEG"))
    return juce::Result::fail("Choose a PNG or JPEG image");
  input.setPosition(0);
  auto image = format->decodeImage(input);
  if (!image.isValid()) return juce::Result::fail("Could not read the image");

  const int longestSide = juce::jmax(image.getWidth(), image.getHeight());
  if (longestSide > kMaxTileImageSize) {
    const auto scale = static_cast<double>(kMaxTileImageSize) / longestSide;
    image = image.rescaled(juce::jmax(1, juce::roundToInt(image.getWidth() * scale)),
                           juce::jmax(1, juce::roundToInt(image.getHeight() * scale)));
  }
  const auto target = artworkFile(prefs, uri, true);
  if (target == juce::File{}) return juce::Result::fail("No local profile for effect images");
  if (const auto result = target.getParentDirectory().createDirectory(); result.failed())
    return result;
  const juce::TemporaryFile temporary(target);
  {
    juce::FileOutputStream output(temporary.getFile());
    if (!output.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, output))
      return juce::Result::fail("Could not save the effect image");
    output.flush();
    if (output.getStatus().failed()) return output.getStatus();
  }
  if (!temporary.overwriteTargetFileWithTemporary())
    return juce::Result::fail("Could not replace the effect image");
  prefs.set(preferenceKey(uri), juce::Uuid().toString());
  return juce::Result::ok();
}

juce::Result removeCustom(UiPrefs& prefs, const juce::String& uri) {
  const auto file = artworkFile(prefs, uri, true);
  if (file.existsAsFile() && !file.deleteFile())
    return juce::Result::fail("Could not remove the effect image");
  prefs.remove(preferenceKey(uri));
  return juce::Result::ok();
}

}  // namespace t3k::ui::lv2Artwork
