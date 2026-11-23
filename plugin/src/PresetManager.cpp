#include "PresetManager.h"
#include "AppData.h"
// For TONE3000Processor::ensureWritableDir: preset saves share the app-data
// folder whose permissions a sudo'd install script can mangle (github
// issue #76).
#include "Processor.h"
#include <algorithm>
#include <limits>

namespace presetfile = t3k::presetfile;

namespace {

constexpr const char* kUserPrefix = "user:";
constexpr const char* kFactoryPrefix = "factory:";

juce::File presetsRootDir() { return t3k::appdata::root().getChildFile("Presets"); }

juce::String stripPrefix(const juce::String& id, const char* prefix) {
  return id.fromFirstOccurrenceOf(prefix, false, false);
}

}  // namespace

PresetManager::PresetManager() : PresetManager(presetsRootDir(), defaultSystemFactoryDir()) {}

PresetManager::PresetManager(const juce::File& baseDir, const juce::File& systemFactory)
    : userDir(baseDir),
      factoryDir(baseDir.getChildFile("Factory")),
      systemFactoryDir(systemFactory) {}

PresetManager::PresetManager(const PresetManager& other)
    : userDir(other.userDir), factoryDir(other.factoryDir), systemFactoryDir(other.systemFactoryDir) {}

PresetManager& PresetManager::operator=(const PresetManager& other) {
  if (this == &other)
    return *this;
  const juce::ScopedLock lock(cacheLock);
  userDir = other.userDir;
  factoryDir = other.factoryDir;
  systemFactoryDir = other.systemFactoryDir;
  cache.clear();  // paths from the old root would only be pruned anyway
  return *this;
}

juce::File PresetManager::defaultSystemFactoryDir() {
  // Shared all-users location the installers write to. A missing dir just
  // means no shipped presets; scans treat it as empty.
#if JUCE_IOS
  // iOS has no installer and so no shared factory directory outside the app:
  // the same .t3kpreset files ride inside the app bundle (plugin/CMakeLists.txt
  // carries resources/factory-presets as bundle resources; iOS bundles are
  // flat, so they land at TONE3000.app/FactoryPresets). The bundle is
  // read-only, which is exactly the contract this directory already has; a
  // user Factory folder still overlays it in list(), as on macOS and Windows.
  // The AUv3 is an app extension inside that same bundle
  // (TONE3000.app/PlugIns/TONE3000.appex) and carries no presets of its own,
  // so from there the app is two levels up.
  juce::File bundle = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
  if (bundle.hasFileExtension("appex"))
    bundle = bundle.getParentDirectory().getParentDirectory();
  return bundle.getChildFile("FactoryPresets");
#elif JUCE_MAC
  return juce::File("/Library/Application Support/TONE3000/Presets/Factory");
#elif JUCE_WINDOWS
  // ProgramData; matches the Inno Setup {commonappdata} destination.
  return juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory)
      .getChildFile("TONE3000")
      .getChildFile("Presets")
      .getChildFile("Factory");
#elif JUCE_LINUX
  // The tarball installs per-user (into factoryDir); this path is the hook
  // for system-wide/distro packaging.
  return juce::File("/usr/share/TONE3000/Presets/Factory");
#else
  return {};
#endif
}

// Filenames

juce::File PresetManager::writeUserPreset(const juce::File& dir, const juce::File& current,
                                          const juce::String& name, const juce::ValueTree& preset) {
  const juce::File target = presetfile::uniqueFile(dir, presetfile::sanitizeStem(name), current);
  const bool haveCurrent = current != juce::File() && current.existsAsFile();

  if (haveCurrent && current == target) {
    // Same file: rewrite in place, then fix a case-only spelling change.
    if (!presetfile::write(current, preset))
      return {};
    if (current.getFileName() != target.getFileName() && !current.moveFileTo(target))
      return current;  // content is saved; the old spelling is cosmetic
    return target;
  }

  // New file, or the filename no longer matches the name (a rename, or a
  // legacy <uuid> file getting its readable name): write the new file
  // first, then drop the old one, so a failure never loses the preset.
  if (!presetfile::write(target, preset))
    return {};
  if (haveCurrent) {
    if (!current.deleteFile())
      juce::Logger::writeToLog("[Presets] Could not remove superseded file: " +
                               current.getFullPathName());
    else
      juce::Logger::writeToLog("[Presets] Renamed file " + current.getFileName() + " -> " +
                               target.getFileName());
  }
  return target;
}

