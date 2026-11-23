# Changelog

Notable changes to the TONE3000 FX Raspberry Pi releases.

## [1.3.0] - 2026-11-23

- **Export** and **Import** in the Settings and Login menu support selective USB backups of user presets and banks, including empty banks. Export writes to `/Tone3000-pi-fx/presets` and replaces the existing backup only after the new one has been written.
- Import merges banks by name and overwrites matching preset names within the destination bank. Names are matched case-insensitively. New banks and presets are added in backup order.
- The desktop installer includes UDisks. Custom kiosk images can use the included USB mount policy for the kiosk account.

## [1.2.0] - 2026-11-23

### Added

- **Mute Audio in Tuner** in System Settings silences the output while the tuner is open.
- LV2 block settings now provide **Switch** and **Delete**, with artwork controls beside the effect name.
- In kiosk mode, **Shut down Pi…** requests a graceful system shutdown with confirmation. The kiosk image saves the working session and audio settings before powering off.

### Changed

- Guitarix EQ controls show their verified frequencies. Stereo preamp, multiband, and other Guitarix controls use clearer labels where their meaning could be confirmed. Saved parameter IDs and values remain compatible.
- TONE3000 skips Gx Tape Stereo on ARM and excludes GxTuner and GxDetune. Saved rigs retain those blocks and their settings, and audio passes through their slots.

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

[1.3.0]: https://github.com/scullcom/tone3000-pi-fx/releases/tag/v1.3.0
[1.2.0]: https://github.com/scullcom/tone3000-pi-fx/releases/tag/v1.2.0
[1.1.0]: https://github.com/scullcom/tone3000-pi-fx/releases/tag/v1.1.0
[1.0.0]: https://github.com/scullcom/tone3000-pi-fx/releases/tag/v1.0.0
