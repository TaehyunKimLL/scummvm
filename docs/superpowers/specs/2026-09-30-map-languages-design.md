# Game languages declared by the hi-res text map: design

Date: 2026-09-30. Branch `dos-port`, worktree `~/work/scummvm/dos`.
Prerequisite: `docs/superpowers/plans/2026-09-30-hires-config-unify.md` complete through Task 18 (version-2 maps,
`HIRESTXT.MAP`, the unified ini keys, repacked packages). This document builds on
`docs/superpowers/specs/2026-09-30-hires-config-unify-design.md` ("the unify spec"; section numbers prefixed `U`).
Plan: `docs/superpowers/plans/2026-09-30-map-languages.md`.

## 0. Binding rulings (user, 2026-09-30)

1. **One game folder holds several languages**: the original plus translations (Korean now, others later).
2. **Languages come from the map**, not from file names and not from md5 detection. The game is detected as its original
   release. The map found as U4 says (`hires_text_map`, else `<game folder>/HIRESTXT.MAP`) declares
   `[translation.<lang>]` sections; each declared language is **added** to the game's selectable languages. The player
   picks it in ScummVM's Language setting (launcher, Edit Game) or with ini `language=<code>`. Selecting it loads that
   section's translation resources and that language's font and render settings.
3. **The section gives the encoding** (`utf-8` or `cp949`), not the language.
4. **No backward compatibility** (as U0.1): the Korean overlay detection entries, `.trs` discovery by file name
   (`korean.trs`, `<code>.trs`), the `sci-<lang>.str` naming rule, the `Text.MAP`/`Text.Res` overlay and the old
   separate-folder packages are removed, not aliased. Packages are regenerated.

## 1. What ScummVM does today (research, file:line at `1f6d36fa618`)

### 1.1 The language list and `language=`

- A detected game carries one `Common::Language` (`DetectedGame::language`) and a GUI-options string. The Advanced
  Detector appends `lang_<English name>` for the entry's language, and `lang_English` when the entry has
  `ADGF_ADDENGLISH` (`engines/advancedDetector.cpp:218-223`, `common/language.cpp:182-187`). `ADGF_DROPLANGUAGE`
  only keeps the language out of the target name (`advancedDetector.h:153`, `advancedDetector.cpp:161-163`).
- When a game is added, the target gets `language=<code>` and `guioptions=<string>` copied from the detection result
  (`base/plugins.cpp:761-765`). `--upgrade-targets` rewrites `guioptions` and sets `language` only if unset
  (`base/commandLine.cpp:1986-1998`).
- Engines refresh `guioptions` on every start through `Common::updateGameGUIOptions()`
  (`common/gui_options.cpp:211-214`): SCI in `SciMetaEngine::createInstance()` (`engines/sci/metaengine.cpp:214-224`,
  which keeps the `lang_` tags already stored), SCUMM in its `createInstance()` (`engines/scumm/metaengine.cpp:430-432`).
- The Edit Game dialog's Language popup lists `<default>` and every language whose `lang_` tag is in the target's
  `guioptions` (all languages when there is no `lang_` tag at all) (`gui/editgamedialog.cpp:400-413`,
  `common/language.cpp:172-180`); it is disabled when only one language is listed (`editgamedialog.cpp:496-499`);
  `GUIO_NOLANG` hides it (no SCI or SCUMM entry sets it). OK writes `language=<code>`, `<default>` removes the key
  (`editgamedialog.cpp:537-545`). The options read `guioptions` from ConfMan only (`gui/options.cpp:266-271`).
- **Starting a target re-runs detection with `language=` as a filter.** `identifyGame()` reads `language`
  (`advancedDetector.cpp:360-366`) and `detectGame()` skips every entry whose language differs
  (`advancedDetector.cpp:770-776`). A SCI target with `language=ko` on an English folder therefore matches no table entry
  and falls through to fallback detection (`advancedDetector.cpp:418-427`, `sci/metaengine.cpp:628-672`). SCUMM's
  `identifyGame()` looks only at `gameid` (`engines/scumm/detection.cpp:192-196`) and applies `language=` as an override
  after detection (`scumm/metaengine.cpp:448-450`).
- A MetaEngine *can* add languages after detection: `DetectedGame` is built in engine code (SCUMM
  `detection.cpp:224-253`; SCI post-processes the Advanced Detector's result in `SciMetaEngineDetection::detectGames()`,
  `sci/detection.cpp:228-248`, which already reads files of the game folder through `customizeGuiOptions(gamePath, ...)`),
  and `toDetectedGame()` is virtual (`advancedDetector.h:602`). Nothing in the launcher asks the engine for languages at
  options time: the popup is built from the stored `guioptions` only.