// Scanning

std::vector<PresetManager::Entry> PresetManager::scanDir(const juce::File& dir, const char* prefix,
                                                         bool factory, ScanState& state) const {
  std::vector<Entry> out;
  if (!dir.isDirectory())
    return out;

  // Stem order, so which of two files claiming one id keeps it (below) does
  // not depend on the OS's directory iteration order: "Original" beats
  // "Original copy".
  auto files = dir.findChildFiles(juce::File::findFiles, false, "*" + juce::String(kFileExtension));
  std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) {
    return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension()) < 0;
  });

  std::set<juce::String> idsInDir;
  for (const auto& file : files) {
    // Same (mtime, size) as last time: the id/name are what we read then.
    // Every write goes through write-then-rename (presetfile::write), so any
    // change to a preset moves at least its mtime. A never-seen path gets a
    // default entry (mtime 0), which no real file matches.
    const juce::int64 modificationMs = file.getLastModificationTime().toMilliseconds();
    const juce::int64 size = file.getSize();
    const juce::String stem = file.getFileNameWithoutExtension();
    state.seen.insert(file.getFullPathName());
    auto& cached = cache[file.getFullPathName()];
    if (cached.modificationMs != modificationMs || cached.size != size) {
      ++state.parsed;
      // Header only for v2 files; a legacy v1 file is parsed in full (once).
      const presetfile::Header header = presetfile::readHeader(file);
      cached.modificationMs = modificationMs;
      cached.size = size;
      cached.valid = header.valid;
      if (cached.valid) {
        // Legacy files (and hand-renamed ones) carry no id: the stem is it.
        cached.rawId = header.id.isEmpty() ? stem : header.id;
        cached.name = header.name.isEmpty() ? stem : header.name;
      } else {
        cached.rawId.clear();
        cached.name.clear();
      }
    }
    if (!cached.valid)
      continue;

    // Two files claiming one id (a copied/imported file): the first keeps
    // it, the second falls back to its stem so both still list and load.
    juce::String id = prefix + cached.rawId;
    if (!idsInDir.insert(id).second) {
      id = prefix + stem;
      if (!idsInDir.insert(id).second)
        continue;  // stem taken too: nothing sane to call it
      juce::Logger::writeToLog("[Presets] Duplicate preset id in " + file.getFileName() +
                               "; listing it as " + id);
    }
    Entry entry;
    entry.info.id = id;
    entry.info.name = cached.name;
    entry.info.factory = factory;
    entry.file = file;
    out.push_back(std::move(entry));
  }
  std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
    return a.info.name.compareIgnoreCase(b.info.name) < 0;
  });
  return out;
}

std::vector<PresetManager::Entry> PresetManager::entries() const {
  const auto startMs = juce::Time::getMillisecondCounterHiRes();
  ScanState state;
  // One lock for the whole scan: concurrent callers (host program API off
  // the message thread) would otherwise each pay to parse the same new file.
  const juce::ScopedLock lock(cacheLock);

  // Factory section: system Factory with the user Factory overlaid on top (a
  // local file with the same id replaces the shipped one), re-sorted by name
  // so the merged section reads like a single folder.
  std::vector<Entry> factory = scanDir(systemFactoryDir, kFactoryPrefix, true, state);
  for (auto& local : scanDir(factoryDir, kFactoryPrefix, true, state)) {
    const auto it = std::find_if(factory.begin(), factory.end(), [&local](const Entry& shipped) {
      return shipped.info.id == local.info.id;
    });
    if (it != factory.end())
      *it = std::move(local);
    else
      factory.push_back(std::move(local));
  }
  std::sort(factory.begin(), factory.end(), [](const Entry& a, const Entry& b) {
    return a.info.name.compareIgnoreCase(b.info.name) < 0;
  });

  // User presets lead so they own the low MIDI program-change numbers; the
  // factory section follows.
  std::vector<Entry> presets = scanDir(userDir, kUserPrefix, false, state);
  presets.insert(presets.end(), std::make_move_iterator(factory.begin()),
                 std::make_move_iterator(factory.end()));

  // Forget files that are gone (remove(), or deleted behind our back).
  for (auto it = cache.begin(); it != cache.end();)
    it = state.seen.count(it->first) != 0 ? std::next(it) : cache.erase(it);

  // Apply the custom order: within each section, ordered ids first (in file
  // order), then everything else. The sort is stable over the name-sorted
  // scan above, so presets missing from the order file (new saves, ids from
  // another machine) stay alphabetical after the ordered block, and a
  // missing/empty order file leaves the classic ordering untouched.
  const juce::StringArray order = readOrder();
  if (!order.isEmpty()) {
    auto rank = [&order](const Entry& entry) {
      const int index = order.indexOf(entry.info.id);
      return index < 0 ? std::numeric_limits<int>::max() : index;
    };
    std::stable_sort(presets.begin(), presets.end(), [&](const Entry& a, const Entry& b) {
      if (a.info.factory != b.info.factory)
        return !a.info.factory;  // user section always first
      return rank(a) < rank(b);
    });
  }

  // Release-level line only when files were actually read and it was slow:
  // a warm scan never logs; a cold one on a slow disk (issue #169) says so
  // in the user's log next to whatever the host was doing at the time.
  const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - startMs;
  if (state.parsed > 0 && elapsedMs >= 100.0)
    juce::Logger::writeToLog("[Presets] Scanned " + juce::String(presets.size()) + " presets (" +
                             juce::String(state.parsed) + " read from disk) in " +
                             juce::String(juce::roundToInt(elapsedMs)) + " ms");
  return presets;
}

