#!/bin/bash
# mksave.sh - 재현 테스트용 세이브를 만든다.
#
#   mksave.sh <타겟> [디스플레이] [진행시간초] [슬롯]
#   BIN=/tmp/svm-s9 mksave.sh i3-multi-hr :430 240 1
#
# ScummVM 은 Alt+숫자 를 퀵세이브로 받는다(engines/scumm/input.cpp:137).
# GMM 대화상자를 거치지 않아 xdotool 로 다루기 쉽고, canSaveGameStateCurrently()
# 가 참일 때만 발동하므로 컷신 중에는 저절로 무시된다. 즉 조작 가능해진
# 시점이 자동으로 골라진다 — autosave_period 를 줄일 필요가 없다.
#
# 그 게이트 덕분에 **세이브 파일이 생겼다는 사실 자체가 게임이 조작 가능한
# 상태였다는 증거**다. 픽셀을 세어 "방 안인가" 를 추측하지 않아도 된다.
#
# 슬롯 0 은 autosave 전용이라 코드가 10 으로 돌린다. 여기서는 1 을 쓴다.
set -u

. "$(dirname "$0")/xvfb.sh"
TARGET=${1:?사용법: mksave.sh <타겟> [디스플레이] [진행시간초] [슬롯]}
DISP=${2:-}
RUN=${3:-240}
SLOT=${4:-1}
BIN=${BIN:-$HOME/src/scummvm/scummvm}
SAVES=$HOME/.local/share/scummvm/saves

export SDL_AUDIODRIVER=dummy

# Find the game window without depending on the executable's name: SDL derives
# the window class from argv[0], so a binary copied to /tmp/svm-work has class
# "svm-work" and a search for "scummvm" finds nothing.
find_window() {
	local disp=$1
	DISPLAY=$disp xwininfo -root -children 2>/dev/null | \
		awk '/^     0x/ {print $1; exit}'
}
export LD_LIBRARY_PATH=$HOME/.local/sysroot/usr/lib/x86_64-linux-gnu
export PATH=$HOME/.local/sysroot/usr/bin:$PATH

# encoding.dat 가 없으면 모든 CJK 문자가 U+FFFD 로 디코드되고, 그러면 세이브는
# 만들어져도 그 세이브로 찍은 캡처가 전부 두부가 된다.
D=$(mktemp -d /tmp/mksave.XXXXXX)
cp "$HOME/.config/scummvm/scummvm.ini" "$D/t.ini"
python3 - "$D/t.ini" "$HOME/src/scummvm/dists/engine-data" <<'PY'
import sys
ini, engdata = sys.argv[1], sys.argv[2]
s = open(ini).read()
if '[scummvm]' in s:
    s = s.replace('[scummvm]', '[scummvm]' + chr(10) + 'extrapath=' + engdata, 1)
else:
    s = '[scummvm]' + chr(10) + 'extrapath=' + engdata + chr(10) * 2 + s
open(ini, 'w').write(s)
PY

# Slots are two digits: .s01, .s10. The old .s0$SLOT form silently missed
# slot 10 entirely.
SAVEFILE=$(printf '%s/%s.s%02d' "$SAVES" "$TARGET" "$SLOT")

# A save left by an earlier run must not count as this one succeeding - the
# whole claim of this script is that a save proves the game became
# interactive. Stamp the start and compare mtimes.
STAMP=$(mktemp)
cleanup_file "$STAMP"

save_is_new() {
	[ -f "$SAVEFILE" ] || return 1
	[ "$SAVEFILE" -nt "$STAMP" ]
}

# 3배 타깃의 창은 960x600 이라 같은 크기의 Xvfb 에는 들어가지 않는다. 게임이
# 조용히 축소 모드로 떨어져 엉뚱한 해상도를 저장하게 되므로 넉넉히 잡는다.
if [ -n "$DISP" ]; then
	Xvfb "$DISP" -screen 0 1600x1000x24 -nolisten tcp >/dev/null 2>&1 &
	XPID=$!
else
	. "$(dirname "$0")/xvfb.sh"
	xvfb_start 1600 1000 || exit 1
	XPID=$XVFB_PID
fi
trap 'kill $SPID $XPID 2>/dev/null; rm -rf "$D"' EXIT
sleep 2

DISPLAY=$DISP "$BIN" --config="$D/t.ini" \
  --gfx-mode=opengl --no-filtering \
  --no-aspect-ratio --stretch-mode=pixel-perfect --scale-factor=1 \
  "$TARGET" > "/tmp/mksave_$TARGET.log" 2>&1 &
SPID=$!
sleep 8

WID=$(find_window "$DISP")
if [ -z "$WID" ]; then
  echo "$TARGET: 창을 찾지 못했다"
  exit 1
fi
DISPLAY=$DISP xdotool windowfocus --sync "$WID" 2>/dev/null

# 컷신을 넘기면서 주기적으로 퀵세이브를 시도한다. 저장이 불가능한
# 동안에는 아무 일도 일어나지 않으므로 계속 두드려도 안전하다.
# 조작 가능해지는 시점과 "방 안에 들어간" 시점은 다르다. Indy3 는 타이틀
# 화면에서도 canSaveGameStateCurrently() 가 참이라 5초 만에 저장이 되는데,
# 그 세이브로 캡처하면 동사줄도 대사도 없다. 그래서 첫 저장 시점에서 멈추지
# 않고 RUN 초 동안 계속 덮어쓴다 — 마지막 저장이 가장 진행된 상태다.
saved=""
for i in $(seq 1 $((RUN / 5))); do
  DISPLAY=$DISP xdotool key --window "$WID" Escape 2>/dev/null
  DISPLAY=$DISP xdotool key --window "$WID" period 2>/dev/null
  sleep 1
  # 방 안에서는 클릭이 캐릭터를 움직여 실제 게임 상태를 만든다.
  DISPLAY=$DISP xdotool mousemove --window "$WID" 160 110 click 1 2>/dev/null
  sleep 2
  DISPLAY=$DISP xdotool key --window "$WID" "alt+$SLOT" 2>/dev/null
  sleep 2

  if [ -z "$saved" ] && save_is_new; then
    saved=$((i * 5))
    echo "$TARGET: ${saved}초 시점에 첫 저장 (조작 가능 상태 도달)"
  fi
done

sleep 2
kill $SPID 2>/dev/null; sleep 2

# Wait for the process so a quicksave still being flushed is not read as a
# truncated file.
wait $SPID 2>/dev/null

if save_is_new; then
  ls -l "$SAVEFILE" | awk '{print "  세이브:", $5"B", $9}'
elif [ -f "$SAVEFILE" ]; then
  echo "  *** 저장 안 됨 — 기존 세이브만 있다(이번 실행이 만든 것이 아니다) ***"
  ls -l "$SAVEFILE" | awk '{print "      기존:", $5"B", $9}'
  exit 1
else
  echo "  *** 저장 안 됨 — 게임이 ${RUN}초 안에 조작 가능해지지 않았다 ***"
  echo "      로그: /tmp/mksave_$TARGET.log"
  exit 1
fi
