I'll review this task. Let me start by reading the package files.
I've read the full diff (5543 lines), the design document, the P3 plan (including its conflict/deviations section), the previous reviewer's findings (P3-review.md), the fixes (P3-fixes.md), the running log (P3-notes.md), and PATCHES.md. I've also read the key implementation files in the checkout to verify the code as actually committed. Here is my review.

---

## Review: P3 SCI KQ1 end-to-end (second, independent review)

Scope covered beyond the prior review: the input path (`adopt`+`key` integration), the UTF-8 heap helpers (`utf8.cpp`), `seg_manager` bounded copies, the upscaled-driver compositing path, the text16 decode loops, the parser `roles` bookkeeping, and the `GfxFont` ownership/destruction order. The prior review's M1–M3 and m1–m9 are already fixed or documented; I did not re-raise them.

### Important

**I1. A key that opens a freshly-created prompt is processed twice (OS-IME syllable doubled; built-in composer's first jamo double-fed).**
`engines/sci/graphics/controls16.cpp:189-195` calls both `adopt()` and `key()` for the same event, and both consume the same key:

```cpp
bool changed = _koreanLine.adopt(text, cursor, events->hangulMode(), events->lastOsImeKey(), maxChars);
if (...type == kSciEventKeyDown) {
    const uint16 key = ... message ...;
    changed = _koreanLine.key(key, ..., events->lastOsImeKey(), text, cursor, maxChars) == kChanged || changed;
}
```

`KoreanEditLine::adopt()` (`korean_input.cpp:80-95`) exists precisely to take over the case "the script just opened the prompt with its first key already in it": for the OS-IME surrogate it replaces `'x'` with the real syllable (`korean_input.cpp:80-86`), and for a jamo key it feeds the composer (`:88-95`). Then `key()` runs with the *same* event and consumes it *again*: the OS-IME branch re-inserts the same syllable (`:106-114`), the jamo branch re-feeds the same jamo (`:116-128`).

Concrete failure (OS-IME, the DESIGN §6 primary path for Windows/Android/ibus): the user types one syllable `문` (U+BB38); the event manager hands the script the surrogate `'x'` (`event.cpp:342-350`), the SCI0 script opens the prompt with `'x'` in the field, and the edit control runs. `adopt()` turns `"x"` into `"문"` (returns true), then `key('x', 0xBB38)` sees `key == kOsImeSurrogateKey && osImeCp >= 0x100` and inserts `문` again → the field reads `문문`. The same applies to the built-in composer: the first jamo of a freshly-opened prompt is fed twice (`'a'` → `ㅁ` then `'a'` again → `ㅃ`), so a command typed into a fresh prompt starts with a mangled syllable.

The KQ1 headless runs did not catch this because they typed only through the built-in composer into an already-open (empty) prompt, where `adopt()` returns false (text `""` != size 1) and `key()` alone handles the key. The unit tests don't cover it either: `test/engines/sci/korean_input.h` `test_osImeSyllable` / `test_firstKeyOfANewPromptIsComposed` call `adopt()` then `remember()` then `key()` with a *different* code point, never `adopt()`+`key()` with the same event as `texteditChangeUtf8` does.

Fix: have `adopt()` report whether it consumed the first key (its "taken over" branch), and in `texteditChangeUtf8` skip the `key()` call for that event when it did; or restructure so `adopt()` only detects/commits external changes and the first-key takeover is folded into the single `key()` call. Add an integration-level test that drives `adopt()`+`key()` with one OS-IME event and asserts the syllable appears once.

---

No Critical findings; the bounded-copy paths (`SegManager::strncpy`, `strcpyBounded`, `utf8ReplaceAt`, `utf8Truncate`), the byte-offset `kStrAt` fallback (bounds-checked against `dest_r.maxSize`), the compositing scratch/row buffers (plane width 640 vs. 1280-byte rows), and the `GfxFontHiRes`/`SciHiResText` ownership order (`_gfxScreen` deleted before `_hiresText`; `GfxFontHiRes` deletes only `_original`) are all correct.

VERDICT: APPROVE WITH CHANGES