std::vector<PresetManager::Info> PresetManager::list() const {
  std::vector<Info> out;
  for (auto& entry : entries())
    out.push_back(std::move(entry.info));
  return out;
}

juce::File PresetManager::fileForId(const juce::String& id) const {
  if (id.isEmpty())
    return {};
  for (const auto& entry : entries())
    if (entry.info.id == id)
      return entry.file;
  return {};
}

// Order

bool PresetManager::move(const juce::String& id, int delta) const {
  if (delta == 0)
    return false;

  const std::vector<Info> presets = list();
  const auto it = std::find_if(presets.begin(), presets.end(),
                               [&id](const Info& info) { return info.id == id; });
  if (it == presets.end())
    return false;

  const int index = static_cast<int>(std::distance(presets.begin(), it));
  const bool factory = it->factory;

  int sectionStart = index;
  while (sectionStart > 0 && presets[static_cast<size_t>(sectionStart - 1)].factory == factory)
    --sectionStart;
  int sectionEnd = index + 1;
  while (sectionEnd < static_cast<int>(presets.size()) &&
         presets[static_cast<size_t>(sectionEnd)].factory == factory)
    ++sectionEnd;

  const int target = std::clamp(index + delta, sectionStart, sectionEnd - 1);
  if (target == index)
    return false;  // already at its section's edge (or a no-op clamp)

  juce::StringArray ids;
  for (const Info& info : presets)
    ids.add(info.id);
  const juce::String moving = ids[index];
  ids.remove(index);
  ids.insert(target, moving);
  return writeOrder(ids);
}

juce::File PresetManager::orderFile() const {
  // Beside the preset files (the *.t3kpreset scan never picks it up).
  return userDir.getChildFile("order.json");
}

juce::StringArray PresetManager::readOrder() const {
  juce::StringArray out;
  const auto parsed = juce::JSON::parse(orderFile().loadFileAsString());
  if (const auto* ids = parsed.getArray())
    for (const auto& id : *ids)
      out.add(id.toString());
  return out;
}

bool PresetManager::writeOrder(const juce::StringArray& ids) const {
  if (!userDir.createDirectory())
    return false;
  juce::Array<juce::var> list;
  for (const auto& id : ids)
    list.add(id);
  return orderFile().replaceWithText(juce::JSON::toString(juce::var(list)));
}

// CRUD

juce::ValueTree PresetManager::load(const juce::String& id) const {
  return presetfile::read(fileForId(id));
}

