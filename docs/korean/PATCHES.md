# Upstream files modified by the Korean topics

New files never conflict and are not listed.

| Topic | Upstream file | Change | Why |
|---|---|---|---|
| `ko/00-svf-lib` | `configure` | one `append_var DEFINES` after the `RELEASE_BUILD` block | `svf_face` layout is the same in every unit |
| `ko/00-svf-lib` | `graphics/module.mk` | SVF objects and C-mode variables | build the renderer |
| `ko/00-svf-lib` | `test/module.mk` | four SVF suites (`svf_build.h`, `svf_face.h`, `svf_golden.h`, `svf_font.h`) | tests |
| `ko/00-svf-lib` | `LICENSES/COPYING.MIT`, `LICENSES/COPYING.OFL` | MIT section for `graphics/svf`; OFL note for the test fixture | notices |
