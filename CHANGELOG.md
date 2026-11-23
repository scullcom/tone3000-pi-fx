# Changelog

Notable changes to the TONE3000 FX Raspberry Pi desktop releases.

## [1.1.0] - 2026-11-23

### Added

- Create and rename preset banks, move presets within or between banks, and make independent copies.
- Each bank has its own MIDI Program Change slots starting at PC 0. Bank navigation is available in the preset picker and through the **Next Bank** and **Previous Bank** MIDI CC mappings.
- Changing banks leaves the current rig playing. The newly selected bank appears red until a preset from it is loaded.
- **Save New Preset** saves the current rig in the selected bank, including an empty bank. It checks for an existing preset with the same name.
- Deleting a bank requires confirmation and removes the presets in that bank.

### Compatibility

- Existing preset files and their order are available in **Your Presets**. The built-in **TONE3000** presets remain browsable. The original MIDI Program Change order applies while no named banks are configured.

## [1.0.0] - 2026-11-23

### Added

- First tagged Raspberry Pi 5 desktop release for 64-bit Raspberry Pi OS Trixie, with an installer and a **Sound & Video** menu entry.
- Native ARM64 LV2 hosting alongside NAM captures and cabinet IRs. Adjust, bypass, rearrange, or swap effects and save the whole rig as a preset.
- Search installed effects and keep a list of Favourites. Install Guitarix effects and matching pedal artwork, with the option to supply custom images.
- TONE3000 catalogue sign-in and the offline **Clean Trem and Verb** factory demo.

[1.1.0]: https://github.com/scullcom/tone3000-pi-fx/releases/tag/v1.1.0
[1.0.0]: https://github.com/scullcom/tone3000-pi-fx/releases/tag/v1.0.0
