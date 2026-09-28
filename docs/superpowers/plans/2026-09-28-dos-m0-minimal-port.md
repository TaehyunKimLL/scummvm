# DOS 포트 M0 — 스파이크와 최소 포트 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** SCI 엔진만 넣은 ScummVM 이 DOSBox-X 와 DOSBox Staging 에서 KQ1 타이틀 화면을 320×200 CLUT8 로 띄우고,
COM 포트 디버그 채널로 조작·화면 덤프가 되며, 덤프가 리눅스 빌드와 픽셀 단위로 같다.

**Architecture:** 전용 백엔드 `backends/platform/dos` (`OSystem_DOS`, ModularBackend). SDL3 는 비디오 모드·서피스·
키보드·마우스에만 쓴다. 사운드는 M0 에서 `NullMixerManager`, 타이머는 null 백엔드처럼 `pollEvent()` 에서
`checkTimers()` 를 부르는 협력형. 스파이크 2건(타이머 방식, VRAM 전송)은 M3/M2 결정을 위한 증거를 남긴다.

**Tech Stack:** DJGPP GCC 12.2 (i586-pc-msdosdjgpp), SDL3 main (정적, `~/opt/sdl3-dos`), CWSDPMI,
DOSBox-X 2026.08.31, DOSBox Staging 0.83, CxxTest (`make test`), Python 3 하네스.

**Spec:** `docs/superpowers/specs/2026-09-28-scummvm-dos-sdl3-design.md`

## Global Constraints

- 브랜치 `dos-port`, 워크트리 `~/work/scummvm/dos`. 하네스 코드는 하네스 저장소 `~/work/scummvm` (브랜치 `master`) 의 `harness/`.
- 크로스 빌드 환경: `source ~/opt/dos-dev/env.sh` (DJGPP 를 PATH 에, `PKG_CONFIG_LIBDIR=$SDL3_DOS/lib/pkgconfig`).
- 엔진 코드(`engines/`)는 고치지 않는다.
- 공통 코드 변경(`gui/`, `configure`, `test/`)은 DOS 백엔드 커밋과 따로 커밋한다.
- DOS 쪽 파일 이름은 8.3: 배포·하네스가 DOS 안에 만드는 파일, 덤프 접두어(1자 + 디렉터리), 설정 `SCUMMVM.INI`.
- 커밋 메시지 끝:
  ```
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01429qJytiMpcCADjppM2jV4
  ```
- 헤드리스 실행: `xvfb-run -a -s "-screen 0 1024x768x24"`, `SDL_AUDIODRIVER=dummy`.
  DOSBox-X 인자 `-conf <f> -nopromptfolder -fastlaunch`, Staging 인자 `--noprimaryconf --conf <f>`.

## 파일 구조

| 파일 | 책임 |
|---|---|
| `harness/dos/spikes/timer/spike_timer.c` | 스파이크 1: RTC IRQ8 1024Hz 와 PIT 재설정이 SDL3 시계와 어떻게 어울리는지 |
| `harness/dos/spikes/blit/spike_blit.c` | 스파이크 2: direct-FB 전체/부분 전송 시간 |
| `harness/dos/spikes/RESULTS.md` | 스파이크 결론 (M2/M3 계획의 입력) |
| `configure` | `*-msdosdjgpp` 호스트, `dos` 백엔드 |
| `backends/platform/dos/module.mk` | 백엔드 오브젝트 목록 |
| `backends/platform/dos/dos.h`, `dos.cpp` | `OSystem_DOS`: 수명, 시간, 로그, 매니저 조립, `main()` |
| `backends/platform/dos/build-dos.sh` | configure 인자와 빌드, `dist/` 에 EXE + CWSDPMI |
| `backends/platform/dos/dos-modes.h` | 순수 로직(헤더 전용): SDL 모드 목록 → 정확 일치 모드, 지원 포맷 |
| `backends/platform/dos/soft-cursor.h` | 순수 로직(헤더 전용): 밑그림을 저장하는 소프트웨어 커서 |
| `backends/graphics/dos/dos-graphics.h`, `.cpp` | `DosGraphicsManager`: 게임 화면, 팔레트, dirty rect, 커서, 오버레이 스텁 |
| `backends/events/dos/dos-events.h`, `.cpp` | `DosEventSource`: SDL3 이벤트 → `Common::Event` |
| `gui/debugsocket-dosuart.h`, `.cpp` | 16550 UART 폴링 전송 |
| `gui/debugsocket.h`, `.cpp` | `DEBUGSOCKET_DOSCOM` 전송 분기 |
| `test/backends/dos_modes.h`, `dos_soft_cursor.h` | 리눅스 `make test` 단위 테스트 |
| `harness/i18n/scigame.py` | TCP 연결(바이트 간 2ms) 추가 |
| `harness/dos/dosgame.py` | DOSBox 설정 생성, 실행, TCP 로 `SciGame` 연결 |
| `harness/dos/m0_accept.py` | M0 수락 테스트: 두 에뮬레이터 × KQ1 타이틀 덤프 = 리눅스 덤프 |

---

### Task 1: 스파이크 — 타이머 방식 (RTC IRQ8 vs PIT)

SDL3 DOS 의 `SDL_GetTicks`/`SDL_Delay` 는 DJGPP `uclock()` 을 쓴다. `uclock()` 은 PIT 채널 0 이 기본 주기로 돈다고
가정하므로 PIT 을 1kHz 로 바꾸면 SDL 시계가 틀어질 수 있다. RTC 주기 인터럽트(IRQ8, 1024Hz)는 PIT 을 건드리지 않는다.
이 태스크는 두 가지를 재고 결론을 남긴다. 코드는 버린다(증거로만 커밋).

**Files:**
- Create: `harness/dos/spikes/timer/spike_timer.c`, `harness/dos/spikes/timer/run.sh`
- Create: `harness/dos/spikes/RESULTS.md`

**Interfaces:**
- Consumes: `~/opt/dos-dev/env.sh`, `~/opt/dos-dev/smoke/smoke.conf` 형식
- Produces: `RESULTS.md` 의 "타이머" 절 — M3 계획이 IRQ 소스(RTC 또는 PIT)를 고르는 근거

- [ ] **Step 1: 스파이크 프로그램 작성**

`harness/dos/spikes/timer/spike_timer.c`:

```c
/* Spike: does an RTC (IRQ8) 1024 Hz handler keep time with SDL3's clock,
 * and what does reprogramming the PIT to 1 kHz do to SDL_GetTicks()?
 * Writes TIMER.LOG. Throwaway. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <crt0.h>
#include <sys/farptr.h>
#include <stdio.h>

int _crt0_startup_flags = _CRT0_FLAG_LOCK_MEMORY;

static volatile unsigned long rtc_ticks;
static volatile unsigned long guarded;      /* touched by ISR and main under the cli lock */
static volatile int torn;                   /* ISR saw a half-updated pair */
static volatile unsigned long pair_a, pair_b;
static _go32_dpmi_seginfo old_rtc, new_rtc;

static void rtc_isr(void) {
	rtc_ticks++;
	if (pair_a != pair_b)
		torn = 1;
	guarded++;
	outportb(0x70, 0x0C);                   /* read C to re-arm the periodic interrupt */
	(void)inportb(0x71);
	outportb(0xA0, 0x20);                   /* EOI slave, then master */
	outportb(0x20, 0x20);
}
static void rtc_isr_end(void) {}

static void rtc_install(void) {
	_go32_dpmi_lock_code(rtc_isr, (char *)rtc_isr_end - (char *)rtc_isr);
	_go32_dpmi_get_protected_mode_interrupt_vector(0x70, &old_rtc);
	new_rtc.pm_offset = (unsigned long)rtc_isr;
	new_rtc.pm_selector = _go32_my_cs();
	_go32_dpmi_allocate_iret_wrapper(&new_rtc);
	disable();
	_go32_dpmi_set_protected_mode_interrupt_vector(0x70, &new_rtc);
	outportb(0x70, 0x8A);                   /* reg A, NMI off */
	unsigned char a = inportb(0x71);
	outportb(0x70, 0x8A);
	outportb(0x71, (a & 0xF0) | 0x06);      /* rate 6 = 1024 Hz */
	outportb(0x70, 0x8B);
	unsigned char b = inportb(0x71);
	outportb(0x70, 0x8B);
	outportb(0x71, b | 0x40);               /* PIE on */
	outportb(0x70, 0x0C);
	(void)inportb(0x71);
	outportb(0xA1, inportb(0xA1) & ~0x01);  /* unmask IRQ8 */
	outportb(0x21, inportb(0x21) & ~0x04);  /* and the cascade */
	enable();
}

static void rtc_remove(void) {
	disable();
	outportb(0x70, 0x8B);
	unsigned char b = inportb(0x71);
	outportb(0x70, 0x8B);
	outportb(0x71, b & ~0x40);
	_go32_dpmi_set_protected_mode_interrupt_vector(0x70, &old_rtc);
	enable();
	_go32_dpmi_free_iret_wrapper(&new_rtc);
}

static unsigned long bios_ticks(void) { return _farpeekl(_dos_ds, 0x46C); }

static void pit_set(unsigned divisor) {
	disable();
	outportb(0x43, 0x36);
	outportb(0x40, divisor & 0xFF);
	outportb(0x40, divisor >> 8);
	enable();
}

/* Run the main loop for ms (by SDL's clock), hammering the cli lock. */
static void run(FILE *lg, const char *label, Uint32 ms) {
	unsigned long r0 = rtc_ticks, b0 = bios_ticks();
	Uint64 s0 = SDL_GetTicks();
	uclock_t u0 = uclock();
	while (SDL_GetTicks() - s0 < ms) {
		for (int i = 0; i < 1000; i++) {
			disable();                      /* the Common::Mutex candidate */
			pair_a++;
			pair_b++;
			enable();
		}
		SDL_PumpEvents();
		SDL_Delay(1);
	}
	fprintf(lg, "%s: sdl=%llu ms  rtc=%lu (expect %lu)  bios=%lu (expect %lu)  uclock=%llu ms  torn=%d\n",
		label, (unsigned long long)(SDL_GetTicks() - s0),
		rtc_ticks - r0, (unsigned long)(ms * 1024UL / 1000UL),
		bios_ticks() - b0, (unsigned long)(ms * 182UL / 10000UL),
		(unsigned long long)((uclock() - u0) * 1000ULL / UCLOCKS_PER_SEC), torn);
}

int main(int argc, char **argv) {
	(void)argc; (void)argv;
	FILE *lg = fopen("TIMER.LOG", "w");
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
	rtc_install();
	run(lg, "A rtc+default-pit", 3000);
	pit_set(1193);                          /* 1000 Hz, no IRQ0 handler of ours */
	run(lg, "B rtc+pit-1kHz   ", 3000);
	pit_set(0);                             /* back to 18.2 Hz */
	run(lg, "C rtc+pit-restored", 1000);
	rtc_remove();
	fprintf(lg, "done\n");
	fclose(lg);
	SDL_Quit();
	return 0;
}
```

- [ ] **Step 2: 실행 스크립트 작성**

`harness/dos/spikes/timer/run.sh`:

```bash
#!/bin/bash
# Build the timer spike and run it headless in both emulators; logs land in this directory.
set -e
source ~/opt/dos-dev/env.sh
cd "$(dirname "$0")"
i586-pc-msdosdjgpp-gcc -O2 -std=gnu99 -I"$SDL3_DOS/include" spike_timer.c -o SPTIMER.EXE -L"$SDL3_DOS/lib" -lSDL3 -lm
cp "$CWSDPMI_EXE" .
for emu in x staging; do
	cat > t-$emu.conf <<EOF
[dosbox]
machine = svga_s3
memsize = 16
[cpu]
cycles = 60000
[autoexec]
mount c .
c:
SPTIMER.EXE
exit
EOF
	rm -f TIMER.LOG
	if [ $emu = x ]; then
		SDL_AUDIODRIVER=dummy timeout 90 xvfb-run -a -s "-screen 0 1024x768x24" dosbox-x -conf t-$emu.conf -nopromptfolder -fastlaunch >/dev/null 2>&1
	else
		SDL_AUDIODRIVER=dummy timeout 90 xvfb-run -a -s "-screen 0 1024x768x24" "$DOSBOX_STAGING" --noprimaryconf --conf t-$emu.conf >/dev/null 2>&1
	fi
	mv TIMER.LOG TIMER-$emu.LOG
	echo "== $emu"; cat TIMER-$emu.LOG
done
```

