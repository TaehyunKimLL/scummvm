#!/bin/bash
# fbdump.sh — 프레임 단위로 ScummVM 의 모든 프레임버퍼를 덤프한다.
#
# 왜 캡처 대신 덤프인가:
#   xwd 스크린샷은 "몇 초 뒤"라는 시간 기준이라 게임 진행 속도에 흔들린다.
#   디스크 캐시가 따뜻한지 여부만으로도 캐릭터가 다른 걸음에 서 있고,
#   그것이 수만 픽셀 차이로 나타나 회귀 판정을 오염시킨다. (실측: 캐시가
#   빈 첫 런만 다른 두 런과 크게 달랐고, 두/세 번째 런은 비트 단위 일치)
#
#   프레임 번호는 결정적이다. N 번째 합성 프레임은 어느 빌드에서든 게임
#   로직상 같은 지점이므로, 두 빌드의 같은 프레임을 비교하면 타이밍
#   변수가 사라진다.
#
# 무엇을 덤프하나 (한 프레임에서 동시에):
#   vs0..vs3   각 VirtScreen 의 픽셀 (main/text/verb/banner)
#   text       _textSurface      - hi-res 텍스트 오버레이
#   alpha      _hiResText._coverage  - 알파 커버리지 (없으면 건너뜀)
#   pal        _currentPalette   - 팔레트 768 바이트
#   메타데이터 (w/h/pitch/topline/xstart) 는 meta.txt 에 남는다.
#
# 사용법:
#   fbdump.sh <출력dir> <타깃> <프레임번호들> [바이너리] [디스플레이]
#     fbdump.sh /tmp/D1 mi2-svfn 400,600,800
#     fbdump.sh /tmp/D2 mi2-svfn 400,600,800 /tmp/svm-master/scummvm :870
#
# 주의: 대상 바이너리는 -g 로 빌드돼 있어야 심볼이 보인다.
#       ~/games/dbgfiles.sh 로 필요한 파일만 -g -O0 재컴파일할 수 있다.

set -u

# Shared helpers: free-display allocation, window lookup, and cleanup that
# accumulates instead of replacing the EXIT trap.
. "$(dirname "$0")/xvfb.sh"
OUT=${1:?사용법: fbdump.sh <출력dir> <타깃> <프레임번호들> [바이너리] [디스플레이]}
TARGET=${2:?타깃 필요}
FRAMES=${3:?프레임 번호 (콤마구분)}
BIN=${4:-$HOME/src/scummvm/scummvm}
# A display number may be given, but the default is whatever is free: two runs
# sharing one X server capture each other's windows, and the result reads as a
# rendering difference.
DISP=${5:-}

SRC=$HOME/src/scummvm
mkdir -p "$OUT"

# ScummVM 은 종료 시 scummvm.ini 를 다시 쓴다. 원본을 건드리지 않도록
# 사본을 준다 (병렬 실행에서 원본이 0 바이트로 잘린 적이 있다).
MASTER_INI="$HOME/.config/scummvm/scummvm.ini"
INI="$OUT/scummvm.ini"
cp "$MASTER_INI" "$INI"

# Common::Encoding 은 런타임에 encoding.dat 이 있어야 레거시 코드페이지를
# 유니코드로 바꾼다. 없으면 경고 한 줄만 남기고 모든 CJK 글자가 U+FFFD 가
# 되어, 렌더링을 재는 덤프가 조용히 무의미해진다.
ENGDATA="$SRC/dists/engine-data"
if [ -f "$ENGDATA/encoding.dat" ]; then
	python3 - "$INI" "$ENGDATA" <<'PY'
import sys
ini, path = sys.argv[1], sys.argv[2]
s = open(ini).read()
if '[scummvm]' in s:
    s = s.replace('[scummvm]', '[scummvm]\nextrapath=%s' % path, 1)
else:
    s = '[scummvm]\nextrapath=%s\n\n' % path + s
open(ini, 'w').write(s)
PY
else
	echo "경고: $ENGDATA/encoding.dat 없음 - CJK 디코드가 실패한다" >&2
fi

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

# drawDirtyScreenParts() 의 마지막 지점 = 그 프레임의 모든 합성이 끝난 곳.
# The breakpoint is resolved by GDB from the symbol name, so nothing here
# depends on the working tree matching the binary under test.
echo "브레이크: ScummEngine::drawDirtyScreenParts (심볼로 해석)"

