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