- [ ] **Step 3: 실행**

Run: `chmod +x harness/dos/spikes/timer/run.sh && harness/dos/spikes/timer/run.sh`
Expected: 두 에뮬레이터 각각 A/B/C 세 줄과 `done`. 읽는 법:
- A 의 `rtc` 가 3072 ±2% 면 RTC 1024Hz 가 동작한다. `torn=0` 이면 `cli` 잠금이 ISR 에 대해 상호 배제된다.
- B 의 `sdl` 과 `uclock` 이 3000 에서 크게 벗어나거나 `bios` 가 55 배로 뛰면 PIT 재설정이 SDL 시계를 깬다.

- [ ] **Step 4: 결론 기록**

`harness/dos/spikes/RESULTS.md` 를 만들고 "## 타이머" 절에 두 에뮬레이터의 A/B/C 줄을 그대로 붙이고,
다음 셋 중 하나를 결론으로 적는다: "RTC IRQ8 채택" / "PIT 채택 (SDL 시계 영향 없음)" / "둘 다 불가 → 협력형".
RTC 가 동작하면 RTC 를 채택한다 (PIT 을 건드리지 않으므로 SDL·BIOS 와 충돌이 없다).

- [ ] **Step 5: Commit** (하네스 저장소)

```bash
cd ~/work/scummvm
git add harness/dos/spikes/timer harness/dos/spikes/RESULTS.md
git commit -m "harness/dos: timer spike, RTC IRQ8 against SDL3's uclock clock"
```

---

### Task 2: 스파이크 — direct-FB 전송 시간

코드로는 direct-FB 뱅크 경로가 사각형만 보낸다 (`SDL_dosframebuffer.c` multibank). 전체와 부분 전송의 비용 비를 잰다.
에뮬레이터의 절대값은 의미가 없고, 비율과 "부분 전송이 실제로 싸다"는 것만 본다.

**Files:**
- Create: `harness/dos/spikes/blit/spike_blit.c`, `harness/dos/spikes/blit/run.sh`
- Modify: `harness/dos/spikes/RESULTS.md`

**Interfaces:**
- Produces: `RESULTS.md` "## 전송" 절 — M2 계획의 dirty rect 정책 근거

- [ ] **Step 1: 스파이크 프로그램 작성**

`harness/dos/spikes/blit/spike_blit.c`:

```c
/* Spike: time full-frame vs small-rect updates through SDL3's direct-FB
 * path at 640x400 (and 640x480) for INDEX8 and RGB565. Writes BLIT.LOG. Throwaway. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>

static int pick(int w, int h, SDL_PixelFormat f, SDL_DisplayMode *out) {
	int n = 0, ok = 0;
	SDL_DisplayMode **m = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &n);
	for (int i = 0; i < n; i++)
		if (m[i]->w == w && m[i]->h == h && m[i]->format == f) { *out = *m[i]; ok = 1; break; }
	SDL_free(m);
	return ok;
}

static void measure(FILE *lg, SDL_Window *win, int w, int h, SDL_PixelFormat f) {
	SDL_DisplayMode mode;
	if (!pick(w, h, f, &mode)) { fprintf(lg, "%dx%d %s: no mode\n", w, h, SDL_GetPixelFormatName(f)); return; }
	SDL_SetWindowFullscreenMode(win, &mode);
	SDL_SetWindowFullscreen(win, true);
	SDL_SyncWindow(win);
	SDL_Surface *s = SDL_GetWindowSurface(win);
	SDL_FillSurfaceRect(s, NULL, 0x1234);
	const int frames = 60;
	Uint64 t0 = SDL_GetPerformanceCounter();
	for (int i = 0; i < frames; i++) SDL_UpdateWindowSurface(win);
	Uint64 t1 = SDL_GetPerformanceCounter();
	SDL_Rect r[4] = { {0, 0, 64, 32}, {200, 100, 64, 32}, {400, 200, 64, 32}, {560, 360, 64, 32} };
	for (int i = 0; i < frames; i++) SDL_UpdateWindowSurfaceRects(win, r, 4);
	Uint64 t2 = SDL_GetPerformanceCounter();
	double f_us = (double)(t1 - t0) * 1e6 / SDL_GetPerformanceFrequency() / frames;
	double p_us = (double)(t2 - t1) * 1e6 / SDL_GetPerformanceFrequency() / frames;
	fprintf(lg, "%dx%d %s: full %.0f us/frame, 4x(64x32) %.0f us/frame, ratio %.1f\n",
		w, h, SDL_GetPixelFormatName(f), f_us, p_us, f_us / (p_us > 0 ? p_us : 1));
}

int main(int argc, char **argv) {
	(void)argc; (void)argv;
	FILE *lg = fopen("BLIT.LOG", "w");
	SDL_SetHint(SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER, "1");
	SDL_Init(SDL_INIT_VIDEO);
	SDL_Window *win = SDL_CreateWindow("blit", 640, 400, 0);
	measure(lg, win, 640, 400, SDL_PIXELFORMAT_INDEX8);
	measure(lg, win, 640, 400, SDL_PIXELFORMAT_RGB565);
	measure(lg, win, 640, 480, SDL_PIXELFORMAT_RGB565);
	measure(lg, win, 640, 400, SDL_PIXELFORMAT_XRGB8888);
	fprintf(lg, "done\n");
	fclose(lg);
	SDL_Quit();
	return 0;
}
```

- [ ] **Step 2: 실행 스크립트 작성**

`harness/dos/spikes/blit/run.sh` 는 Task 1 의 `run.sh` 와 같은 구조로 쓴다 — 바뀌는 곳만:
`spike_timer.c`/`SPTIMER.EXE`/`TIMER.LOG` → `spike_blit.c`/`SPBLIT.EXE`/`BLIT.LOG`, conf 이름 `b-$emu.conf`.
전체 내용:

```bash
#!/bin/bash
# Build the blit spike and run it headless in both emulators; logs land in this directory.
set -e
source ~/opt/dos-dev/env.sh
cd "$(dirname "$0")"
i586-pc-msdosdjgpp-gcc -O2 -std=gnu99 -I"$SDL3_DOS/include" spike_blit.c -o SPBLIT.EXE -L"$SDL3_DOS/lib" -lSDL3 -lm
cp "$CWSDPMI_EXE" .
for emu in x staging; do
	cat > b-$emu.conf <<EOF
[dosbox]
machine = svga_s3
memsize = 16
[cpu]
cycles = 60000
[autoexec]
mount c .
c:
SPBLIT.EXE
exit
EOF
	rm -f BLIT.LOG
	if [ $emu = x ]; then
		SDL_AUDIODRIVER=dummy timeout 90 xvfb-run -a -s "-screen 0 1024x768x24" dosbox-x -conf b-$emu.conf -nopromptfolder -fastlaunch >/dev/null 2>&1
	else
		SDL_AUDIODRIVER=dummy timeout 90 xvfb-run -a -s "-screen 0 1024x768x24" "$DOSBOX_STAGING" --noprimaryconf --conf b-$emu.conf >/dev/null 2>&1
	fi
	mv BLIT.LOG BLIT-$emu.LOG
	echo "== $emu"; cat BLIT-$emu.LOG
done
```

- [ ] **Step 3: 실행**

Run: `chmod +x harness/dos/spikes/blit/run.sh && harness/dos/spikes/blit/run.sh`
Expected: 모드마다 한 줄. Staging 은 `640x400 RGB565: no mode` (측정된 사실). `ratio` 가 1.5 이상이면 부분 전송이 싸다.

- [ ] **Step 4: 결론 기록** — `RESULTS.md` 에 "## 전송" 절을 더하고 두 에뮬레이터의 줄을 붙인 뒤
"부분 전송 유효: 예/아니오" 와 비율을 적는다.

- [ ] **Step 5: Commit**

```bash
cd ~/work/scummvm
git add harness/dos/spikes/blit harness/dos/spikes/RESULTS.md
git commit -m "harness/dos: direct-FB transfer spike, full frame against small rects"
```

---

### Task 3: 빌드 골격 — configure 호스트와 링크되는 SCI 전용 EXE (스파이크 3)

`dos` 백엔드를 null 그래픽/믹서로 조립해 링크까지 간다. 컴파일 오류는 예상되며, 고치는 규칙을 아래에 둔다.

**Files:**
- Modify: `configure` (5곳, 아래)
- Create: `backends/platform/dos/module.mk`, `backends/platform/dos/dos.h`, `backends/platform/dos/dos.cpp`,
  `backends/platform/dos/build-dos.sh`

**Interfaces:**
- Produces: `class OSystem_DOS` (`dos.h`) — 이후 태스크가 `initBackend()` 에서 매니저를 바꾼다.
  `build-dos.sh` → `build-dos/scummvm.exe`, `dist/dos/SCUMMVM.EXE`, `dist/dos/CWSDPMI.EXE`.

- [ ] **Step 1: configure — 호스트 해석**

`configure` 의 `case $_host in` (1787행 부근), `*)` 바로 위에 넣는다:

```sh
i?86-pc-msdosdjgpp)
	_host_os=msdosdjgpp
	_host_cpu=i586
	_host_alias=$_host
	;;
```

(기본 분기는 `_host_alias` 를 `i586-msdosdjgpp` 로 만들어 도구 이름이 틀린다.)

- [ ] **Step 2: configure — 실행 파일 확장자, POSIX, 16bit, 디버그 소켓**

1. `get_system_exe_extension()` 의 `mingw* | *os2-emx)` 줄을 `mingw* | *os2-emx | msdosdjgpp)` 로 바꾼다.
2. "Checking if host is POSIX compliant" 의 `_posix=yes` 목록(`3ds | android | beos* ...` 줄) 끝의 `uclinux*)` 를
   `uclinux* | msdosdjgpp)` 로 바꾼다.
3. "Enable 16bit support only for backends which support it" 의 `case $_backend in` 목록에 `dos` 를 더한다
   (`3ds | android | dc | dos | ds | ...`).
4. `_debug_socket` 자동 판정의 `darwin* | linux* | ... | cygwin*)` 에 `| msdosdjgpp` 를 더한다.

- [ ] **Step 3: configure — 크로스 대상과 백엔드**

`if test -n "$_host"; then ... case "$_host" in` (3700행 부근) 의 `kos32)` 앞에 넣는다:

```sh
		i?86-pc-msdosdjgpp)
			_backend="dos"
			_dynamic_modules=no
			_sdlnet=no
			_libcurl=no
			_enet=no
			_cloud=no
			_opengl_mode=none
			_imgui=no
			_tts=no
			_discord=no
			_updates=no
			_build_scalers=no
			_build_aspect=no
			;;
```

`case $_backend in` (4270행 부근) 의 `null)` 앞에 넣는다:

```sh
	dos)
		append_var DEFINES "-DDOS_DJGPP"
		append_var CXXFLAGS "`pkg-config --cflags sdl3`"
		append_var LIBS "`pkg-config --static --libs sdl3`"
		;;
```

`append_var MODULES "backends/platform/$_backend"` (4446행) 가 `backends/platform/dos` 를 자동으로 넣는다.

- [ ] **Step 4: 백엔드 module.mk**

`backends/platform/dos/module.mk`:

```make
MODULE := backends/platform/dos

MODULE_OBJS := \
	dos.o

# We don't use rules.mk but rather manually update OBJS and MODULE_DIRS.
MODULE_OBJS := $(addprefix $(MODULE)/, $(MODULE_OBJS))
OBJS := $(MODULE_OBJS) $(OBJS)
MODULE_DIRS += $(sort $(dir $(MODULE_OBJS)))
```