# 프레임 번호를 GDB 조건으로
COND=""
for f in ${FRAMES//,/ }; do
  [ -n "$COND" ] && COND="$COND || "
  COND="$COND\$fn == $f"
done

# Per-run file. A fixed name is overwritten by any concurrent or immediately
# following run - fbcompare.sh invokes this twice in a row, so one target ended
# up running the other's script and dumped nothing at all, which then read as
# "no difference".
# Start from a save when one exists. From the title screen the game needs far
# longer than the GDB timeout to reach a late frame, and the breakpoint then
# never fires - which looks identical to "the build draws nothing".
SLOTARG=""
for s in "$HOME/.local/share/scummvm/saves/$TARGET".s[0-9][0-9]; do
	[ -e "$s" ] || continue
	n=${s##*.s}
	SLOTARG="--save-slot=$((10#$n))"
	break
done

GDBFILE=$(mktemp /tmp/fbdump.XXXXXX.gdb)
cleanup_file "$GDBFILE"

cat > "$GDBFILE" <<GDBEOF
set pagination off
set confirm off
set breakpoint pending on
set \$fn = 0

# Break on the function by NAME, not on a line number scraped from the working
# tree: the control build is compiled from different source - our patch edits
# this very function - so the same tree line resolved to 558 in one binary and
# 555 in the other, and in the control it fell outside the function. The
# breakpoint never fired, the run produced zero dumps, and that reads exactly
# like "this build renders nothing".
#
# The entry of drawDirtyScreenParts() is the frame boundary: everything the
# previous frame composited is in place, and _currentPalette holds the palette
# that produced it.
break ScummEngine::drawDirtyScreenParts
commands
  silent
  set \$fn = \$fn + 1
  if ($COND)
    printf "FRAME %d room=%d\n", \$fn, this->_currentRoom

    # 각 VirtScreen
    set \$i = 0
    while \$i < 4
      set \$v = &this->_virtscr[\$i]
      if \$v->h > 0 && \$v->pixels != 0
        printf "  vs%d w=%d h=%d pitch=%d topline=%d xstart=%d bpp=%d\n", \\
          \$i, \$v->w, \$v->h, \$v->pitch, \$v->topline, \$v->xstart, \$v->format.bytesPerPixel
        eval "dump binary memory $OUT/f%d_vs%d.bin \$v->pixels (char*)\$v->pixels + \$v->pitch * \$v->h", \$fn, \$i
      end
      set \$i = \$i + 1
    end

    # hi-res 텍스트 오버레이
    if this->_textSurface.pixels != 0
      printf "  text w=%d h=%d pitch=%d bpp=%d\n", \\
        this->_textSurface.w, this->_textSurface.h, this->_textSurface.pitch, this->_textSurface.format.bytesPerPixel
      eval "dump binary memory $OUT/f%d_text.bin this->_textSurface.pixels (char*)this->_textSurface.pixels + this->_textSurface.pitch * this->_textSurface.h", \$fn
    end

    # 팔레트를 텍스트 서피스 바로 뒤에 뜬다. 뒤에 오는 항목이 실패하면
    # GDB 가 스크립트를 통째로 중단해 앞의 덤프까지 무의미해지므로, 확실한
    # 것부터 먼저 저장한다.
    eval "dump binary memory $OUT/f%d_pal.bin this->_currentPalette this->_currentPalette + 768", \$fn
  end
  cont
end

set args --config=$INI --gfx-mode=opengl --no-filtering --no-aspect-ratio --stretch-mode=pixel-perfect --scale-factor=1 $SLOTARG $TARGET
run
quit
GDBEOF

if [ -n "$DISP" ]; then
	Xvfb "$DISP" -screen 0 960x600x24 -nolisten tcp >/dev/null 2>&1 &
	XPID=$!
else
	xvfb_start 960 600 || exit 1
	XPID=$XVFB_PID
fi
sleep 2
cleanup_pid "$XPID"

# 인트로 스킵 (캡처와 동일한 키 시퀀스)
(
  prev=0
  for kv in Return@4 Escape@6 Escape@9 Escape@12 Escape@15 Escape@18 Escape@21 Escape@24; do
    key=${kv%@*}; at=${kv#*@}
    sleep $((at - prev)); prev=$at
    find_window "$DISP" | while read -r w; do
      DISPLAY=$DISP xdotool key --window "$w" "$key" 2>/dev/null
    done
  done
) &
KPID=$!

DISPLAY=$DISP timeout 300 gdb -batch -x "$GDBFILE" "$BIN" > "$OUT/gdb.txt" 2>&1
kill $KPID 2>/dev/null

grep -E "^FRAME|^  " "$OUT/gdb.txt" > "$OUT/meta.txt" 2>/dev/null
N=$(ls "$OUT"/*.bin 2>/dev/null | wc -l)
echo "덤프 $N 개 -> $OUT"

# Say why there is nothing, rather than leaving a silent zero that the caller
# reads as "no difference".
if [ "$N" -eq 0 ]; then
	if grep -q "Breakpoint 1, " "$OUT/gdb.txt" 2>/dev/null; then
		echo "  브레이크는 걸렸으나 덤프 실패 - $OUT/gdb.txt 의 Error 확인" >&2
	elif grep -q "Breakpoint 1 at" "$OUT/gdb.txt" 2>/dev/null; then
		echo "  브레이크 히트 0건: 프레임 $FRAME 도달 전 종료(타임아웃/세이브 없음)" >&2
		[ -z "$SLOTARG" ] && echo "  세이브가 없어 인트로부터 시작했다" >&2
	else
		echo "  브레이크포인트가 걸리지 않았다 - 디버그 심볼 확인" >&2
	fi
fi
sed -n '1,20p' "$OUT/meta.txt" 2>/dev/null
