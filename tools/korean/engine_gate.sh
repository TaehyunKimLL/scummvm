#!/usr/bin/env bash
# agentws gate for project scummvm-engine (scummvm-korean repo, jamo branch; gate line:
# bash tools/korean/engine_gate.sh, run from the checkout under test): build the Linux test
# configuration from the checkout in the current directory and run `make test`.
# Pass = the only failing assertions are the known ones listed below
# (HANDOVER.md: hires_text_ttf_fit.h:493). Exit 0 pass, 1 test failure or
# build failure, 2 setup error.
#
# The object dir lives outside the checkout, keyed by the checkout path, so a
# gate run never writes into the worktree the integrator merges.
set -u
src=$(pwd)
[ -x "$src/configure" ] || { echo "engine_gate: no ./configure in $src" >&2; exit 2; }
# The fonts are the svf-fonts submodule (dists/svf-fonts); a fresh worktree has it empty. The repo's
# submodule URL may point at a local checkout (offline), hence protocol.file.allow.
if [ -f "$src/.gitmodules" ] && [ ! -e "$src/dists/svf-fonts/README.md" ]; then
  git -C "$src" -c protocol.file.allow=always submodule update --init >/dev/null 2>&1 \
    || { echo "engine_gate: git submodule update --init failed in $src" >&2; exit 2; }
fi
key=$(printf %s "$src" | sha1sum | cut -c1-10)
obj="/tmp/agentws-engine-gate/$key"
mkdir -p "$obj" || exit 2
acct_home=$(getent passwd "$(id -un)" | cut -d: -f6)
export PATH="$acct_home/.local/sysroot/usr/bin:$PATH"
export LD_LIBRARY_PATH="$acct_home/.local/sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
jobs=${ENGINE_GATE_JOBS:-24}
known=("test/graphics/hires_text_ttf_fit.h:493")

cd "$obj" || exit 2
if [ ! -f config.mk ] || ! grep -q "^srcdir = $src\$" Makefile 2>/dev/null; then
  "$src/configure" --disable-all-engines --enable-engine=sci,scumm,agi \
    --disable-engine=sci32,scumm_7_8,he --enable-debug-socket \
    --with-sdl-prefix="$acct_home/.local/sysroot/usr" > configure.log 2>&1 \
    || { tail -20 configure.log; echo "engine_gate: configure failed" >&2; exit 1; }
fi
echo "engine_gate: src $src obj $obj jobs $jobs"
t0=$(date +%s)
make -j"$jobs" > build.log 2>&1 || { grep -E "error|Error" build.log | head -20; echo "engine_gate: build failed" >&2; exit 1; }
make -j"$jobs" test > test.log 2>&1
# cxxtest prints "OK!" at the end of the dots line, "Failed N and Skipped M of K tests" on its own line.
summary=$(grep -oE "(Failed [0-9]+ and Skipped [0-9]+ of [0-9]+ tests|OK!)" test.log | tail -1)
fails=$(grep -oE "[^ ]+\.h:[0-9]+: Error" test.log | sed -E 's#^.*/(test/[^:]+:[0-9]+): Error#\1#' | sort -u)
unknown=""
while IFS= read -r f; do
  [ -z "$f" ] && continue
  ok=0; for k in "${known[@]}"; do [ "$f" = "$k" ] && ok=1; done
  [ $ok = 1 ] || unknown="$unknown $f"
done <<< "$fails"
echo "engine_gate: $summary ($(( $(date +%s) - t0 )) s); known failures: ${known[*]}"
if [ -z "$summary" ]; then tail -30 test.log; echo "engine_gate: no test summary" >&2; exit 1; fi
if [ -n "$unknown" ]; then echo "engine_gate: RED, new failures:$unknown" >&2; exit 1; fi
echo "engine_gate: GREEN"
