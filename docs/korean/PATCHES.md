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