- [ ] **Step 5: OSystem_DOS 헤더**

`backends/platform/dos/dos.h`:

```cpp
/* ScummVM - Graphic Adventure Engine
 * (GPLv3 header as in backends/platform/null/null.cpp)
 */

#ifndef BACKENDS_PLATFORM_DOS_DOS_H
#define BACKENDS_PLATFORM_DOS_DOS_H

#include "backends/modular-backend.h"
#include "common/events.h"

/**
 * MS-DOS through DJGPP. SDL3 opens the hardware (VESA modes, keyboard,
 * mouse); everything above that is ScummVM's. There are no threads: timers
 * run from pollEvent() and SDL3's cooperative scheduler runs in delayMillis().
 */
class OSystem_DOS : public ModularMixerBackend, public ModularGraphicsBackend, Common::EventSource {
public:
	OSystem_DOS();
	~OSystem_DOS() override;

	void initBackend() override;

	bool pollEvent(Common::Event &event) override;

	Common::MutexInternal *createMutex() override;
	uint32 getMillis(bool skipRecord = false) override;
	void delayMillis(uint msecs) override;
	void getTimeAndDate(TimeDate &td, bool skipRecord = false) const override;

	void quit() override;

	void logMessage(LogMessageType::Type type, const char *message) override;
	void addSysArchivesToSearchSet(Common::SearchSet &s, int priority) override;

private:
	Common::EventSource *_eventSource;	///< this until Task 7, then a DosEventSource
};

#endif
```

- [ ] **Step 6: OSystem_DOS 구현 (null 매니저로)**

`backends/platform/dos/dos.cpp`:

```cpp
/* ScummVM - Graphic Adventure Engine
 * (GPLv3 header as in backends/platform/null/null.cpp)
 */

#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_fopen
#define FORBIDDEN_SYMBOL_EXCEPTION_fclose
#define FORBIDDEN_SYMBOL_EXCEPTION_stderr
#define FORBIDDEN_SYMBOL_EXCEPTION_fputs
#define FORBIDDEN_SYMBOL_EXCEPTION_exit
#define FORBIDDEN_SYMBOL_EXCEPTION_time_h

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <time.h>
#include <stdio.h>
#include <sys/nearptr.h>
#include <SDL3/SDL.h>

#include "backends/platform/dos/dos.h"
#include "common/textconsole.h"
#include "backends/fs/posix/posix-fs-factory.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "backends/events/default/default-events.h"
#include "backends/mixer/null/null-mixer.h"
#include "backends/graphics/null/null-graphics.h"
#include "common/fs.h"
#include "base/main.h"

// ScummVM's call depth is far past DJGPP's 256 KB default stack.
unsigned _stklen = 1024 * 1024;

OSystem_DOS::OSystem_DOS() : _eventSource(this) {
	_fsFactory = new POSIXFilesystemFactory();
}

OSystem_DOS::~OSystem_DOS() {
}

void OSystem_DOS::initBackend() {
	SDL_SetHint(SDL_HINT_DOS_ALLOW_DIRECT_FRAMEBUFFER, "1");
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
		error("SDL_Init: %s", SDL_GetError());

	_timerManager = new DefaultTimerManager();
	_eventManager = new DefaultEventManager(_eventSource);
	_savefileManager = new DefaultSaveFileManager("SAVES");
	_graphicsManager = new NullGraphicsManager();
	_mixerManager = new NullMixerManager();
	_mixerManager->init();

	BaseBackend::initBackend();
}

bool OSystem_DOS::pollEvent(Common::Event &event) {
	((DefaultTimerManager *)getTimerManager())->checkTimers();
	((NullMixerManager *)_mixerManager)->update(1);
	return false;
}

Common::MutexInternal *OSystem_DOS::createMutex() {
	// No preemption yet: SDL3's threads only switch inside SDL calls.
	return new NullMutexInternal();
}

uint32 OSystem_DOS::getMillis(bool skipRecord) {
	return (uint32)SDL_GetTicks();
}

void OSystem_DOS::delayMillis(uint msecs) {
	SDL_Delay(msecs);	// also yields to SDL3's cooperative threads
}

void OSystem_DOS::getTimeAndDate(TimeDate &td, bool skipRecord) const {
	time_t curTime = time(0);
	struct tm t = *localtime(&curTime);
	td.tm_sec = t.tm_sec;
	td.tm_min = t.tm_min;
	td.tm_hour = t.tm_hour;
	td.tm_mday = t.tm_mday;
	td.tm_mon = t.tm_mon;
	td.tm_year = t.tm_year;
	td.tm_wday = t.tm_wday;
}

void OSystem_DOS::quit() {
	SDL_Quit();
	exit(0);
}

void OSystem_DOS::logMessage(LogMessageType::Type type, const char *message) {
	// The screen is in a graphics mode; the log is the only place output can go.
	FILE *f = fopen("SCUMMVM.LOG", "a");
	if (f) {
		fputs(message, f);
		fclose(f);
	}
}

void OSystem_DOS::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	Common::FSNode data("DATA");
	if (data.isDirectory())
		s.add("DATA", new Common::FSDirectory(data, 4), priority);
}

int main(int argc, char *argv[]) {
	// SDL3's VESA driver maps the framebuffer through the "fat DS" pointer.
	if (!__djgpp_nearptr_enable()) {
		fputs("__djgpp_nearptr_enable failed (needs a DPMI host that allows it)\n", stderr);
		return 1;
	}
	g_system = new OSystem_DOS();
	int res = scummvm_main(argc, argv);
	g_system->destroy();
	return res;
}

#endif
```

`pollEvent` 는 Task 7 전까지 이벤트를 만들지 않는다 (`EventSource` 로서 `this` 가 이 함수를 쓴다).

- [ ] **Step 7: 빌드 스크립트**

`backends/platform/dos/build-dos.sh`:

```bash
#!/bin/bash
# Configure and build the DOS port out of tree, then stage dist/dos.
# Usage: backends/platform/dos/build-dos.sh [extra configure args]
set -e
source ~/opt/dos-dev/env.sh
src="$(cd "$(dirname "$0")/../../.." && pwd)"
out="$src/build-dos"
mkdir -p "$out" "$src/dist/dos"
cd "$out"
if [ ! -f config.mk ] || [ -n "$*" ]; then
	"$src/configure" --host=i586-pc-msdosdjgpp \
		--disable-all-engines --enable-engine=sci --disable-engine=sci32 \
		--disable-mt32emu --disable-fluidsynth --disable-timidity \
		--disable-zlib --disable-png --disable-jpeg --disable-gif \
		--disable-vorbis --disable-tremor --disable-flac --disable-mad \
		--disable-theoradec --disable-mpeg2 --disable-faad --disable-a52 \
		--disable-freetype2 --disable-fribidi --disable-lua \
		--disable-detection-full --enable-release "$@"
fi
make -j"$(nproc)"
cp scummvm.exe "$src/dist/dos/SCUMMVM.EXE"
cp "$CWSDPMI_EXE" "$src/dist/dos/CWSDPMI.EXE"
ls -la "$src/dist/dos"
```

(M0 은 세이브 압축이 필요 없으므로 zlib 을 끈다. zlib 은 M4 계획에서 켠다.)

- [ ] **Step 8: 빌드 — 오류를 고치며 링크까지**

Run: `chmod +x backends/platform/dos/build-dos.sh && backends/platform/dos/build-dos.sh 2>&1 | tail -40`

configure 가 거부하는 `--disable-*` 가 있으면 그 인자만 빼고 다시 돌린다 (`./configure --help | grep <이름>` 으로 확인).
컴파일 오류는 이 규칙으로만 고친다:
- DJGPP 에 없는 POSIX API (예: `posix_spawn`, `sys/socket.h`, `getpwuid`) 를 쓰는 **백엔드·공통 코드** 는
  기존 `#if defined(POSIX)` 조건에 `&& !defined(__DJGPP__)` 를 더해 빼고, 빠진 기능은 DOS 에서 없는 것으로 둔다.
- `engines/` 는 고치지 않는다. 엔진이 안 되면 멈추고 보고한다.
- 고친 곳마다 `harness/dos/spikes/RESULTS.md` "## 빌드" 절에 `파일:줄 — 이유` 한 줄을 남긴다.

Expected: `dist/dos/SCUMMVM.EXE` 생성.

- [ ] **Step 9: DOSBox 에서 `--version` 확인**

```bash
mkdir -p /tmp/claude-1000/dosver && cp dist/dos/*.EXE /tmp/claude-1000/dosver/
cat > /tmp/claude-1000/dosver/v.conf <<'EOF'
[dosbox]
machine = svga_s3
memsize = 16
[autoexec]
mount c /tmp/claude-1000/dosver
c:
SCUMMVM.EXE --version > VER.TXT
exit
EOF
SDL_AUDIODRIVER=dummy timeout 60 xvfb-run -a -s "-screen 0 1024x768x24" dosbox-x -conf /tmp/claude-1000/dosver/v.conf -nopromptfolder -fastlaunch >/dev/null 2>&1
cat /tmp/claude-1000/dosver/VER.TXT; ls -la dist/dos/SCUMMVM.EXE
```

Expected: `VER.TXT` 에 `ScummVM 2026.x...` 와 `Features compiled in:` 줄. EXE 크기를 `RESULTS.md` "## 빌드" 에 적는다
(목표 10MB 이하, 넘으면 넘는다고 적기만 한다).

- [ ] **Step 10: Commit** (두 커밋: 공통 configure, DOS 백엔드)

```bash
git add configure
git commit -m "CONFIGURE: Add the i586-pc-msdosdjgpp host and the dos backend"
git add backends/platform/dos
git commit -m "DOS: Backend skeleton on SDL3, null graphics and mixer"
```

그 밖에 Step 8 에서 고친 공통 파일은 각각 `"<AREA>: Leave out <API> on DJGPP"` 형식으로 따로 커밋한다.

---

### Task 4: 비디오 모드 선택 (순수 로직 + 단위 테스트)

**Files:**
- Create: `backends/platform/dos/dos-modes.h`
- Create: `test/backends/dos_modes.h`
- Modify: `test/module.mk` (TESTS 에 한 줄)

**Interfaces:**
- Produces (namespace `DOS`):
  - `struct VideoMode { uint16 w, h; Graphics::PixelFormat format; int sdlIndex; }`
  - `int findExactMode(const Common::Array<VideoMode> &modes, uint w, uint h, const Graphics::PixelFormat &f)` — 인덱스, 없으면 -1
  - `Common::List<Graphics::PixelFormat> supportedFormats(const Common::Array<VideoMode> &modes, uint w, uint h)` —
    그 크기에 정확히 있는 트루컬러를 RGB565 > XRGB1555 > XRGB8888 순으로, CLUT8 을 맨 끝에 (항상)
  - `Graphics::PixelFormat rgb565()`, `xrgb1555()`, `xrgb8888()`

- [ ] **Step 1: 실패하는 테스트 작성**

`test/backends/dos_modes.h`:

```cpp
#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/dos-modes.h"

class DosModesTestSuite : public CxxTest::TestSuite {
	static Common::Array<DOS::VideoMode> staging() {
		// 640x400 as DOSBox Staging's S3 offers it, measured 2026-09-28.
		Common::Array<DOS::VideoMode> m;
		DOS::VideoMode a = { 640, 400, DOS::xrgb8888(), 0 };
		DOS::VideoMode b = { 640, 400, Graphics::PixelFormat::createFormatCLUT8(), 1 };
		DOS::VideoMode c = { 640, 480, DOS::rgb565(), 2 };
		DOS::VideoMode d = { 320, 200, Graphics::PixelFormat::createFormatCLUT8(), 3 };
		m.push_back(a); m.push_back(b); m.push_back(c); m.push_back(d);
		return m;
	}
	static Common::Array<DOS::VideoMode> dosboxX() {
		Common::Array<DOS::VideoMode> m = staging();
		DOS::VideoMode e = { 640, 400, DOS::rgb565(), 4 };
		DOS::VideoMode f = { 640, 400, DOS::xrgb1555(), 5 };
		m.push_back(e); m.push_back(f);
		return m;
	}

public:
	void test_exact_mode_found() {
		TS_ASSERT_EQUALS(DOS::findExactMode(staging(), 320, 200, Graphics::PixelFormat::createFormatCLUT8()), 3);
	}

	void test_exact_mode_missing_format() {
		TS_ASSERT_EQUALS(DOS::findExactMode(staging(), 640, 400, DOS::rgb565()), -1);
	}

	void test_formats_prefer_16bit_then_clut8_last() {
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(dosboxX(), 640, 400);
		Common::List<Graphics::PixelFormat>::const_iterator it = got.begin();
		TS_ASSERT_EQUALS(got.size(), 4u);
		TS_ASSERT(*it++ == DOS::rgb565());
		TS_ASSERT(*it++ == DOS::xrgb1555());
		TS_ASSERT(*it++ == DOS::xrgb8888());
		TS_ASSERT(*it++ == Graphics::PixelFormat::createFormatCLUT8());
	}

	void test_formats_only_what_the_size_has() {
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(staging(), 640, 400);
		TS_ASSERT_EQUALS(got.size(), 2u);
		TS_ASSERT(got.front() == DOS::xrgb8888());
		TS_ASSERT(got.back() == Graphics::PixelFormat::createFormatCLUT8());
	}

	void test_clut8_even_without_a_mode() {
		Common::List<Graphics::PixelFormat> got = DOS::supportedFormats(staging(), 800, 600);
		TS_ASSERT_EQUALS(got.size(), 1u);
		TS_ASSERT(got.front() == Graphics::PixelFormat::createFormatCLUT8());
	}
};
```

`test/module.mk` 의 `TESTS` 에서 `$(srcdir)/test/backends/surfacesdl_hwformat.h` 뒤에 줄을 더한다:

```make
	$(srcdir)/test/backends/surfacesdl_hwformat.h \
	$(srcdir)/test/backends/dos_*.h
```

- [ ] **Step 2: 실패 확인** (리눅스 빌드 트리)

```bash
mkdir -p ~/work/scummvm/builds/linux-dos-test && cd ~/work/scummvm/builds/linux-dos-test
[ -f config.mk ] || ~/work/scummvm/dos/configure --disable-all-engines --enable-engine=sci --enable-debug-socket
make test 2>&1 | tail -5
```

Expected: `dos-modes.h: No such file or directory` 로 테스트 빌드 실패.

- [ ] **Step 3: 구현**

`backends/platform/dos/dos-modes.h`:

```cpp
/* ScummVM - Graphic Adventure Engine
 * (GPLv3 header)
 */

#ifndef BACKENDS_PLATFORM_DOS_DOS_MODES_H
#define BACKENDS_PLATFORM_DOS_DOS_MODES_H

#include "common/array.h"
#include "common/list.h"
#include "common/util.h"
#include "graphics/pixelformat.h"

namespace DOS {

/** One VESA mode as SDL3 lists it; sdlIndex is its place in that list. */
struct VideoMode {
	uint16 w, h;
	Graphics::PixelFormat format;
	int sdlIndex;
};

inline Graphics::PixelFormat rgb565() { return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0); }
inline Graphics::PixelFormat xrgb1555() { return Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0); }
inline Graphics::PixelFormat xrgb8888() { return Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0); }

/** Index into @p modes of the mode that is exactly w x h in @p f, or -1. */
inline int findExactMode(const Common::Array<VideoMode> &modes, uint w, uint h, const Graphics::PixelFormat &f) {
	for (uint i = 0; i < modes.size(); ++i)
		if (modes[i].w == w && modes[i].h == h && modes[i].format == f)
			return (int)i;
	return -1;
}

/**
 * What getSupportedFormats() reports for a w x h game: the true-colour
 * formats a mode of exactly that size has, cheapest on the bus first,
 * then CLUT8, which is always there (the engine falls back to it).
 */
inline Common::List<Graphics::PixelFormat> supportedFormats(const Common::Array<VideoMode> &modes, uint w, uint h) {
	static const Graphics::PixelFormat order[] = { rgb565(), xrgb1555(), xrgb8888() };
	Common::List<Graphics::PixelFormat> out;
	for (uint i = 0; i < ARRAYSIZE(order); ++i)
		if (findExactMode(modes, w, h, order[i]) >= 0)
			out.push_back(order[i]);
	out.push_back(Graphics::PixelFormat::createFormatCLUT8());
	return out;
}

} // End of namespace DOS

#endif
```

- [ ] **Step 4: 통과 확인**

Run: `cd ~/work/scummvm/builds/linux-dos-test && make test 2>&1 | grep -E "DosModes|OK!|Failed"`
Expected: `OK!` (또는 전체 통과), `DosModesTestSuite` 실패 없음.

- [ ] **Step 5: Commit**

```bash
cd ~/work/scummvm/dos
git add backends/platform/dos/dos-modes.h
git commit -m "DOS: Pick VESA modes and report formats from SDL3's mode list"
git add test/backends/dos_modes.h test/module.mk
git commit -m "TEST: DOS mode selection"
```

---

### Task 5: 소프트웨어 커서 (순수 로직 + 단위 테스트)

**Files:**
- Create: `backends/platform/dos/soft-cursor.h`
- Create: `test/backends/dos_soft_cursor.h`

**Interfaces:**
- Produces: `class DOS::SoftCursor`
  - `void setImage(const byte *buf, uint w, uint h, int hotX, int hotY, uint32 key, uint bpp)` — 이미지를 복사해 둔다
  - `Common::Rect draw(byte *dst, int pitch, int dstW, int dstH, int x, int y)` — (x-hotX, y-hotY) 에 키 색을 빼고
    그린다, 덮은 곳을 저장, 덮은 사각형(클립됨)을 돌려준다
  - `Common::Rect restore(byte *dst, int pitch)` — 마지막 draw 가 덮은 곳을 되돌리고 그 사각형을 돌려준다(없으면 빈 사각형)
  - `bool hasImage() const`

- [ ] **Step 1: 실패하는 테스트 작성**

`test/backends/dos_soft_cursor.h`:

```cpp
#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/soft-cursor.h"

class DosSoftCursorTestSuite : public CxxTest::TestSuite {
	// 2x2 cursor: key colour 0 at top-left, ink 7 elsewhere.
	static const byte *image() { static const byte img[4] = { 0, 7, 7, 7 }; return img; }

public:
	void test_draw_skips_key_colour_and_restore_puts_back() {
		byte screen[4 * 4];
		memset(screen, 1, sizeof(screen));
		DOS::SoftCursor c;
		c.setImage(image(), 2, 2, 0, 0, 0, 1);
		Common::Rect r = c.draw(screen, 4, 4, 4, 1, 1);
		TS_ASSERT_EQUALS(r, Common::Rect(1, 1, 3, 3));
		TS_ASSERT_EQUALS(screen[1 * 4 + 1], 1);	// key colour: untouched
		TS_ASSERT_EQUALS(screen[1 * 4 + 2], 7);
		TS_ASSERT_EQUALS(screen[2 * 4 + 1], 7);
		Common::Rect back = c.restore(screen, 4);
		TS_ASSERT_EQUALS(back, r);
		for (int i = 0; i < 16; ++i)
			TS_ASSERT_EQUALS(screen[i], 1);
	}

	void test_hotspot_and_clipping() {
		byte screen[4 * 4];
		memset(screen, 1, sizeof(screen));
		DOS::SoftCursor c;
		c.setImage(image(), 2, 2, 1, 1, 0, 1);
		Common::Rect r = c.draw(screen, 4, 4, 4, 0, 0);	// top-left at (-1,-1)
		TS_ASSERT_EQUALS(r, Common::Rect(0, 0, 1, 1));
		TS_ASSERT_EQUALS(screen[0], 7);
		c.restore(screen, 4);
		TS_ASSERT_EQUALS(screen[0], 1);
	}

	void test_restore_without_draw_is_empty() {
		byte screen[16];
		DOS::SoftCursor c;
		TS_ASSERT(c.restore(screen, 4).isEmpty());
	}

	void test_16bpp_key() {
		uint16 screen[2 * 2] = { 5, 5, 5, 5 };
		const uint16 img[1] = { 0xF800 };
		DOS::SoftCursor c;
		c.setImage((const byte *)img, 1, 1, 0, 0, 0x001F, 2);
		c.draw((byte *)screen, 4, 2, 2, 1, 0);
		TS_ASSERT_EQUALS(screen[1], 0xF800);
		c.restore((byte *)screen, 4);
		TS_ASSERT_EQUALS(screen[1], 5);
	}
};
```

- [ ] **Step 2: 실패 확인**

Run: `cd ~/work/scummvm/builds/linux-dos-test && make test 2>&1 | tail -3`
Expected: `soft-cursor.h: No such file or directory`.

- [ ] **Step 3: 구현**

`backends/platform/dos/soft-cursor.h`:

```cpp
/* ScummVM - Graphic Adventure Engine
 * (GPLv3 header)
 */

#ifndef BACKENDS_PLATFORM_DOS_SOFT_CURSOR_H
#define BACKENDS_PLATFORM_DOS_SOFT_CURSOR_H

#include "common/array.h"
#include "common/rect.h"
#include "common/scummsys.h"

namespace DOS {

/**
 * A cursor drawn into the frame we send, since SDL3's direct-framebuffer
 * path draws none. It keeps what it covered so moving it rewrites only its
 * own rectangle. Image and destination share one pixel size (1, 2 or 4
 * bytes); converting to the screen format is the caller's job.
 */
class SoftCursor {
public:
	SoftCursor() : _w(0), _h(0), _hotX(0), _hotY(0), _key(0), _bpp(1) {}

	bool hasImage() const { return _w && _h; }

	void setImage(const byte *buf, uint w, uint h, int hotX, int hotY, uint32 key, uint bpp) {
		_w = w; _h = h; _hotX = hotX; _hotY = hotY; _key = key; _bpp = bpp;
		_image.resize(w * h * bpp);
		if (!_image.empty())
			memcpy(&_image[0], buf, _image.size());
	}

	Common::Rect draw(byte *dst, int pitch, int dstW, int dstH, int x, int y) {
		Common::Rect r(x - _hotX, y - _hotY, x - _hotX + _w, y - _hotY + _h);
		r.clip(Common::Rect(0, 0, dstW, dstH));
		_saved = r;
		if (r.isEmpty() || !hasImage())
			return _saved = Common::Rect();
		_under.resize(r.width() * r.height() * _bpp);
		for (int row = 0; row < r.height(); ++row) {
			byte *d = dst + (r.top + row) * pitch + r.left * _bpp;
			memcpy(&_under[row * r.width() * _bpp], d, r.width() * _bpp);
			const int sy = r.top + row - (y - _hotY);
			for (int col = 0; col < r.width(); ++col) {
				const int sx = r.left + col - (x - _hotX);
				const byte *s = &_image[(sy * _w + sx) * _bpp];
				if (read(s) != _key)
					memcpy(d + col * _bpp, s, _bpp);
			}
		}
		return _saved;
	}

	Common::Rect restore(byte *dst, int pitch) {
		const Common::Rect r = _saved;
		for (int row = 0; row < r.height(); ++row)
			memcpy(dst + (r.top + row) * pitch + r.left * _bpp, &_under[row * r.width() * _bpp], r.width() * _bpp);
		_saved = Common::Rect();
		return r;
	}

private:
	uint32 read(const byte *p) const {
		switch (_bpp) {
		case 1: return *p;
		case 2: return *(const uint16 *)p;
		default: return *(const uint32 *)p;
		}
	}

	uint _w, _h;
	int _hotX, _hotY;
	uint32 _key;
	uint _bpp;
	Common::Array<byte> _image, _under;
	Common::Rect _saved;
};

} // End of namespace DOS

#endif
```