### 1.2 SCUMM translations today

- The bundle is found by name: `getTrsBundleNames()` gives `<code>.trs`, then `korean.trs` for Korean
  (`engines/scumm/trs_bundle.h:46-70`); detection turns a bundle's file name back into a language
  (`trs_bundle.h:86-98`, `detection_internal.h:217-261`), so a folder with `korean.trs` is detected as Korean.
- `probeLanguageBundle()` (`string.cpp:2415-2493`) opens the first name present, decides UTF-8 from the body BOM or
  `text_encoding=utf8` (`decideTrsUtf8()`, `text_utf8.h:176-182`), checks the renderer takes code points (v1-v6 PC) and
  otherwise ignores the bundle; with hi-res text off it transcodes UTF-8 to `legacyTextPage(_language)`.
  `loadLanguageBundle()` reads it (`string.cpp:2525-2618`).
- The Korean paths key on `_language == KO_KOR`: `isScummvmKorTarget()` (`charset.cpp:50-58`), `loadKorFont()` and
  `loadCJKCells()` open `korean.fnt` / `korean%02d.fnt` by name (`charset.cpp:208-217`, `260-300`), the
  2350-glyph index (`charset.cpp:320-323`), `_newTextRenderStyle` (`scumm.cpp:963`). `_language` is the detected language
  with the ini override applied (`scumm.cpp:142`, `metaengine.cpp:448-450`).
- Translated strings are copied into resources (`loadPtrToResource()` translates when a bundle is loaded,
  `resource.cpp:1355-1365`), so verb and object names in a save are in the language that was active when they were set.

### 1.3 SCI translations today

- Sources: `addAppropriateSources()` adds the volumes, the game folder as a patch directory, and `message.map` +
  `resource.msg` when `message.map` exists in the search path (`engines/sci/resource/resource.cpp:636-730`). Patch files
  are listed through `SearchMan` with top-level patterns (`resource.cpp:1785-1845`); the game folder is in `SearchMan`
  with depth 4 but not flat (`engines/engine.cpp:234-236`, `common/fs.cpp:508-531`), so files in a subfolder are **not**
  seen by those names.
- The Korean overlay format is recognised by the name `message.map` plus `getLanguage() == KO_KOR`
  (`isKoreanMessageMap()`, `resource.cpp:3118-3120`, used at `880`, `1953-1990`), and volume reads of TEXT/MESSAGE
  resources switch format on the same language test (`resource.cpp:611-618`).
- Script strings: `ScriptStrings::load()` opens `sci-<language code>.str` (`engines/sci/engine/translation.cpp:30-43`);
  its presence with `language=` set also declares the TEXT resources UTF-8 (`_utf8Manifest`, `sci.cpp:328-343`).
- `Text.MAP`/`Text.Res` overlays are found by name and force `KO_KOR` (`engine/text_overlay.cpp:47+`,
  `sci.cpp:318-325`, `sci.cpp:1145-1146`).
- Detection: Korean overlays are separate `KO_KOR` entries listing the English files plus `resource.msg` or `text.000`
  (`detection_tables.h`: Castle of Dr. Brain 221, EcoQuest 665, EcoQuest 2 802, GK1 1006/1013, GK2 1097, KQ1 1688 (fork,
  `ADGF_UTF8I18N`) and 1700, KQ5 2050, KQ6 2540/2550/2560, LB2 3021, SQ4 6300); the fork's LB1 entry (2926) is an in-place
  patch (its map and volume differ from every English entry). `ADGF_UTF8I18N` (`engines/sci/detection.h:52-64`) marks the
  data UTF-8.
- Encoding: `getLanguage()` returns the overlay's Korean, else `language=`, else the entry (`sci.cpp:1138-1168`);
  `languageCodePage()` maps it to a code page (`sci.cpp:1116-1136`); `text_encoding=` overrides
  (`sci.cpp:150-160`, `textencoding.cpp:80-91`, `sci.cpp:1107-1113`); `heapStringsAreUtf8()` is `ADGF_UTF8I18N` or the
  manifest (`sci.cpp:1057-1075`). The `.uni` bundle is opened by the names `sci.uni`, `korean.uni`, `towns.uni`
  (`engines/sci/graphics/cache.cpp:416-431`).
