# fonts-cjk.dat is used only on systems with plenty of memory
# The Korean build of it (Noto Sans KR only), kept in the svf-fonts submodule.
ifdef USE_FREETYPE2
DIST_FILES_LIST += dists/svf-fonts/cjk/fonts-cjk.dat
endif