- [ ] **Step 4: 통과 확인**

Run: `cd ~/work/scummvm/builds/linux-dos-test && make test 2>&1 | grep -E "DosSoftCursor|OK!|Failed"`
Expected: 실패 없음.

- [ ] **Step 5: Commit**

```bash
cd ~/work/scummvm/dos
git add backends/platform/dos/soft-cursor.h
git commit -m "DOS: A software cursor that keeps what it covers"
git add test/backends/dos_soft_cursor.h
git commit -m "TEST: DOS software cursor"
```

---

### Task 6: `DosGraphicsManager` — CLUT8 게임 화면을 SDL3 로

**Files:**
- Create: `backends/graphics/dos/dos-graphics.h`, `backends/graphics/dos/dos-graphics.cpp`
- Modify: `backends/platform/dos/module.mk` (오브젝트 추가), `backends/platform/dos/dos.cpp` (매니저 교체)

**Interfaces:**
- Consumes: `DOS::VideoMode`, `DOS::findExactMode`, `DOS::supportedFormats`, `DOS::rgb565` (Task 4);
  `DOS::SoftCursor` (Task 5)
- Produces: `class DosGraphicsManager : public GraphicsManager`, 생성자 `DosGraphicsManager()`,
  `SDL_Window *window() const`, `Common::Point gameMouse(float wx, float wy) const` (창 좌표 → 게임 좌표, Task 7 이 씀),
  `void setMousePos(int x, int y)` (Task 7 이 씀)

- [ ] **Step 1: 헤더**

`backends/graphics/dos/dos-graphics.h`:

```cpp
/* ScummVM - Graphic Adventure Engine
 * (GPLv3 header)
 */

#ifndef BACKENDS_GRAPHICS_DOS_DOS_GRAPHICS_H
#define BACKENDS_GRAPHICS_DOS_DOS_GRAPHICS_H

#include "backends/graphics/graphics.h"
#include "backends/platform/dos/dos-modes.h"
#include "backends/platform/dos/soft-cursor.h"
#include "common/array.h"
#include "common/rect.h"
#include "graphics/surface.h"

struct SDL_Window;

/**
 * The game screen in system RAM, copied to SDL3's window surface one dirty
 * rectangle at a time with the cursor on top; SDL3's direct-framebuffer
 * path then sends those rectangles to VRAM. M0: the physical mode must be
 * exactly the game's size and format; the overlay is kept but not shown.
 */
class DosGraphicsManager : public GraphicsManager {
public:
	DosGraphicsManager();
	~DosGraphicsManager() override;

	SDL_Window *window() const { return _window; }
	Common::Point gameMouse(float wx, float wy) const;
	void setMousePos(int x, int y);

	bool hasFeature(OSystem::Feature f) const override { return false; }
	void setFeatureState(OSystem::Feature f, bool enable) override {}
	bool getFeatureState(OSystem::Feature f) const override { return false; }

	Graphics::PixelFormat getScreenFormat() const override { return _screen.format; }
	Common::List<Graphics::PixelFormat> getSupportedFormats() const override;

	void initSize(uint width, uint height, const Graphics::PixelFormat *format = nullptr) override;
	int getScreenChangeID() const override { return _screenChangeID; }
	void beginGFXTransaction() override {}
	OSystem::TransactionError endGFXTransaction() override;

	int16 getHeight() const override { return _screen.h; }
	int16 getWidth() const override { return _screen.w; }
	void setPalette(const byte *colors, uint start, uint num) override;
	void grabPalette(byte *colors, uint start, uint num) const override;
	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
	Graphics::Surface *lockScreen() override { return &_screen; }
	void unlockScreen() override { addDirty(Common::Rect(_screen.w, _screen.h)); }
	void fillScreen(uint32 col) override;
	void fillScreen(const Common::Rect &r, uint32 col) override;
	void updateScreen() override;
	void setShakePos(int shakeXOffset, int shakeYOffset) override;
	void setFocusRectangle(const Common::Rect &rect) override {}
	void clearFocusRectangle() override {}

	void showOverlay(bool inGUI) override;
	void hideOverlay() override { _overlayVisible = false; }
	bool isOverlayVisible() const override { return _overlayVisible; }
	Graphics::PixelFormat getOverlayFormat() const override { return _overlay.format; }
	void clearOverlay() override;
	void grabOverlay(Graphics::Surface &surface) const override;
	void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;
	int16 getOverlayHeight() const override { return _overlay.h; }
	int16 getOverlayWidth() const override { return _overlay.w; }

	bool showMouse(bool visible) override;
	void warpMouse(int x, int y) override;
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
						const Graphics::PixelFormat *format, const byte *mask, frac_t scaleX, frac_t scaleY) override;
	void setCursorPalette(const byte *colors, uint start, uint num) override {}

private:
	void addDirty(const Common::Rect &r);
	bool setMode(int index);

	Common::Array<DOS::VideoMode> _modes;
	SDL_Window *_window;
	int _screenChangeID;

	uint _pendingW, _pendingH;
	Graphics::PixelFormat _pendingFormat;

	Graphics::Surface _screen;	///< the game's pixels, in its own format
	Graphics::Surface _overlay;	///< RGB565 640x480; not shown until M4
	bool _overlayVisible;
	byte _palette[256 * 3];
	bool _paletteDirty;
	int _shakeX, _shakeY;

	Common::Array<Common::Rect> _dirty;
	bool _fullDirty;

	DOS::SoftCursor _cursor;
	bool _cursorVisible;
	int _mouseX, _mouseY;
};

#endif
```

- [ ] **Step 2: 구현**

`backends/graphics/dos/dos-graphics.cpp`:

```cpp
/* ScummVM - Graphic Adventure Engine
 * (GPLv3 header)
 */

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <SDL3/SDL.h>

#include "backends/graphics/dos/dos-graphics.h"
#include "common/textconsole.h"

static Graphics::PixelFormat fromSdl(SDL_PixelFormat f, bool &ok) {
	ok = true;
	switch (f) {
	case SDL_PIXELFORMAT_INDEX8: return Graphics::PixelFormat::createFormatCLUT8();
	case SDL_PIXELFORMAT_RGB565: return DOS::rgb565();
	case SDL_PIXELFORMAT_XRGB1555: return DOS::xrgb1555();
	case SDL_PIXELFORMAT_XRGB8888: return DOS::xrgb8888();
	default: ok = false; return Graphics::PixelFormat();
	}
}

// Past this many rectangles one full-screen update is cheaper than the list.
static const uint kMaxDirtyRects = 32;

DosGraphicsManager::DosGraphicsManager() :
	_window(nullptr), _screenChangeID(0), _pendingW(0), _pendingH(0),
	_overlayVisible(false), _paletteDirty(false), _shakeX(0), _shakeY(0),
	_fullDirty(false), _cursorVisible(false), _mouseX(0), _mouseY(0) {
	memset(_palette, 0, sizeof(_palette));
	int n = 0;
	SDL_DisplayMode **m = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &n);
	for (int i = 0; i < n; ++i) {
		bool ok;
		Graphics::PixelFormat f = fromSdl(m[i]->format, ok);
		if (!ok)
			continue;
		DOS::VideoMode vm = { (uint16)m[i]->w, (uint16)m[i]->h, f, i };
		_modes.push_back(vm);
	}
	SDL_free(m);
	_overlay.create(640, 480, DOS::rgb565());
}

DosGraphicsManager::~DosGraphicsManager() {
	_screen.free();
	_overlay.free();
	if (_window)
		SDL_DestroyWindow(_window);
}

Common::List<Graphics::PixelFormat> DosGraphicsManager::getSupportedFormats() const {
	// SCI asks before initGraphics(); M0 answers for its hires size.
	return DOS::supportedFormats(_modes, 640, 400);
}

void DosGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	_pendingW = width;
	_pendingH = height;
	_pendingFormat = format ? *format : Graphics::PixelFormat::createFormatCLUT8();
}

bool DosGraphicsManager::setMode(int index) {
	int n = 0;
	SDL_DisplayMode **m = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &n);
	const SDL_DisplayMode mode = *m[_modes[index].sdlIndex];
	SDL_free(m);
	if (!_window)
		_window = SDL_CreateWindow("ScummVM", mode.w, mode.h, 0);
	if (!_window) {
		warning("DosGraphicsManager: SDL_CreateWindow: %s", SDL_GetError());
		return false;
	}
	if (!SDL_SetWindowFullscreenMode(_window, &mode) || !SDL_SetWindowFullscreen(_window, true)) {
		warning("DosGraphicsManager: mode %dx%d: %s", mode.w, mode.h, SDL_GetError());
		return false;
	}
	SDL_SyncWindow(_window);
	return SDL_GetWindowSurface(_window) != nullptr;
}

OSystem::TransactionError DosGraphicsManager::endGFXTransaction() {
	if (!_pendingW)
		return OSystem::kTransactionSuccess;
	const int index = DOS::findExactMode(_modes, _pendingW, _pendingH, _pendingFormat);
	if (index < 0) {
		warning("DosGraphicsManager: no %ux%u %s mode", _pendingW, _pendingH, _pendingFormat.toString().c_str());
		_pendingW = 0;
		return OSystem::kTransactionSizeChangeFailed;
	}
	if (!setMode(index)) {
		_pendingW = 0;
		return OSystem::kTransactionSizeChangeFailed;
	}
	_screen.free();
	_screen.create(_pendingW, _pendingH, _pendingFormat);
	_pendingW = 0;
	_paletteDirty = true;
	_fullDirty = true;
	++_screenChangeID;
	return OSystem::kTransactionSuccess;
}

void DosGraphicsManager::setPalette(const byte *colors, uint start, uint num) {
	memcpy(_palette + start * 3, colors, num * 3);
	_paletteDirty = true;
}

void DosGraphicsManager::grabPalette(byte *colors, uint start, uint num) const {
	memcpy(colors, _palette + start * 3, num * 3);
}

void DosGraphicsManager::addDirty(const Common::Rect &r) {
	if (_fullDirty || r.isEmpty())
		return;
	if (_dirty.size() >= kMaxDirtyRects) {
		_fullDirty = true;
		_dirty.clear();
		return;
	}
	_dirty.push_back(r);
}

void DosGraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	_screen.copyRectToSurface(buf, pitch, x, y, w, h);
	addDirty(Common::Rect(x, y, x + w, y + h));
}

void DosGraphicsManager::fillScreen(uint32 col) {
	_screen.fillRect(Common::Rect(_screen.w, _screen.h), col);
	_fullDirty = true;
}

void DosGraphicsManager::fillScreen(const Common::Rect &r, uint32 col) {
	_screen.fillRect(r, col);
	addDirty(r);
}

void DosGraphicsManager::setShakePos(int shakeXOffset, int shakeYOffset) {
	if (shakeXOffset != _shakeX || shakeYOffset != _shakeY) {
		_shakeX = shakeXOffset;
		_shakeY = shakeYOffset;
		_fullDirty = true;
	}
}

void DosGraphicsManager::updateScreen() {
	if (!_window || !_screen.getPixels())
		return;
	SDL_Surface *s = SDL_GetWindowSurface(_window);
	if (!s)
		return;

	if (_paletteDirty && s->format == SDL_PIXELFORMAT_INDEX8) {
		SDL_Color c[256];
		for (int i = 0; i < 256; ++i) {
			c[i].r = _palette[i * 3];
			c[i].g = _palette[i * 3 + 1];
			c[i].b = _palette[i * 3 + 2];
			c[i].a = 255;
		}
		SDL_SetPaletteColors(SDL_GetSurfacePalette(s), c, 0, 256);
	}

	Common::Array<Common::Rect> send;
	Common::Rect back = _cursor.restore((byte *)s->pixels, s->pitch);
	if (!back.isEmpty())
		send.push_back(back);

	const int bpp = _screen.format.bytesPerPixel;
	if (_fullDirty) {
		// Shake shifts the whole picture; the uncovered strip is colour 0.
		memset(s->pixels, 0, s->pitch * s->h);
		Common::Rect src(_screen.w, _screen.h);
		Common::Rect dst = src;
		dst.translate(_shakeX, _shakeY);
		dst.clip(Common::Rect(s->w, s->h));
		for (int y = dst.top; y < dst.bottom; ++y)
			memcpy((byte *)s->pixels + y * s->pitch + dst.left * bpp,
				   _screen.getBasePtr(dst.left - _shakeX, y - _shakeY), dst.width() * bpp);
		send.clear();
		send.push_back(Common::Rect(s->w, s->h));
	} else {
		for (uint i = 0; i < _dirty.size(); ++i) {
			Common::Rect r = _dirty[i];
			r.translate(_shakeX, _shakeY);
			r.clip(Common::Rect(s->w, s->h));
			for (int y = r.top; y < r.bottom; ++y)
				memcpy((byte *)s->pixels + y * s->pitch + r.left * bpp,
					   _screen.getBasePtr(r.left - _shakeX, y - _shakeY), r.width() * bpp);
			send.push_back(r);
		}
	}

	if (_cursorVisible && _cursor.hasImage()) {
		Common::Rect c = _cursor.draw((byte *)s->pixels, s->pitch, s->w, s->h, _mouseX, _mouseY);
		if (!c.isEmpty())
			send.push_back(c);
	}

	if (!send.empty() || _paletteDirty) {
		Common::Array<SDL_Rect> rects;
		for (uint i = 0; i < send.size(); ++i) {
			SDL_Rect r = { send[i].left, send[i].top, send[i].width(), send[i].height() };
			rects.push_back(r);
		}
		SDL_UpdateWindowSurfaceRects(_window, rects.empty() ? nullptr : &rects[0], (int)rects.size());
	}
	_dirty.clear();
	_fullDirty = false;
	_paletteDirty = false;
}

void DosGraphicsManager::showOverlay(bool inGUI) {
	static bool warned = false;
	if (!warned) {
		warning("DosGraphicsManager: the GUI overlay is not shown before M4");
		warned = true;
	}
	_overlayVisible = true;
}

void DosGraphicsManager::clearOverlay() {
	_overlay.fillRect(Common::Rect(_overlay.w, _overlay.h), 0);
}

void DosGraphicsManager::grabOverlay(Graphics::Surface &surface) const {
	surface.copyFrom(_overlay);
}

void DosGraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	_overlay.copyRectToSurface(buf, pitch, x, y, w, h);
}

bool DosGraphicsManager::showMouse(bool visible) {
	const bool last = _cursorVisible;
	_cursorVisible = visible;
	return last;
}

void DosGraphicsManager::setMousePos(int x, int y) {
	_mouseX = x;
	_mouseY = y;
}

void DosGraphicsManager::warpMouse(int x, int y) {
	setMousePos(x, y);
	if (_window)
		SDL_WarpMouseInWindow(_window, (float)x, (float)y);
}

Common::Point DosGraphicsManager::gameMouse(float wx, float wy) const {
	// M0: the mode is the game's size, so window and game coordinates agree.
	return Common::Point((int16)wx, (int16)wy);
}

void DosGraphicsManager::setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
										const Graphics::PixelFormat *format, const byte *mask, frac_t scaleX, frac_t scaleY) {
	const Graphics::PixelFormat f = format ? *format : Graphics::PixelFormat::createFormatCLUT8();
	if (f != _screen.format) {
		warning("DosGraphicsManager: cursor format %s differs from the screen's, ignored", f.toString().c_str());
		return;
	}
	_cursor.setImage((const byte *)buf, w, h, hotspotX, hotspotY, keycolor, f.bytesPerPixel);
}

#endif
```