- Saves record `script0Size` and `gameObjectOffset` (`engine/savegame.h:82-98`), no language. Heap strings copied from
  TEXT resources are saved with the heap.

## 2. The model

**Detection returns the original release with its own language; the map's languages are added to that target's
language list, and `language=` chooses among them at start.** Concretely:

1. Detection (SCI `detectGames()`, SCUMM `detectGames()`) reads `<folder>/HIRESTXT.MAP` (the only map it can know: no
   ini exists yet), and appends `lang_<name>` for each declared language the engine can draw in that release. One entry
   per game, never one per language.
2. Every start (the engines' existing `updateGameGUIOptions()` calls) recomputes the tags from the map the target
   actually resolves (U4, with the target's `hires_text_map`), so the popup follows the map.
3. The Edit Game popup is unchanged in how it lists languages; it gains the section's `name=` as a label suffix
   (section 5.4).
4. At start the engine resolves the text language from `language=` (section 6) and loads the chosen section.
5. The Advanced Detector's language filter learns that a map-declared language is not a detection language
   (section 5.3), so `language=ko` on an English folder still matches the English entry.

Why not one detection entry per language: the language list would depend on a file the launcher's "Add game" scan
cannot relate to an ini key (`hires_text_map` pointing elsewhere is invisible to it); "Add game" would ask the player to
choose between identical-looking entries and create one target per language with separate saves by accident; and
`--upgrade-targets`/`identifyGame()` would have to match an entry whose files are not what defines it. Why not only at
options time: the popup has no engine hook, and a target added from the command line or by a mass add would show no
Korean until an engine hook is added to common GUI code. The chosen model uses the existing `guioptions` channel at the
two points ScummVM already writes it, plus one small detector hook.

## 3. The `[translation.<lang>]` section

### 3.1 Name

`[translation.<lang>]`, where `<lang>` is a ScummVM language code, exactly as `language=` takes it
(`Common::parseLanguage()`, case-insensitive). For most languages that is the ISO 639-1 code (`ko`, `ja`, `de`, `th`);
ScummVM's exceptions keep ScummVM's spelling (`cn`, `tw` for the two Chinese scripts, `br` for Brazilian Portuguese,
`fr-ca`), because the section must round-trip with `language=` and with the popup. No qualifier (`:q`) is allowed.
A section whose language is the detected game's own language (the English family `en`/`gb`/`us` counts as one) is
ignored with a warning.

### 3.2 Keys

| Key | Value | Required | Read by | Meaning |
|---|---|---|---|---|
| `encoding` | `utf-8` or `cp949` (parsed by `HiResFontMap::parseCodePage()`, so its aliases `utf8`, `euc-kr` also work) | yes | SCI, SCUMM | Encoding of every text resource of this translation. `cp949` only with `<lang>` = `ko`. |
| `name` | UTF-8 text, at most 60 bytes | no | SCI, SCUMM | Display name: suffix of the popup label (5.4) and the start log line. |
| `map` | path (U4) of a version-2 map | no | SCI, SCUMM | The fonts and render settings of this language (section 4). Unset: the game map's own font sections. |
| `dir` | path of a folder | no | SCI, SCUMM | The language folder: auxiliary files the engine opens by fixed names are looked up here first (3.4). |
| `trs` | path of an SCVMTRS bundle | SCUMM: yes | SCUMM | The translation text. |
| `messages` | `<map path>, <volume path>` | SCI: one of these three | SCI | A message-map/volume pair in the fan-patch layout (today's `MESSAGE.MAP` + `RESOURCE.MSG`). |
| `strings` | path of a script-string table | SCI: one of these three | SCI | Today's `SCI-<LANG>.STR` format (`engine/translation.h`), any file name. |
| (`dir`) | | SCI: one of these three | SCI | Resource patch files in `dir` count as translation resources. |

Paths follow U4 exactly: relative to the folder of the map that names them, `data:` in the data roots, absolute as
written. A key is read once; unknown keys, keys another engine reads, and invalid values give the U10.2/U10.3 warnings.

### 3.3 Validation (a section is **declared** only if all hold)

1. `<lang>` parses; no qualifier; not the game's own language.
2. `encoding` present and one of the two values; `cp949` only for `ko`.
3. Every path key names an existing file (`dir`: an existing folder); `messages` has exactly two entries; `map` loads as a
   version-2 map (U10.1).
4. The engine's required resource key(s) are set (SCUMM `trs`; SCI at least one of `messages`, `strings`, `dir`).
5. The engine can draw it in this release (**usable**):
   - SCUMM, `utf-8`: the release takes code points (v1-v6, `heversion == 0`, not FM-Towns, PC-Engine, Sega CD, NES,
     Mac Loom/Indy3; today's rule at `string.cpp:2462-2466`, moved into one function). `cp949`: `v < 7` or FT
     (today's `isScummvmKorTarget()` condition).
   - SCI: SCI0 through SCI1.1 (the 16-bit text path). SCI32 is out of scope (section 12).

A section failing 1-4 is ignored with a warning (at start; silent at detection). A section failing 5 is not listed and,
if chosen, refused at start (section 6).

### 3.4 `dir` per engine

The folder is **not** added to `SearchMan`: nothing in it is seen unless the language is chosen, and nothing in it can
shadow the game's own files. When the language is chosen, the engine looks in it first for:

- SCUMM: `korean.fnt`, `korean00.fnt`..`korean19.fnt` (the patch fonts `loadKorFont()`/`loadCJKCells()` open) - the only
  fixed-name files a SCUMM translation carries.
- SCI: resource patch files (every name `readResourcePatches()` accepts: `text.001`, `0.fon`, `100.msg`, ...), which
  replace resources of the same id after the game's own patches; and the `.uni` bundle names (`sci.uni`, `korean.uni`,
  `towns.uni`).

Lookups in `dir` are case-insensitive (`Common::FSDirectory`).

## 4. Game map and language map

- **The game map** is the map U4 resolves (`hires_text_map`, else `<game folder>/HIRESTXT.MAP`). It declares the
  languages. Its font sections (`[render]`, `[text]`, `[layout]`, `[fonts]`, `[font*]`, `[glyphs*]`, `[shadow]`) apply
  to the original language, and to a translation without `map=`.
- **A manifest-only game map** (no section other than `[map]` and `[translation.*]`) applies **nothing** to the original
  language: the engine runs the original exactly as with `hires_text_map=` (no map). This is what the packages ship, so
  the English original draws exactly as without the hi-res layer.
- **A language map** (`[translation.X] map=`) replaces the game map's font sections completely while that language is
  chosen; there is no merge between the two. It is an ordinary version-2 map with two differences: a
  `[translation.*]` section in it is ignored with a warning (only the game map declares languages), and `[text]
  encoding` in it is ignored with a warning (the section's `encoding` applies).
- ini keys (U11: `hires_text_face`, `_size`, `_scale`, `_blend`, `_advance`, `render_target`) apply over whichever map is
  in effect, as in U3.2.
- Why `map=` rather than inline keys: a language's font setup is a whole version-2 map (fonts table, per-id sections,
  glyph remaps, render, shadow); inlining it would need a second level of section names (`[translation.ko/font.4]`) and a
  merge rule for every key. A separate map keeps one loader and one precedence rule, lets one language map serve several
  games from `DATA\`, and reuses the maps the unify plan already ships (`LB2KO.MAP`, `M2KO.MAP`, ...) unchanged except
  for dropping `[text] encoding`.
- **Presets** (U = true colour, L = 8-bit) are not files (user decision 2026-09-30, U ruling 7): one language map per
  game holds both, its L variant in `:clut8` sections (U3.4), and the render target chooses (U7.1.1). A folder ships one
  game map, `HIRESTXT.MAP` (declares `ko` with `map=data:LB2KO.MAP`); the L target adds `render_target=clut8`. The
  language map is loaded with the same two phases and target as a game map; the game map's `[translation.*]` sections
  take no qualifiers (section 12).

## 5. The language list

### 5.1 Shared reader

`graphics/hires_text/map_languages.{h,cpp}` (new, part of the U13 shared set) reads only `[map] version` and the
`[translation.*]` sections of a game map, validates 3.3 items 1-4, and returns the declared sections in file order with
their warnings. It depends on `common/` and the U4 path helpers only. The engine supplies item 5 as a predicate. The
version-2 font loader (`HiResFontMap::loadMap`) keeps skipping `[translation.*]` in a game map (the reader owns them) and
warns about them in a language map. The honoured-key set gains `kHiResKeyTranslation`; AGS lacks it and gets the U10.3
warning `AGS does not use [translation.ko]`.

### 5.2 Where the list is written

| When | Map read | Code |
|---|---|---|
| Add game / mass add / `--add` / `--detect` | `<folder>/HIRESTXT.MAP` only | SCI `SciMetaEngineDetection::detectGames()`, SCUMM `ScummMetaEngineDetection::detectGames()` |
| Every start | the target's game map (U4, with `hires_text_map`) | SCI `SciMetaEngine::createInstance()`, SCUMM `ScummMetaEngine::createInstance()` (their `updateGameGUIOptions()` calls) |
| `--upgrade-targets` | as "Add game" | (unchanged code path; calls `detectGames()`) |

The language tags written are: the detected entry's language (plus English for `ADGF_ADDENGLISH`), then each declared,
usable language in map order. SCI stops carrying stored `lang_` tags forward (`metaengine.cpp:216-224`): it recomputes
them, so a language removed from the map leaves the popup at the next start.

### 5.3 Detector hook

`AdvancedMetaEngineDetectionBase` gains

```cpp
virtual Common::Language languageForMatching(const Common::FSNode &gameDir, Common::Language iniLanguage) const {
	return iniLanguage;
}
```

called in `identifyGame()` on the value read from `language=` before `detectGame()` filters with it. SCI overrides it:
when the target's game map declares `iniLanguage` (section 3.3 items 1-4), it returns `Common::UNK_LANG`, so the
original's entry matches. SCUMM needs no hook (its `identifyGame()` does not filter).

### 5.4 Display name

`MetaEngine` gains `virtual void getLanguageLabels(const Common::String &target, Common::HashMap<int, Common::U32String>
&labels) const {}`; SCI and SCUMM fill it from the target's game map (`labels[KO_KOR] = name`). `EditGameDialog`
appends ` - <name>` to the popup entry of a language that has a label ("Korean - 2014 fan translation"). The launcher's
font may lack the script of `name`; packages for DOS write it in ASCII.

## 6. Choosing the language at start

`language` is read through ConfMan's normal lookup (command line `--language`, the game domain the popup writes,
`[scummvm]`). Let D be the detected language, L the value (possibly unset), M the game map's declared sections.

| Case | Text language | Loaded | Message |
|---|---|---|---|
| L unset, or L = D (English family counts as one) | D | the game map (or no map if manifest-only) | - |
| L declared in M and usable | L | the section's resources; its `map=` or the game map | debug 1: `HIRESTXT.MAP: language ko (<name>) from <game map>: encoding cp949, map <path>` |
| L declared in M, not usable in this release | D | as the first row | warning (10.4) |
| L declared in M, resources fail to load (bad bundle, encoding mismatch) | D | as the first row | warning (10.4) |
| L not declared, M non-empty | ScummVM's upstream override (as today, unchanged) | as today | warning (10.4) naming the declared languages |
| L not declared, M empty | upstream override | as today | - |

"Text language" is what the engine uses everywhere it uses the game language today (SCUMM `_language`, SCI
`getLanguage()`): the Korean code paths keyed on `KO_KOR` therefore run for a `ko` translation exactly as they ran for
the removed Korean entries. A refused translation never leaves a half-translated game: the whole start falls back to D.

## 7. Loading flow per engine

### 7.1 SCUMM

1. `ScummMetaEngine::createInstance()`: remember D = `res.language`; read the game map's manifest with the SCUMM rules and
   `Scumm::translationUsable(release, tr, why)` (release = version, HE version, platform, game id; at start it also
   checks the bundle's header against `encoding`); choose (section 6); set `res.language` to the text language; hand
   the chosen `Graphics::HiResTranslation` (or none) to the engine (`ScummEngine::setTranslation()`, before `init()`).
2. `ScummHiResText::loadConfig()`: loads the language map when the translation has one, else the game map, else none
   (manifest-only game map with no translation: none). The default code page is the translation's `encoding` when a
   translation is chosen; the game map's `[text] encoding` otherwise (U3.2).
3. `probeLanguageBundle()`: no name probing. With no translation: no bundle (`_trsBundlePath` empty). With one: the
   bundle is `trs=`; `encoding=utf-8` sets `_textUtf8` (a body BOM is skipped as today; no BOM is fine);
   `encoding=cp949` with a body BOM refuses the translation (10.4). The renderer check moves to
   `translationUsable()`. Hi-res text off with `utf-8`: today's transcode to `legacyTextPage(_language)`, else '?' with
   today's warning. `text_encoding` is no longer read by SCUMM.
4. `loadLanguageBundle()` opens the resolved `trs` path (`Common::File` on the `FSNode`).
5. `loadKorFont()`, `loadCJKCells()`: open `korean*.fnt` from `dir` first, then by name as today (a Korean original
   release keeps its root `korean.fnt`).

### 7.2 SCI

1. `SciEngine` constructor: read the manifest with the SCI rules and `Sci::sciTranslationUsable(version, tr, why)`; choose;
   store `_translation` and `_textLanguage`. `getLanguage()` returns `_textLanguage` (no `Text.MAP` branch, no
   ConfMan branch of its own). `text_encoding` keeps its meaning only with no translation; with one it is ignored with a
   warning.
2. Encoding: `heapStringsAreUtf8()` = translation `encoding == utf-8` (with no translation: `text_encoding`, else false);
   `usesKoreanText()` = translation `encoding == cp949` (with none: `textEncodingKorean(text_encoding, language)` as
   today); `getSciLanguageCodePage()` = `kWindows949` for `cp949`, else as today. `ADGF_UTF8I18N` and `_utf8Manifest`
   are removed.
3. `SciEngine::run()`: after `addAppropriateSources()`, `ResourceManager::addTranslationSources(const
   Graphics::HiResTranslation &)` adds, in this order: `dir`'s patch files (a `TranslationDirectoryResourceSource`
   listing the folder itself, not `SearchMan`); `messages` as an `ExtMapResourceSource` + `VolumeResourceSource` opened
   from their `FSNode`s and flagged as translation sources. Translation sources are scanned after the game's own, so
   their resources replace the game's.
4. The fan-patch map/volume format is keyed on the source flag, not on `KO_KOR` or the name `message.map`:
   `isKoreanMessageMap(source)` becomes `source->isTranslation()`, and the TEXT/MESSAGE volume rule at
   `resource.cpp:611-618` asks the same. A root `message.map` (Sierra's own) is read as upstream reads it.
5. `ScriptStrings::load(const Common::Path &)` opens `strings=`. `TextOverlay` is deleted.
6. `GfxCache`: the map to load is the language map, else the game map (manifest-only: none); `loadUniBundle()` tries the
   three names in `dir` first.

### 7.3 AGS

Unchanged. `[translation.*]` in an AGS game's map gets the U10.3 warning. AGS keeps its own `.tra` translations.

## 8. Detection and removed code

- **SCI**: every `KO_KOR` entry whose files are an English entry's files plus overlay files (`resource.msg`,
  `message.map`, `text.NNN`) is removed: Castle of Dr. Brain, EcoQuest, KQ1 (both), KQ5, KQ6 (three), LB2, SQ4, and -
  pending the user's answer - GK1 and GK2 (section 13). The plan checks each against its English entry's md5s before
  removing it; an entry whose own map or volumes differ (EcoQuest 2, the fork's in-place LB1) is a Korean original and
  stays. `ADGF_UTF8I18N` is removed.
- **SCUMM**: `detectLanguageBundle()`, `getTrsBundleName()`, `getTrsBundleNames()`, `getTrsBundleLanguage()` and the
  `.trs` branch of `detectLanguage()` are removed; `trsBodyIsUtf8()` stays. The md5 table's `KO_KOR` rows
  (`scumm-md5.h`) describe data files that are themselves Korean (in-place patches) and stay; such a game's D is Korean.
  Other upstream language detection (Chinese font, Russian patcher, Rebel Assault II `GAME.TRS`, Dig/COMI bundles) is
  unchanged.
- **SCI engine**: `TextOverlay` (`Text.MAP`/`Text.Res`), `_utf8Manifest`, the `sci-<lang>.str` name, the
  `message.map`-name rule, `isKoreanMessageMap()`'s language test.
- **Map loader**: `kHSecReserved` becomes the translation family (read by `map_languages`, skipped by the font loader in
  a game map, warned in a language map).

## 9. Saves, existing inis, GUI edge cases

- **Saves** stay untagged and shared by all languages of one target (save files are named by target). Loading a save made
  in another language is allowed: SCUMM verb and object names, and SCI heap strings copied from text resources, stay in
  the language they were saved in until the game rewrites them. Packages keep one target per language (`lb2`, `lb2ko`),
  so their saves do not mix. SCI's `script0Size` check is unaffected: a translation carries no SCRIPT resources (a
  translation that does is refused by the save check exactly as a patched script is today).
- **Existing inis**: `language=ko` keeps working when the folder's map declares `ko`. An old package (separate Korean
  folder, overlay files at its root, no `[translation.ko]`) is not supported: with `language=ko` the start warns
  (section 6, last-but-one row) when a map exists, the SCI overlay files at the root are read as Sierra's own
  `message.map`/volume, and the text is wrong. The fix is the regenerated package. `language=en` on an English original
  is case 1. `text_encoding` in a SCUMM target is ignored silently (SCUMM never had it upstream).
- **A language the engine cannot draw** (SCI32, SCUMM v7/v8 or FM-Towns with `utf-8`, `cp949` for a language other
  than `ko`): not listed in the popup; chosen through the ini, it is refused at start (section 6) and the original runs.
- **A language ScummVM has no code for** (`[translation.xx]`): warning at start, never listed.
- **`hires_text=false`**: the map is still read for its `[translation.*]` sections (`hires_text` turns off faces, not
  translations); `hires_text_map=` (empty) means no map and therefore no added languages. A `utf-8` translation without
  the hi-res layer draws as today (SCUMM: transcoded or '?'; SCI: the `.uni` bundle or the game font).
- **The game map changes between starts**: the popup follows at the next start (5.2); a `language=` no longer declared is
  the "not declared" row.

## 10. Errors and warnings

Same channel and style as U10: `warning()` once per cause per start, collected in the manifest's `warnings` for tests;
at detection they are collected and printed only at debug level (`debugC(kDebugGlobalDetection)`).

### 10.1 A section is ignored (the map still loads)

- `HIRESTXT.MAP: [translation.xx]: 'xx' is not a ScummVM language code; section ignored`
- `HIRESTXT.MAP: [translation.ko:pc]: a translation section takes no qualifier; section ignored`
- `HIRESTXT.MAP: [translation.en]: English is the game's own language; section ignored`
- `HIRESTXT.MAP: [translation.ko]: encoding is missing; section ignored`
- `HIRESTXT.MAP: [translation.ko] encoding 'cp932' is not utf-8 or cp949; section ignored`
- `HIRESTXT.MAP: [translation.ja] encoding cp949 is for ko only; section ignored`
- `HIRESTXT.MAP: [translation.ko] trs=KO/KOREAN.TRS: no such file; section ignored` (any path key; `dir`: `no such folder`)
- `HIRESTXT.MAP: [translation.ko] messages needs the map and the volume, in that order; section ignored`
- `HIRESTXT.MAP: [translation.ko] map=data:LB2KO.MAP: not a version 2 map; section ignored`
- `HIRESTXT.MAP: [translation.ko]: SCUMM needs trs=; section ignored`
- `HIRESTXT.MAP: [translation.ko]: SCI needs messages=, strings= or dir=; section ignored`

### 10.2 A key is ignored

- `HIRESTXT.MAP: unknown key [translation.ko] <key>` (U10.2 text)
- `SCI does not use [translation.ko] trs` (U10.3 text; the section is otherwise read)
- `HIRESTXT.MAP: [translation.ko] name is longer than 60 bytes or not UTF-8; ignored`

### 10.3 In a language map

- `HIRESTXT.MAP: [translation.ko] is read only in the game's map; ignored`
- `HIRESTXT.MAP: [text] encoding is not read in a language map; the translation's encoding applies`
- AGS game map: `AGS does not use [translation.ko]`

### 10.4 At start

- `HIRESTXT.MAP: language=ko: SCUMM draws UTF-8 text only in v1-v6 PC releases; the game runs in English` (the `why`
  text comes from the engine's `translationUsable()`; SCI: `SCI draws translations only in SCI0-SCI1.1 games`)
- `HIRESTXT.MAP: language=ko: trs=<path> is not an SCVMTRS bundle; the game runs in English`
- `HIRESTXT.MAP: language=ko: trs=<path> starts with a UTF-8 BOM but encoding is cp949; the game runs in English`
- `HIRESTXT.MAP: language=ja is not declared by <game map> (declared: ko); ScummVM's language override applies`
- `SCI: text_encoding is ignored: [translation.ko] encoding=cp949 applies`

## 11. Examples

### 11.1 Laura Bow 2, English + Korean, one folder, one `RESOURCE.AUD`

```text
SCUMMVM\
  SCUMMVM.EXE  SCUMMVM.INI  EXAMPLE.INI  README.TXT  LB2.BAT  LB2KO.BAT  LB2KOL.BAT
  DATA\
    LB2KO.MAP                         language map (fonts, render; U bare, L in :clut8) - version 2, no [text] encoding
    KO2350.SVF  KO2350G.SVF  ENCODING.DAT  OFL*.TXT
  GAMES\LB2\
    RESOURCE.MAP  RESOURCE.000  RESOURCE.AUD  RESOURCE.SFX  AUDIOSFX\  *.DRV ...   English DOS CD, unmodified
    HIRESTXT.MAP                       game map: declares ko (both presets)
    KO\
      MESSAGE.MAP  RESOURCE.MSG        the 2014 fan patch's two files, unmodified (cp949)
```

```ini
; GAMES\LB2\HIRESTXT.MAP - Laura Bow 2 languages (manifest only: English draws as without a map)
[map]
version=2

[translation.ko]
name=2014 fan translation
encoding=cp949
messages=KO/MESSAGE.MAP, KO/RESOURCE.MSG
map=data:LB2KO.MAP
```

```ini
; SCUMMVM.INI
[scummvm]
render_target=auto

[lb2]
engineid=sci
gameid=laurabow2
description=Laura Bow 2 (English)
platform=pc
path=GAMES\LB2
extrapath=DATA

[lb2ko]
engineid=sci
gameid=laurabow2
description=Laura Bow 2 (Korean, anti-aliased)
platform=pc
language=ko
path=GAMES\LB2
extrapath=DATA

[lb2kol]
engineid=sci
gameid=laurabow2
description=Laura Bow 2 (Korean, 8-bit font)
platform=pc
language=ko
render_target=clut8
path=GAMES\LB2
extrapath=DATA
```

Detection of `GAMES\LB2` gives `laurabow2` English DOS CD with `lang_English lang_Korean`; the popup of `lb2` offers
Korean ("Korean - 2014 fan translation"). Speech is the English `RESOURCE.AUD` in every target; the zip carries it once
(about 230 MB instead of 460 MB).

### 11.2 Monkey Island 2, English + Korean

```text
GAMES\MI2\
  MONKEY2.000  MONKEY2.001            English DOS release
  HIRESTXT.MAP                        game map: declares ko
  KO\
    KOREAN.TRS                        the fan translation (cp949 SCVMTRS bundle)
    KOREAN00.FNT ... KOREAN08.FNT     the patch fonts (cells, and the draw fonts with hi-res text off)
DATA\
  M2KO.MAP  M2U*.SVF  M2L*.SVF  ENCODING.DAT
```

```ini
; GAMES\MI2\HIRESTXT.MAP
[map]
version=2

[translation.ko]
name=mi2kor fan translation
encoding=cp949
trs=KO/KOREAN.TRS
dir=KO
map=data:M2KO.MAP
```

```ini
[mi2]
engineid=scumm
gameid=monkey2
description=Monkey Island 2 (English)
path=GAMES\MI2
extrapath=DATA

[mi2ko]
engineid=scumm
gameid=monkey2
description=Monkey Island 2 (Korean, anti-aliased)
language=ko
path=GAMES\MI2
extrapath=DATA

[mi2kol]
engineid=scumm
gameid=monkey2
description=Monkey Island 2 (Korean, 8-bit font)
language=ko
render_target=clut8
path=GAMES\MI2
extrapath=DATA
```

`M2KO.MAP` loses its `[text] encoding=cp949` (the section says it). The English target needs no `language=en` any
more: nothing in the folder makes detection say Korean.

### 11.3 The other packages

LB1, KQ1 and Camelot become one folder each in the same way (`KO\RESOURCE.MSG`, `KO\MESSAGE.MAP`, `KO\SCI-KO.STR`,
KQ1's `KO\TEXT.000` and `KO\KOREAN.UNI` reached through `dir=KO`; `encoding=utf-8`). MI1's package is the in-place DUMB
patch, detected as a Korean original by md5: it has no translation section and changes only by its rebuilt EXE.

## 12. Out of scope

- SCI32 translations (GK1, GK2 and later games): not usable (3.3 item 5).
- AGS languages (AGS keeps `.tra`).
- Tagging saves with their language.
- Switching the language in-game (the Global Main Menu has no Language setting; the choice applies at the next start).
- A data-language/text-language split for upstream script-patch checks that test `getLanguage() == EN_ANY`: with a
  translation chosen they see the translation's language, exactly as with the removed Korean entries.
- `[translation.*]` in qualified form, and per-language ini keys.

## 13. Open for the user

1. GK1/GK2 Korean (SCI32) have upstream overlay entries this feature cannot serve. Remove them now (ruling 4, losing
   GK1/GK2 Korean until a SCI32 follow-up) or keep those two entries with the old `message.map` rule for SCI32 only?
2. Presets: **decided** (user, 2026-09-30) - render-target qualifiers in one map (U3.4); the ini key is the existing
   `render_target` (section 4).
3. Tag saves with their text language (a save-format change in both engines) - or leave them shared as section 9 says?