std::vector<PresetManager::Bank> PresetManager::banks() const {
  auto json = juce::JSON::parse(userDir.getChildFile("banks.json").loadFileAsString());
  std::vector<Bank> result{{{}, "Your Presets", {}}};
  juce::StringArray unassigned;
  for (const auto& preset : list())
    if (!preset.factory) unassigned.add(preset.id);

  if (const auto* records = json.getArray()) {
    juce::StringArray defaultOrder;
    for (const auto& record : *records) {
      Bank bank{record["id"].toString(), record["name"].toString(), {}};
      const auto members = record["presets"];
      if (bank.id.isEmpty()) {
        if (const auto* ids = members.getArray())
          for (const auto& id : *ids) defaultOrder.addIfNotAlreadyThere(id.toString());
        continue;
      }
      if (bank.name.isEmpty() || std::any_of(result.begin(), result.end(),
                                             [&](const auto& item) { return item.id == bank.id; }))
        continue;
      if (const auto* ids = members.getArray())
        for (const auto& id : *ids)
          if (unassigned.contains(id.toString())) {
            bank.presets.add(id.toString());
            unassigned.removeString(id.toString());
          }
      result.push_back(std::move(bank));
    }
    for (const auto& id : defaultOrder)
      if (unassigned.contains(id)) {
        result.front().presets.add(id);
        unassigned.removeString(id);
      }
  }
  result.front().presets.addArray(unassigned);
  return result;
}

bool PresetManager::writeBanks(const std::vector<Bank>& banks) const {
  if (!TONE3000Processor::ensureWritableDir(userDir)) return false;
  juce::Array<juce::var> records;
  for (const auto& bank : banks) {
    auto* record = new juce::DynamicObject();
    record->setProperty("id", bank.id);
    record->setProperty("name", bank.name);
    juce::Array<juce::var> ids;
    for (const auto& id : bank.presets) ids.add(id);
    record->setProperty("presets", ids);
    records.add(juce::var(record));
  }
  return userDir.getChildFile("banks.json").replaceWithText(juce::JSON::toString(records));
}

bool PresetManager::createBank(const juce::String& name) const {
  const juce::ScopedLock lock(cacheLock);
  auto records = banks();
  const auto trimmed = name.trim();
  if (trimmed.isEmpty()) return false;
  records.push_back({juce::Uuid().toString(), trimmed, {}});
  return writeBanks(records);
}

bool PresetManager::renameBank(const juce::String& id, const juce::String& name) const {
  const juce::ScopedLock lock(cacheLock);
  auto records = banks();
  const auto trimmed = name.trim();
  if (id.isEmpty() || trimmed.isEmpty()) return false;
  for (auto& bank : records) {
    if (bank.id != id) continue;
    bank.name = trimmed;
    return writeBanks(records);
  }
  return false;
}

bool PresetManager::moveToBank(const juce::String& presetId, const juce::String& bankId, int index) const {
  const juce::ScopedLock lock(cacheLock);
  auto records = banks();
  auto destination = std::find_if(records.begin(), records.end(), [&](const auto& bank) {
    return bank.id == bankId;
  });
  if (destination == records.end() || !presetId.startsWith(kUserPrefix)) return false;
  bool found = false;
  for (auto& bank : records) {
    found = bank.presets.contains(presetId) || found;
    bank.presets.removeString(presetId);
  }
  if (!found) return false;
  destination->presets.insert(juce::jlimit(0, destination->presets.size(), index), presetId);
  return writeBanks(records);
}

bool PresetManager::copyToBank(const juce::String& presetId, const juce::String& bankId) const {
  const juce::ScopedLock lock(cacheLock);
  auto records = banks();
  auto destination = std::find_if(records.begin(), records.end(), [&](const auto& bank) {
    return bank.id == bankId;
  });
  auto preset = load(presetId);
  if (destination == records.end() || !preset.isValid()) return false;
  const auto baseName = preset.getProperty("name", "Preset").toString();
  auto name = baseName;
  const auto presets = list();
  const auto nameUsed = [&](const juce::String& candidate) {
    return std::any_of(presets.begin(), presets.end(), [&](const auto& item) {
      return destination->presets.contains(item.id) && item.name.equalsIgnoreCase(candidate);
    });
  };
  for (int suffix = 2; nameUsed(name); ++suffix) name = baseName + " " + juce::String(suffix);
  return save(name, std::move(preset), bankId, true).id.isNotEmpty();
}