- [ ] **Step 3: module.mk 과 OSystem_DOS 에 연결**

`backends/platform/dos/module.mk` 의 `MODULE_OBJS` 를:

```make
MODULE_OBJS := \
	dos.o \
	../../graphics/dos/dos-graphics.o
```

`backends/platform/dos/dos.cpp`: `#include "backends/graphics/null/null-graphics.h"` 를
`#include "backends/graphics/dos/dos-graphics.h"` 로, `_graphicsManager = new NullGraphicsManager();` 를
`_graphicsManager = new DosGraphicsManager();` 로 바꾼다.

- [ ] **Step 4: 빌드**

Run: `backends/platform/dos/build-dos.sh 2>&1 | tail -5`
Expected: 링크 성공.

- [ ] **Step 5: KQ1 이 뜨는지 눈으로 한 번 확인 (Task 9 전 임시)**

```bash
d=/tmp/claude-1000/dosrun && rm -rf $d && mkdir -p $d/c && cp dist/dos/*.EXE $d/c/
cat > $d/c/SCUMMVM.INI <<'EOF'
[scummvm]
[kq1sci]
gameid=kq1sci
path=D:\
EOF
cat > $d/run.conf <<EOF
[dosbox]
machine = svga_s3
memsize = 16
[cpu]
cycles = 60000
[autoexec]
mount c $d/c
mount d "$HOME/work/scummvm/gamedata/King's Quest 1 - Quest for the Crown (DOS 1991 Remake)"
c:
SCUMMVM.EXE kq1sci
exit
EOF
SDL_AUDIODRIVER=dummy timeout 40 xvfb-run -a -s "-screen 0 1024x768x24" dosbox-x -conf $d/run.conf -nopromptfolder -fastlaunch >/dev/null 2>&1
tail -20 $d/c/SCUMMVM.LOG
```

Expected: 로그에 엔진 시작 메시지, `DosGraphicsManager: no ... mode` 경고 없음. (화면 확인은 Task 9 의 덤프로 한다.)
`timeout` 으로 끝나는 것은 정상이다 (이벤트가 없어 게임이 계속 돈다).

- [ ] **Step 6: Commit**

```bash
git add backends/graphics/dos backends/platform/dos/module.mk backends/platform/dos/dos.cpp
git commit -m "DOS: Graphics manager, dirty rectangles to SDL3's window surface with a software cursor"
```

---

### Task 7: `DosEventSource` — 키보드, 마우스, 종료

**Files:**
- Create: `backends/events/dos/dos-events.h`, `backends/events/dos/dos-events.cpp`
- Modify: `backends/platform/dos/module.mk`, `backends/platform/dos/dos.h`, `backends/platform/dos/dos.cpp`

**Interfaces:**
- Consumes: `DosGraphicsManager::gameMouse(float, float)`, `DosGraphicsManager::setMousePos(int, int)` (Task 6)
- Produces: `class DosEventSource : public Common::EventSource` — `explicit DosEventSource(DosGraphicsManager *gfx)`,
  `bool pollEvent(Common::Event &event)`; 정적 `static Common::KeyCode toKeyCode(SDL_Keycode key)`

SDL3 의 ASCII 영역 키코드는 ASCII 값이고 `Common::KeyCode` 도 그렇다. 표는 그 밖의 키만 둔다.
(`SdlEventSource::SDLToOSystemKeycode` 는 SDL 백엔드 전체에 묶여 있어 가져오지 않는다.)

- [ ] **Step 1: 헤더**

`backends/events/dos/dos-events.h`:

```cpp
/* ScummVM - Graphic Adventure Engine
 * (GPLv3 header)
 */

#ifndef BACKENDS_EVENTS_DOS_DOS_EVENTS_H
#define BACKENDS_EVENTS_DOS_DOS_EVENTS_H

#include <SDL3/SDL_keycode.h>
#include "common/events.h"

class DosGraphicsManager;

/** SDL3's DOS keyboard and INT 33h mouse, as ScummVM events. */
class DosEventSource : public Common::EventSource {
public:
	explicit DosEventSource(DosGraphicsManager *gfx) : _gfx(gfx) {}

	bool pollEvent(Common::Event &event) override;

	static Common::KeyCode toKeyCode(SDL_Keycode key);

private:
	DosGraphicsManager *_gfx;
};

#endif
```

- [ ] **Step 2: 구현**

`backends/events/dos/dos-events.cpp`:

```cpp
/* ScummVM - Graphic Adventure Engine
 * (GPLv3 header)
 */

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <SDL3/SDL.h>

#include "backends/events/dos/dos-events.h"
#include "backends/graphics/dos/dos-graphics.h"

Common::KeyCode DosEventSource::toKeyCode(SDL_Keycode key) {
	if (key < 0x80)
		return (Common::KeyCode)key;	// ASCII, the same numbers on both sides
	switch (key) {
	case SDLK_UP: return Common::KEYCODE_UP;
	case SDLK_DOWN: return Common::KEYCODE_DOWN;
	case SDLK_LEFT: return Common::KEYCODE_LEFT;
	case SDLK_RIGHT: return Common::KEYCODE_RIGHT;
	case SDLK_HOME: return Common::KEYCODE_HOME;
	case SDLK_END: return Common::KEYCODE_END;
	case SDLK_PAGEUP: return Common::KEYCODE_PAGEUP;
	case SDLK_PAGEDOWN: return Common::KEYCODE_PAGEDOWN;
	case SDLK_INSERT: return Common::KEYCODE_INSERT;
	case SDLK_F1: return Common::KEYCODE_F1;
	case SDLK_F2: return Common::KEYCODE_F2;
	case SDLK_F3: return Common::KEYCODE_F3;
	case SDLK_F4: return Common::KEYCODE_F4;
	case SDLK_F5: return Common::KEYCODE_F5;
	case SDLK_F6: return Common::KEYCODE_F6;
	case SDLK_F7: return Common::KEYCODE_F7;
	case SDLK_F8: return Common::KEYCODE_F8;
	case SDLK_F9: return Common::KEYCODE_F9;
	case SDLK_F10: return Common::KEYCODE_F10;
	case SDLK_F11: return Common::KEYCODE_F11;
	case SDLK_F12: return Common::KEYCODE_F12;
	case SDLK_KP_0: return Common::KEYCODE_KP0;
	case SDLK_KP_1: return Common::KEYCODE_KP1;
	case SDLK_KP_2: return Common::KEYCODE_KP2;
	case SDLK_KP_3: return Common::KEYCODE_KP3;
	case SDLK_KP_4: return Common::KEYCODE_KP4;
	case SDLK_KP_5: return Common::KEYCODE_KP5;
	case SDLK_KP_6: return Common::KEYCODE_KP6;
	case SDLK_KP_7: return Common::KEYCODE_KP7;
	case SDLK_KP_8: return Common::KEYCODE_KP8;
	case SDLK_KP_9: return Common::KEYCODE_KP9;
	case SDLK_KP_ENTER: return Common::KEYCODE_KP_ENTER;
	case SDLK_KP_PLUS: return Common::KEYCODE_KP_PLUS;
	case SDLK_KP_MINUS: return Common::KEYCODE_KP_MINUS;
	case SDLK_KP_MULTIPLY: return Common::KEYCODE_KP_MULTIPLY;
	case SDLK_KP_DIVIDE: return Common::KEYCODE_KP_DIVIDE;
	case SDLK_KP_PERIOD: return Common::KEYCODE_KP_PERIOD;
	case SDLK_LSHIFT: return Common::KEYCODE_LSHIFT;
	case SDLK_RSHIFT: return Common::KEYCODE_RSHIFT;
	case SDLK_LCTRL: return Common::KEYCODE_LCTRL;
	case SDLK_RCTRL: return Common::KEYCODE_RCTRL;
	case SDLK_LALT: return Common::KEYCODE_LALT;
	case SDLK_RALT: return Common::KEYCODE_RALT;
	case SDLK_CAPSLOCK: return Common::KEYCODE_CAPSLOCK;
	case SDLK_NUMLOCKCLEAR: return Common::KEYCODE_NUMLOCK;
	case SDLK_SCROLLLOCK: return Common::KEYCODE_SCROLLOCK;
	case SDLK_PAUSE: return Common::KEYCODE_PAUSE;
	default: return Common::KEYCODE_INVALID;
	}
}

static byte toFlags(SDL_Keymod mod) {
	byte f = 0;
	if (mod & SDL_KMOD_SHIFT) f |= Common::KBD_SHIFT;
	if (mod & SDL_KMOD_CTRL) f |= Common::KBD_CTRL;
	if (mod & SDL_KMOD_ALT) f |= Common::KBD_ALT;
	if (mod & SDL_KMOD_CAPS) f |= Common::KBD_CAPS;
	if (mod & SDL_KMOD_NUM) f |= Common::KBD_NUM;
	return f;
}

bool DosEventSource::pollEvent(Common::Event &event) {
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_EVENT_QUIT:
			event.type = Common::EVENT_QUIT;
			return true;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP: {
			event.type = (ev.type == SDL_EVENT_KEY_DOWN) ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP;
			event.kbdRepeat = ev.key.repeat;
			event.kbd.keycode = toKeyCode(ev.key.key);
			event.kbd.flags = toFlags(ev.key.mod);
			// The shifted character for this layout, e.g. 'A' or '!'.
			const SDL_Keycode shifted = SDL_GetKeyFromScancode(ev.key.scancode, ev.key.mod, true);
			event.kbd.ascii = (shifted >= 0x20 && shifted < 0x7F) ? (uint16)shifted
							: (event.kbd.keycode == Common::KEYCODE_RETURN || event.kbd.keycode == Common::KEYCODE_KP_ENTER) ? Common::ASCII_RETURN
							: (event.kbd.keycode == Common::KEYCODE_ESCAPE) ? Common::ASCII_ESCAPE
							: (event.kbd.keycode == Common::KEYCODE_BACKSPACE) ? Common::ASCII_BACKSPACE
							: (event.kbd.keycode == Common::KEYCODE_TAB) ? Common::ASCII_TAB
							: (event.kbd.keycode >= Common::KEYCODE_F1 && event.kbd.keycode <= Common::KEYCODE_F12)
								? (uint16)(Common::ASCII_F1 + (event.kbd.keycode - Common::KEYCODE_F1)) : 0;
			if (event.kbd.keycode == Common::KEYCODE_INVALID && !event.kbd.ascii)
				continue;
			return true;
		}
		case SDL_EVENT_MOUSE_MOTION:
			event.type = Common::EVENT_MOUSEMOVE;
			event.mouse = _gfx->gameMouse(ev.motion.x, ev.motion.y);
			_gfx->setMousePos(event.mouse.x, event.mouse.y);
			return true;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP: {
			const bool down = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
			if (ev.button.button == SDL_BUTTON_LEFT)
				event.type = down ? Common::EVENT_LBUTTONDOWN : Common::EVENT_LBUTTONUP;
			else if (ev.button.button == SDL_BUTTON_RIGHT)
				event.type = down ? Common::EVENT_RBUTTONDOWN : Common::EVENT_RBUTTONUP;
			else
				continue;
			event.mouse = _gfx->gameMouse(ev.button.x, ev.button.y);
			return true;
		}
		default:
			break;
		}
	}
	return false;
}

#endif
```

