# Upstream files modified by the Korean topics

New files never conflict and are not listed.

| Topic | Upstream file | Change | Why |
|---|---|---|---|
| `ko/00-svf-lib` | `configure` | one `append_var DEFINES` after the `RELEASE_BUILD` block | `svf_face` layout is the same in every unit |
| `ko/00-svf-lib` | `graphics/module.mk` | SVF objects and C-mode variables | build the renderer |
| `ko/00-svf-lib` | `test/module.mk` | four SVF suites (`svf_build.h`, `svf_face.h`, `svf_golden.h`, `svf_font.h`) | tests |
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
| `ko/03-sci` | `engines/sci/engine/script.{h,cpp}, seg_manager.{h,cpp}, state.cpp` | `Script::stringIdAtOffset`, `SegManager::stringKey`, bounded UTF-8 copy in `strncpy`, `SciEngine::translated/keyOf/strSplitHeap`, key parameter of `strSplitLanguage` | keys of script strings and tagged buffers for the translation lookups |
| `ko/03-sci` | `engines/sci/engine/kstring.cpp` | `kStrAt` byte access moved into `strAtByte`, UTF-8 `kStrAt`/`kStrLen`, `kFormat` translation, plural s and particles, buffer tags in `kStrCpy`/`kStrCat` | UTF-8 heap, particles |
| `ko/03-sci` | `engines/sci/engine/kgraphics.cpp, kmenu.cpp, graphics/menu.cpp` | translated text at display, control text, window title, menu entries | script strings are translated where they reach the screen |
| `ko/03-sci` | `engines/sci/graphics/drivers/gfxdriver.h, gfxdriver_intern.h, upscaled.cpp, init.cpp` | `setTextPlane` and compositing of the text plane; RGB request follows the text's render target | hi-res text in the 2x driver |
| `ko/03-sci` | `engines/sci/graphics/screen.{h,cpp}, paint16.cpp, cache.{h,cpp}` | text plane kept in step (clear under `putPixel`, invert, save/restore bits, clear on restore), `hiResFont` | hi-res text on screen |
| `ko/03-sci` | `engines/sci/graphics/text16.{h,cpp}` | UTF-8 decode in the width, draw and status loops, `getLongestUtf8`, font 1001 switch off for UTF-8 | layout and drawing by code point |
| `ko/03-sci` | `engines/sci/parser/vocabulary.{h,cpp}, grammar.cpp, said.cpp` | word table load, `lookupKorean`, UTF-8 safe case fold, arrangement, second parse tree and `said()` retry | Korean input to the game's `said()` specs |
| `ko/03-sci` | `engines/sci/event.{h,cpp}, metaengine.cpp, graphics/controls16.{h,cpp}` | Han/Yeong action and keymap, OS-IME surrogate key, `texteditChangeUtf8` | typing Korean |