bool PresetManager::deleteBank(const juce::String& id) const {
  const juce::ScopedLock lock(cacheLock);
  auto records = banks();
  auto bank = std::find_if(records.begin() + 1, records.end(), [&](const auto& item) { return item.id == id; });
  if (bank == records.end()) return false;

  // Keep a recoverable copy until all file moves and the bank update succeed.
  const auto pending = userDir.getNonexistentChildFile(".deleted-bank", {}, false);
  if (!pending.createDirectory()) return false;
  std::vector<std::pair<juce::File, juce::File>> moved;
  bool complete = true;
  for (const auto& presetId : bank->presets) {
    const auto original = fileForId(presetId);
    if (original == juce::File()) { complete = false; break; }
    const auto target = pending.getChildFile(original.getFileName());
    if (!original.moveFileTo(target)) { complete = false; break; }
    moved.emplace_back(original, target);
  }
  records.erase(bank);
  if (!complete || !writeBanks(records)) {
    bool restored = true;
    for (const auto& [original, target] : moved)
      restored = target.moveFileTo(original) && restored;
    if (restored) pending.deleteFile();
    else juce::Logger::writeToLog("[Presets] Recover files from " + pending.getFullPathName());
    return false;
  }
  if (!pending.deleteRecursively())
    juce::Logger::writeToLog("[Presets] Could not clear " + pending.getFullPathName());
  return true;
}

PresetManager::Info PresetManager::save(const juce::String& name, juce::ValueTree preset,
                                       const juce::String& bankId, bool banked) const {
  const juce::ScopedLock lock(cacheLock);
  // ensureWritableDir rather than a bare createDirectory: the folder can
  // exist and still be unwritable (root-owned after a sudo'd script), and
  // that state used to fail every save with this same log line forever.
  if (!TONE3000Processor::ensureWritableDir(userDir)) {
    juce::Logger::writeToLog("[Presets] Failed to create presets directory: " +
                             userDir.getFullPathName());
    return {};
  }

  // Same-name save overwrites that preset (keeps its id); this is the update
  // path. A legacy file picks up its stem as the stored id here, so the id
  // the UI and any DAW project already hold stays valid.
  juce::File current;
  juce::String rawId;
  auto records = banked ? banks() : std::vector<Bank>{};
  auto bank = std::find_if(records.begin(), records.end(), [&](const auto& item) { return item.id == bankId; });
  if (banked && bank == records.end()) return {};
  const auto preferred = banked ? preset["id"].toString() : juce::String();
  for (const Entry& existing : entries()) {
    if (!existing.info.factory && (!banked || bank->presets.contains(existing.info.id)) &&
        existing.info.name.compareIgnoreCase(name) == 0) {
      if (rawId.isEmpty() || existing.info.id == preferred) {
        current = existing.file;
        rawId = stripPrefix(existing.info.id, kUserPrefix);
      }
      if (preferred.isEmpty() || existing.info.id == preferred) break;
    }
  }
  if (rawId.isEmpty())
    rawId = juce::Uuid().toString();

  preset.setProperty("name", name, nullptr);
  preset.setProperty("id", rawId, nullptr);
  const auto written = writeUserPreset(userDir, current, name, preset);
  if (written == juce::File()) {
    juce::Logger::writeToLog("[Presets] Failed to write preset file for: " + name);
    return {};
  }

  Info info;
  info.id = kUserPrefix + rawId;
  if (banked && !bank->presets.contains(info.id)) {
    bank->presets.add(info.id);
    if (!writeBanks(records)) {
      written.deleteFile();
      return {};
    }
  }
  info.name = name;
  info.factory = false;
  return info;
}

bool PresetManager::rename(const juce::String& id, const juce::String& newName) const {
  const juce::String trimmed = newName.trim();
  if (!id.startsWith(kUserPrefix) || trimmed.isEmpty())
    return false;
  const juce::File file = fileForId(id);
  juce::ValueTree preset = presetfile::read(file);
  if (!preset.isValid())
    return false;
  preset.setProperty("name", trimmed, nullptr);
  // Legacy file: the stem was its id; pin it inside before the file moves.
  preset.setProperty("id", stripPrefix(id, kUserPrefix), nullptr);
  return writeUserPreset(userDir, file, trimmed, preset) != juce::File();
}

bool PresetManager::remove(const juce::String& id) const {
  if (!id.startsWith(kUserPrefix))
    return false;
  const juce::File file = fileForId(id);
  return file != juce::File() && file.deleteFile();
}