- [ ] **Step 3: OSystem_DOS 에 연결**

`backends/platform/dos/module.mk` 에 `../../events/dos/dos-events.o` 를 더한다.

`dos.h`: `private:` 의 `_eventSource` 주석을 `///< a DosEventSource; OSystem_DOS::pollEvent() runs the timers first` 로 바꾼다.

`dos.cpp`:
- `#include "backends/events/dos/dos-events.h"` 추가.
- `initBackend()` 에서 `_graphicsManager` 를 먼저 만들고, 이벤트 소스를 그 뒤에 만든다:

```cpp
	DosGraphicsManager *gfx = new DosGraphicsManager();
	_graphicsManager = gfx;
	_eventSource = new DosEventSource(gfx);
	_timerManager = new DefaultTimerManager();
	_eventManager = new DefaultEventManager(this);
```

- `pollEvent()`:

```cpp
bool OSystem_DOS::pollEvent(Common::Event &event) {
	((DefaultTimerManager *)getTimerManager())->checkTimers();
	((NullMixerManager *)_mixerManager)->update(1);
	return _eventSource->pollEvent(event);
}
```

- 생성자 초기화 `_eventSource(this)` 를 `_eventSource(nullptr)` 로, 소멸자에 `delete _eventSource;` 를 넣는다
  (`DefaultEventManager` 의 보스는 `this` 이므로 이벤트 소스를 따로 지운다).

- [ ] **Step 4: 빌드**

Run: `backends/platform/dos/build-dos.sh 2>&1 | tail -5`
Expected: 링크 성공.

- [ ] **Step 5: Commit**

```bash
git add backends/events/dos backends/platform/dos
git commit -m "DOS: Keyboard and mouse from SDL3 as ScummVM events"
```

(키 매핑 동작은 Task 9 의 수락 테스트가 아닌 수동 확인 대상이다: 디버그 채널은 `EventManager::pushEvent` 로
입력을 넣으므로 SDL 경로를 지나지 않는다. M1 계획에서 DOSBox-X 키 주입으로 자동화한다.)

---

### Task 8: COM 포트 디버그 채널 (`DEBUGSOCKET_DOSCOM`)

**Files:**
- Create: `gui/debugsocket-dosuart.h`, `gui/debugsocket-dosuart.cpp`
- Modify: `gui/debugsocket.h` (전송 선택, 멤버), `gui/debugsocket.cpp` (ctor/dtor/listen/readLine/send), `gui/module.mk`

**Interfaces:**
- Produces: `class GUI::DosUart` — `bool open(const Common::String &spec)` (`"com1"`, `"com2:38400"`),
  `int read(char *buf, int max)` (논블로킹), `void write(const char *p, uint len)`, `bool isOpen() const`.
  설정 키 `debug_socket=com1[:baud]` (기본 115200).
- 프로토콜 제약 (문서화): UART 수신 FIFO 16바이트를 프레임마다 한 번 비우므로 호스트는 **바이트 사이 2ms** 로 보낸다.

- [ ] **Step 1: UART 헤더와 구현**

`gui/debugsocket-dosuart.h`:

```cpp
/* ScummVM - Graphic Adventure Engine
 * (GPLv3 header)
 */

#ifndef GUI_DEBUGSOCKET_DOSUART_H
#define GUI_DEBUGSOCKET_DOSUART_H

#include "common/str.h"

namespace GUI {

/**
 * A 16550 UART polled from the game loop, the DOS transport of the debug
 * socket. No interrupts: the receive FIFO holds 16 bytes and is drained
 * once a frame, so the host paces what it sends (2 ms a byte).
 */
class DosUart {
public:
	DosUart() : _base(0) {}

	/** "com1".."com4", optionally ":<baud>" (a divisor of 115200). */
	bool open(const Common::String &spec);
	bool isOpen() const { return _base != 0; }
	/** Whatever has arrived, up to max bytes; never waits. */
	int read(char *buf, int max);
	/** Blocks until every byte is in the transmit register. */
	void write(const char *p, uint len);

private:
	uint16 _base;
};

} // End of namespace GUI

#endif
```

`gui/debugsocket-dosuart.cpp`:

```cpp
/* ScummVM - Graphic Adventure Engine
 * (GPLv3 header)
 */

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <pc.h>

#include "gui/debugsocket-dosuart.h"
#include "common/textconsole.h"

namespace GUI {

static const uint16 kComBase[4] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };

bool DosUart::open(const Common::String &spec) {
	Common::String s = spec;
	s.toLowercase();
	if (!s.hasPrefix("com") || s.size() < 4 || s[3] < '1' || s[3] > '4') {
		warning("DebugSocket: '%s' is not com1..com4", spec.c_str());
		return false;
	}
	uint32 baud = 115200;
	if (s.size() > 5 && s[4] == ':')
		baud = atol(s.c_str() + 5);
	if (baud == 0 || 115200 % baud) {
		warning("DebugSocket: %u baud does not divide 115200", (uint)baud);
		return false;
	}
	const uint16 base = kComBase[s[3] - '1'];
	outportb(base + 7, 0x5A);		// scratch register: is there a UART at all?
	if (inportb(base + 7) != 0x5A) {
		warning("DebugSocket: no UART at 0x%X", base);
		return false;
	}
	const uint16 div = (uint16)(115200 / baud);
	outportb(base + 1, 0x00);		// no interrupts
	outportb(base + 3, 0x80);		// DLAB
	outportb(base + 0, div & 0xFF);
	outportb(base + 1, div >> 8);
	outportb(base + 3, 0x03);		// 8N1
	outportb(base + 2, 0xC7);		// FIFO on, both cleared, 14-byte trigger
	outportb(base + 4, 0x03);		// DTR, RTS
	_base = base;
	debug(1, "DebugSocket: COM at 0x%X, %u baud", base, (uint)baud);
	return true;
}

int DosUart::read(char *buf, int max) {
	int n = 0;
	while (n < max && (inportb(_base + 5) & 0x01))
		buf[n++] = (char)inportb(_base);
	return n;
}

void DosUart::write(const char *p, uint len) {
	for (uint i = 0; i < len; ++i) {
		while (!(inportb(_base + 5) & 0x20))
			;
		outportb(_base, (byte)p[i]);
	}
}

} // End of namespace GUI

#endif
```

`gui/module.mk` 의 `debugsocket-protocol.o \` 다음 줄에 `debugsocket-dosuart.o \` 를 더한다 (파일 내용이 `DOS_DJGPP` 로 감싸져 있어 다른 플랫폼에서는 빈 오브젝트다).

- [ ] **Step 2: 전송 선택과 멤버 (`gui/debugsocket.h`)**

전송 선택 블록을:

```cpp
#if defined(USE_DEBUG_SOCKET) && defined(DOS_DJGPP)
#define DEBUGSOCKET_DOSCOM
#elif defined(USE_DEBUG_SOCKET) && defined(WIN32)
#define DEBUGSOCKET_WIN32
#elif defined(USE_DEBUG_SOCKET) && defined(POSIX)
#define DEBUGSOCKET_POSIX
#endif
```

로 바꾸고 (DJGPP 는 POSIX 로 설정되므로 DOS 분기가 먼저 와야 한다), `#include "gui/debugsocket-protocol.h"` 아래에
`#include "gui/debugsocket-dosuart.h"` 를 더하고, 클래스 설명의 `debug_socket=<path>` 문단에
"on DOS `com1`..`com4[:baud]`, a 16550 polled once a frame; the host sends 2 ms apart" 를 덧붙인다.
멤버 블록:

```cpp
#if defined(DEBUGSOCKET_POSIX)
	int _listenFd, _clientFd;
#elif defined(DEBUGSOCKET_WIN32)
	...(그대로)
#elif defined(DEBUGSOCKET_DOSCOM)
	DosUart _uart;
#endif
```

- [ ] **Step 3: 전송 구현 (`gui/debugsocket.cpp`)**

1. `listen()` 의 `#elif defined(DEBUGSOCKET_WIN32) ... return true;` 뒤, `#else` 앞에:

```cpp
#elif defined(DEBUGSOCKET_DOSCOM)
	if (!_uart.open(path))
		return false;
	_inBuf.clear();
	return true;
```

2. `readLine()` 의 `#elif defined(DEBUGSOCKET_WIN32)` 블록 뒤, `#else` 앞에:

```cpp
#elif defined(DEBUGSOCKET_DOSCOM)
	if (!_uart.isOpen())
		return false;
	char buf[64];
	int n;
	while ((n = _uart.read(buf, sizeof(buf))) > 0)
		_inBuf += Common::String(buf, n);
```

그리고 줄 자르기 블록의 조건 `#if defined(DEBUGSOCKET_POSIX) || defined(DEBUGSOCKET_WIN32)` 를
`#if defined(DEBUGSOCKET_POSIX) || defined(DEBUGSOCKET_WIN32) || defined(DEBUGSOCKET_DOSCOM)` 로 바꾼다
(252행, 같은 조건이 다른 곳에도 있으면 모두).

3. `send()` 의 `#elif defined(DEBUGSOCKET_WIN32)` 블록 뒤, `#else` 앞에:

