# Upstream files modified by the Korean topics

New files never conflict and are not listed.

| Topic | Upstream file | Change | Why |
|---|---|---|---|
| `ko/00-svf-lib` | `configure` | one `append_var DEFINES` after the `RELEASE_BUILD` block | `svf_face` layout is the same in every unit |
| `ko/00-svf-lib` | `graphics/module.mk` | SVF objects and C-mode variables | build the renderer |
| `ko/00-svf-lib` | `test/module.mk` | five SVF suites (`svf_build.h`, `svf_face.h`, `svf_golden.h`, `svf_font.h`, `svf_aa.h`) | tests |
| `ko/00-svf-lib` | `LICENSES/COPYING.MIT`, `LICENSES/COPYING.OFL` | MIT section for `graphics/svf`; OFL note for the test fixture | notices |
| `ko/00-svf-lib` | `devtools/create_project/create_project.cpp` | three `SVF_*` entries in `setup.defines` | MSVC, Xcode, Code::Blocks builds need the same switches as `configure` |
| `ko/00-svf-lib` | `devtools/create_project/cmake.cpp` | one `add_definitions` line with the three `SVF_*` switches | the CMake generator writes its own define list |
| `ko/00-svf-lib` | `Makefile.common` | `%.o: %.c` rule in the no-dependency-tracking branch | out-of-tree builds with such a compiler create `graphics/svf/` before compiling the C files |
| `ko/01-core` | `common/language.h`, `common/language.cpp` | `getLanguageFolderCode`, `parseLanguageFolderCode` and their table | the ko/ folder of a translation |
| `ko/01-core` | `graphics/module.mk` | 12 `hires_text/` objects | build the hi-res text core |
| `ko/01-core` | `test/module.mk` | one glob, `test/graphics/hires_text_*.h` | tests |
| `ko/01-core` | `LICENSES/COPYING.OFL` | entry for `dists/engine-data/hires_text/NEODG16A.SVF` | notice for the shipped face |
| `ko/02-common-korean` | `common/keyboard.h` | `KEYCODE_HANGUL`, `KEYCODE_HANJA` before `KEYCODE_LAST` | the Han/Yeong key reaches engines |
| `ko/02-common-korean` | `backends/keymapper/hardware-input.cpp` | `HANGUL`, `HANJA` in `defaultKeys` | keymaps can bind the keys |
| `ko/02-common-korean` | `backends/events/sdl/sdl2-events.cpp`, `sdl3-events.cpp` | `SDL_SCANCODE_LANG1/2` cases | SDL delivers the keys |
| `ko/02-common-korean` | `common/module.mk` | 5 objects (`hangul`, `korean_stem`, `korean_vocab`, `language_folder`, `translation_file`) | build the Korean common code |
| `ko/02-common-korean` | `test/module.mk` | `TEST_LIBS += backends/keymapper/hardware-input.o` | the key name test |
| `ko/03-sci` | `Makefile.common, ports.mk` | `DIST_FILES_HIRES_TEXT(_MAPS)`, installed into `$(datadir)/hires_text/` by `install-data` | the engine's fallback map and face are found as `data:hires_text/...` |
| `ko/03-sci` | `test/module.mk` | SCI suites (`test/engines/sci/*.h`), `libsci.a` in `TEST_LIBS` | tests |
| `ko/03-sci` | `engines/sci/util.h` | `#ifdef CXXTEST_RUNNING` for the test friend | the SCI test build compiles (the runner defines the macro empty) |
| `ko/03-sci` | `engines/sci/module.mk` | 6 objects (`korean_input`, `textmode`, `utf8`, `engine/script_strings`, `graphics/hirestext`, `parser/korean_parser`) | build the new units |
| `ko/03-sci` | `engines/sci/graphics/scifont.{h,cpp}, fontsjis.{h,cpp}, fontkorean.{h,cpp}` | `GfxFont` interface takes `uint32` instead of `uint16` | a UTF-8 game passes code points; upstreamable |
| `ko/03-sci` | `engines/sci/sci.{h,cpp}` | text mode, language folder in SearchMan, script strings, fingerprint header, hi-res setup, `SciAction`, `getLanguage()` from the text mode | decide how text is held once; the translation's language |
| `ko/03-sci` | `engines/sci/detection.cpp` | `Korean` debug channel; `identifyGame` ignores `language=` when the folder has that translation; a `-ko` target per translation | run the release a translation was made for |
| `ko/03-sci` | `engines/sci/engine/script.{h,cpp}, seg_manager.{h,cpp}, state.cpp` | `Script::stringIdAtOffset`, `SegManager::stringKey`, `stringCapacity`, `strcpyBounded`, `Script::stringBytesAt`, bounded UTF-8 copy in `strncpy`, `SciEngine::translated/keyOf/strSplitHeap`, key parameter of `strSplitLanguage` | keys of script strings and tagged buffers for the translation lookups |
| `ko/03-sci` | `engines/sci/engine/kstring.cpp` | `kStrAt` byte access moved into `strAtByte`, UTF-8 `kStrAt` (rebuilds the string)/`kStrLen`, `kFormat` translation (formatted twice for the length diagnostic), plural s and particles, buffer tags in `kStrCpy`/`kStrCat` | UTF-8 heap, particles |
| `ko/03-sci` | `engines/sci/engine/kgraphics.cpp, kmenu.cpp, graphics/menu.cpp` | translated text at display, control text, window title, menu entries | script strings are translated where they reach the screen |
| `ko/03-sci` | `engines/sci/graphics/drivers/gfxdriver.h, gfxdriver_intern.h, upscaled.cpp, init.cpp, default.cpp` | `setTextPlane` and compositing of the text plane; RGB request follows the text's render target; default.cpp asks the backend for the target's pixel format (565, 555, 888) and logs the format | hi-res text in the 2x driver |
| `ko/03-sci` | `engines/sci/graphics/screen.{h,cpp}, paint16.cpp, cache.{h,cpp}` | text plane kept in step (clear under `putPixel`, invert, save/restore bits, clear on restore), `hiResFont` | hi-res text on screen |
| `ko/03-sci` | `engines/sci/graphics/text16.{h,cpp}` | UTF-8 decode in the width, draw and status loops, `getLongestUtf8`, font 1001 switch off for UTF-8 | layout and drawing by code point |
| `ko/03-sci` | `engines/sci/parser/vocabulary.{h,cpp}, grammar.cpp, said.cpp` | word table load, `lookupKorean`, UTF-8 safe case fold, arrangement, second parse tree and `said()` retry | Korean input to the game's `said()` specs |
| `ko/03-sci` | `engines/sci/event.{h,cpp}, metaengine.cpp, graphics/controls16.{h,cpp}` | Han/Yeong action and keymap, OS-IME surrogate key, `texteditChangeUtf8` | typing Korean |
| `ko/03-sci` | `engines/sci/engine/kfile.cpp` | `kGetSaveFiles` cuts a UTF-8 save name on a code point boundary | no dangling lead byte in the restore list |
| `ko/03-sci` | `engines/sci/engine/script.cpp` | (P4) SCI1.1 strings start where `ScriptStrings::sci11StringsStart` says, for UTF-8 games | the translation's string ids on a heap whose strings open with a NUL pair |
| `ko/03-sci` | `engines/sci/parser/vocabulary.{h,cpp}` | (P4) `Vocabulary::koreanAnswerLine` | a Korean answer as English words for the answer rooms |
| `ko/03-sci` | `engines/sci/engine/kstring.cpp` | (P4) `koreanAnswerView` in `kStrAt` reads and `kStrLen` | QFG2 room 199, QFG1 EGA rooms 22 and 29 search the typed text |
| `ko/03-sci` | `engines/sci/engine/seg_manager.{h,cpp}` | (P4) `growDynmem`, used by `strncpy` and `strcpyBounded`; a same-block source is copied out | a dynamic block grows for a longer translation instead of cutting it |
| `ko/03-sci` | `engines/sci/graphics/cache.cpp` | (P4) `hiResFont` picks the font set of the font resource drawn (`& 0x7ff` as `GfxFontFromResource`) | lsl1sci's font id 2107 draws as font 59 |
| `ko/05-scumm` | `test/module.mk` | SCUMM suites (`test/engines/scumm/*.h`), `libscumm.a` in `TEST_LIBS` | tests |
| `ko/05-scumm` | `engines/scumm/module.mk` | 6 objects (`korean`, `korean_sentence`, `korean_text`, `ksx1001_table`, `saved_text`, `trs_bundle`) | build the new units |
| `ko/05-scumm` | `engines/scumm/scumm.h`, `scumm_v2.h` | `_koreanUtf8`, `hasTranslationBundle()`, the UTF-8 text helpers (`nextCJKChar`, `koreanCharsetGlyph`, `cjkTrailBytes`, `koreanCharStart`, `koreanBreakableAt`, `koreanCharBefore`, `copyTranslatedText`, `koreanRefusalGlyph`), `transcodeLanguageBundle`, `convertLoadedText`; `drawKoreanSentence` | Korean text is UTF-8 inside the engine (v1-v6) |
| `ko/05-scumm` | `engines/scumm/charset.cpp` | `loadKorFont` sets `_koreanUtf8` (version < 7); `get2byteCharPtr` takes a code point through the KS X 1001 table and refuses what `korean.fnt` lacks; UTF-8 steps in `getStringWidth` and `addLinebreaks` (same Korean break rule, positions from the character start) | measure and draw UTF-8 |
| `ko/05-scumm` | `engines/scumm/string.cpp` | `nextCJKChar` at the two byte-combining sites (dialog, `drawString`), `drawString` buffer 270 -> 512, wrap prediction by character length, particles from the shown name, `loadLanguageBundle` tries `ko.trs` and converts a CP949 body once, the four copies of `translateText` through `copyTranslatedText`, the sentence line hook in `drawSentence` | the boundary and the text paths |
| `ko/05-scumm` | `engines/scumm/saveload.cpp` | `TENC` trailer written for a Korean v1-v6 target, read on load; an older save is converted | saves hold UTF-8 |
| `ko/05-scumm` | `engines/scumm/detection_internal.h`, `scumm.cpp` | `ko.trs` and a `ko/` folder detect a Korean translation; the `ko` folder joins the search path | the language folder of a translation |