```cpp
#elif defined(DEBUGSOCKET_DOSCOM)
	if (_uart.isOpen())
		_uart.write(all.c_str(), all.size());
```

4. `pollAccept()`, 생성자 초기화 목록, 소멸자: DOS 분기는 할 일이 없다 (연결 개념 없음, UART 는 열린 채로 둔다).

- [ ] **Step 4: 리눅스에서 기존 동작이 그대로인지**

Run: `cd ~/work/scummvm/builds/linux-dos-test && make -j$(nproc) >/dev/null && make test 2>&1 | tail -3`
Expected: 빌드 성공, 테스트 통과 (debugsocket-protocol 테스트 포함).

- [ ] **Step 5: DOS 빌드**

Run: `cd ~/work/scummvm/dos && backends/platform/dos/build-dos.sh 2>&1 | tail -5`
Expected: 링크 성공. `grep USE_DEBUG_SOCKET build-dos/config.h` 가 `#define USE_DEBUG_SOCKET` 를 보인다.

- [ ] **Step 6: Commit** (공통 코드)

```bash
git add gui/debugsocket-dosuart.h gui/debugsocket-dosuart.cpp gui/debugsocket.h gui/debugsocket.cpp gui/module.mk
git commit -m "GUI: Carry the debug socket over a polled 16550 COM port on DOS"
```

---

### Task 9: 하네스와 M0 수락 테스트

**Files (하네스 저장소 `~/work/scummvm`):**
- Modify: `harness/i18n/scigame.py` — `_TcpConn`, `SciGame.attach_tcp()`
- Create: `harness/dos/dosgame.py`, `harness/dos/m0_accept.py`

**Interfaces:**
- Consumes: `SciGame.cmd/key/wait_text/wait_idle/dump` (기존), `dist/dos/*.EXE` (Task 3), `debug_socket=com1` (Task 8)
- Produces:
  - `SciGame.attach_tcp(host, port, timeout=60) -> SciGame` — 연결될 때까지 재시도, 바이트 간 2ms 로 쓴다
  - `dosgame.launch(emu, gamedir, out, gameid="kq1sci", port=5555, extra_ini="") -> SciGame` — `emu` 는 `"x"` 또는 `"staging"`.
    `out/c` 가 C:, `gamedir` 가 D:, 덤프는 `C:\D\` 아래(호스트 `out/c/D/`)
  - `m0_accept.py <emu>` — 종료 코드 0 이면 통과

- [ ] **Step 1: SciGame 에 TCP 전송 추가**

`harness/i18n/scigame.py`, `class _PipeConn` 뒤에:

```python
class _TcpConn:
    """A TCP stream to DOSBox's null-modem port, with the socket methods
    cmd() uses. The DOS side drains a 16-byte UART FIFO once a frame, so
    every byte goes out 2 ms after the last one."""

    def __init__(self, host, port):
        self.s = socket.create_connection((host, port))

    def settimeout(self, t):
        self.s.settimeout(t)

    def sendall(self, data):
        for b in data:
            self.s.sendall(bytes((b,)))
            time.sleep(0.002)

    def recv(self, n):
        return self.s.recv(n)

    def close(self):
        self.s.close()
```

`class SciGame` 의 `connect()` 뒤에:

```python
    @classmethod
    def attach_tcp(cls, host, port, timeout=60):
        """Connect to a game that is already running (under DOSBox), retrying
        until its null-modem port accepts."""
        g = cls("%s:%d" % (host, port))
        deadline = time.time() + timeout
        while True:
            try:
                g.sock = _TcpConn(host, port)
                return g
            except OSError:
                if time.time() > deadline:
                    raise RuntimeError("no null-modem port at %s:%d after %ds" % (host, port, timeout))
                time.sleep(0.5)
```

`cmd()` 가 `self.sock` 에서 쓰는 메서드(`sendall`, `recv`, `settimeout`)가 `_TcpConn` 에 모두 있는지 확인한다:
`grep -n "self.sock\." harness/i18n/scigame.py`. 다른 메서드를 쓰면 `_TcpConn` 에 같은 이름으로 위임을 더한다.

- [ ] **Step 2: DOSBox 실행기**

`harness/dos/dosgame.py`:

```python
"""Run the DOS build of ScummVM under DOSBox-X or DOSBox Staging, headless,
and hand back a SciGame talking to it over the emulated COM1."""
import os
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "i18n"))
from scigame import SciGame  # noqa: E402

DIST = os.path.expanduser("~/work/scummvm/dos/dist/dos")
STAGING = os.path.expanduser("~/opt/dosbox-staging-linux-x86_64-0.83.0-7b400/dosbox")
XVFB = ["xvfb-run", "-a", "-s", "-screen 0 1024x768x24"]


def _conf(out, gamedir, gameid, port):
    return """[dosbox]
machine = svga_s3
memsize = 16
[cpu]
cycles = 60000
[serial]
serial1 = nullmodem port:%d transparent:1
[autoexec]
mount c "%s"
mount d "%s"
c:
SCUMMVM.EXE %s
exit
""" % (port, os.path.join(out, "c"), gamedir, gameid)


def launch(emu, gamedir, out, gameid="kq1sci", port=5555, extra_ini=""):
    c = os.path.join(out, "c")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(c, "D"))
    for f in ("SCUMMVM.EXE", "CWSDPMI.EXE"):
        shutil.copy(os.path.join(DIST, f), c)
    with open(os.path.join(c, "SCUMMVM.INI"), "w") as f:
        f.write("[scummvm]\n[%s]\ngameid=%s\npath=D:\\\ndebug_socket=com1\n%s" % (gameid, gameid, extra_ini))
    conf = os.path.join(out, "run.conf")
    with open(conf, "w") as f:
        f.write(_conf(out, gamedir, gameid, port))
    if emu == "x":
        cmd = XVFB + ["dosbox-x", "-conf", conf, "-nopromptfolder", "-fastlaunch"]
    elif emu == "staging":
        cmd = XVFB + [STAGING, "--noprimaryconf", "--conf", conf]
    else:
        raise ValueError("emu is 'x' or 'staging', not %r" % emu)
    env = dict(os.environ, SDL_AUDIODRIVER="dummy")
    log = open(os.path.join(out, "emu.log"), "w")
    proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
    g = SciGame.attach_tcp("127.0.0.1", port, timeout=90)
    g.proc = proc
    g.log = log
    return g


def dos_dump(g, out, name):
    """Dump into C:\\D\\<name> (one letter: the dump appends up to 7 characters
    and DOS names are 8.3) and return the host-side prefix."""
    assert len(name) == 1
    g.dump("C:\\D\\" + name)
    return os.path.join(out, "c", "D", name)
```

- [ ] **Step 3: 수락 테스트**

`harness/dos/m0_accept.py`:

```python
"""M0 acceptance: KQ1 reaches its title screen under DOS, answers on COM1,
and its 320x200 frame and palette equal the Linux build's, byte for byte.

    python3 harness/dos/m0_accept.py x|staging
"""
import filecmp
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "i18n"))
import dosgame  # noqa: E402
from scigame import SciGame  # noqa: E402

GAME = os.path.expanduser("~/work/scummvm/gamedata/King's Quest 1 - Quest for the Crown (DOS 1991 Remake)")
LINUX = os.path.expanduser("~/work/scummvm/builds/linux-dos-test/scummvm")
RUNS = os.path.expanduser("~/work/scummvm/runs/dos-m0")


def title(g):
    g.key("Return")
    g.wait_text("Begin Game", 600)
    g.wait_idle(3, 100)


def main():
    emu = sys.argv[1]
    ref = os.path.join(RUNS, "linux")
    os.makedirs(ref, exist_ok=True)
    with SciGame.launch(GAME, out=ref, binary=LINUX, headless=True) as g:
        title(g)
        g.dump(os.path.join(ref, "T"))
    out = os.path.join(RUNS, emu)
    with dosgame.launch(emu, GAME, out) as g:
        print("state:", g.cmd("state")[:200])
        title(g)
        dos = dosgame.dos_dump(g, out, "T")
    ok = True
    for suffix in ("_low.bin", "_pal.bin"):
        same = filecmp.cmp(os.path.join(ref, "T" + suffix), dos + suffix, shallow=False)
        print("T%s: %s" % (suffix, "same" if same else "DIFFERENT"))
        ok &= same
    print("M0 %s: %s" % (emu, "PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
```

(`SciGame.close()` 가 `quit` 을 보낸 뒤 `proc` 을 기다리고, 안 끝나면 죽인다 — DOSBox 는 autoexec 의 `exit` 로 끝난다.)

- [ ] **Step 4: 리눅스 기준 빌드**

Run: `cd ~/work/scummvm/builds/linux-dos-test && make -j$(nproc) 2>&1 | tail -2 && ls -la scummvm`
Expected: `scummvm` 실행 파일 (Task 4 에서 configure 한 트리, `--enable-debug-socket`).

- [ ] **Step 5: 수락 테스트 실행 — DOSBox-X**

Run: `cd ~/work/scummvm && python3 harness/dos/m0_accept.py x`
Expected: `state:` 한 줄 (COM 왕복 성공), `T_low.bin: same`, `T_pal.bin: same`, `M0 x: PASS`.

실패하면 `runs/dos-m0/x/c/SCUMMVM.LOG` 와 `runs/dos-m0/x/emu.log` 를 먼저 본다. 원인을 고치는 변경은
해당 태스크의 파일에서 하고 그 태스크의 커밋 형식으로 커밋한다.

- [ ] **Step 6: 수락 테스트 실행 — Staging**

Run: `cd ~/work/scummvm && python3 harness/dos/m0_accept.py staging`
Expected: `M0 staging: PASS`. (Staging 의 `transparent:1` 지원이 다르면 `emu.log` 에 경고가 남는다 — 그 경우 옵션을
Staging 문법으로 바꾸고 `dosgame.py` 에서 에뮬레이터별로 나눈다.)

- [ ] **Step 7: Commit** (하네스 저장소)

```bash
cd ~/work/scummvm
git add harness/i18n/scigame.py harness/dos/dosgame.py harness/dos/m0_accept.py
git commit -m "harness/dos: run the DOS build under DOSBox over COM1, and the M0 acceptance test"
```

---

### Task 10: spec 갱신과 M0 마감

**Files:**
- Modify: `docs/superpowers/specs/2026-09-28-scummvm-dos-sdl3-design.md`
- Modify: `harness/dos/spikes/RESULTS.md` (하네스 저장소)

- [ ] **Step 1: spec 에 M0 결과를 반영**

- 5.4 "IRQ0 타이머 (1kHz)" 를 Task 1 결론에 맞게 고친다: RTC 가 동작했으면 제목을 "타이머 (RTC IRQ8, 1024Hz)" 로,
  본문의 "PIT 을 1kHz 로 설정" 을 "RTC 주기 인터럽트 rate 6 (1024Hz), PIT 은 건드리지 않는다 (SDL3 의 `uclock()` 이
  PIT 기본 주기를 가정한다)" 로 바꾼다. 다른 결론이면 그 결론을 적는다.
- 6장 COM 채널에 "호스트는 바이트 사이 2ms 로 보낸다 (FIFO 16바이트를 프레임마다 비움). IRQ 수신은 M3" 을 더한다.
- 7.3 의 "DOS 빌드 전용 `--dump-frames=<목록>`" 을 "debug socket 의 기존 `dump <prefix>` 명령 (SCI 는
  `_low/_scaled/_pal/_ctl/_pri`). DOS 에서는 접두어를 1자로 둔다 (8.3)" 로 바꾼다.
- 8장 M0 행에 `harness/dos/spikes/RESULTS.md` 링크와 "통과: DOSBox-X / Staging" 을 적는다.

- [ ] **Step 2: Commit**

```bash
cd ~/work/scummvm/dos
git add docs/superpowers/specs/2026-09-28-scummvm-dos-sdl3-design.md
git commit -m "docs: DOS design after M0 — timer source, COM pacing, dumps through the debug socket"
```
