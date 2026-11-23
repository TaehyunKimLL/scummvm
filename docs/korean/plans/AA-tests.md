# Anti-aliased (2 bpp, 8 bpp) faces: tests, CLUT8 policy, game runs

Owner request (2026-11-23): "AA 폰트도 테스트 해야 함", the faces generated from TrueType ("TrueType에서 생성할 것"), and chains of
a jamo set (조합형) with a precomposed face (완성형) ("조합형 + 완성형 AA 조합으로 테스트 해야 함"). Until now every golden and
every game run used 1 bpp faces (NEODG16A, the NEODG16G-derived fixture).

## 1. CLUT8 policy (what 2 bpp text looks like when the plane is stamped)

On CLUT8 (SCI 16-colour EGA and 256-colour games) `TextCompose::stampSpan` writes the text index where coverage >= 128
(`kStamp`, default threshold 128); `SvfFont` thresholds the same way on a CLUT8 surface. So of the four levels of a 2 bpp
face (coverage 0, 85, 170, 255) the levels **170 and 255 survive, 85 is dropped**. At 3/2 the coverage has other values and the
same cut at 128 applies. There is no ramp: a CLUT8 game's palette has no spare entries for one.

Measured with `svfline.py` on "한글 게임을 시작합니다. Hello 123" (cell 16, one line):

| face | pixels with level 85 / 170 / 255 | stamped at >= 128 | stamped at >= 64 | coverage-weighted ink (sum / 255) |
|---|---|---|---|---|
| NEODG16A (1 bpp, the shipped fallback) | 0 / 0 / 911 | 911 | 911 | 911 |
| NG16P (NanumGothic Bold 16 px, precomposed 2 bpp) | 293 / 300 / 762 | 1062 | 1355 | 1060 |
| NG16JA (the same as a jamo set 844, 2 bpp) | 375 / 433 / 657 | 1090 | 1465 | 1071 |

* The threshold 128 keeps the ink of the face: the stamped pixel count (1062, 1090) equals the coverage-weighted ink (1060, 1071)
  within 2 %, because the dropped 85 pixels and the kept 170 pixels balance. The stamped AA text is about 17 % heavier than the
  1 bpp NEODG16A line, which is the difference of the two designs (NanumGothic Bold against Neo Dunggeunmo), not a loss of the
  stamp.
* Lowering the threshold to 64 (keeping level 85) makes the stroke 28 % heavier than the AA design and the text clots
  (`test_dumpWhenAsked`, `aa-dump/clut8-policy.png`: rows 5, 8, 11 of the strip).
* The strip (3 faces/chains x stamp 128, stamp 64, blend; each pixel 3x3): the 1 bpp face and the 128 stamps of the 2 bpp faces
  are equally crisp and legible; the stamped 2 bpp text has the 16 px shapes of NanumGothic with one-pixel strokes where the AA
  outline was thin, no gaps in the syllables; the 64 stamp is clearly worse (blobs); the blend is the smooth reference.

**Recommendation: keep the threshold 128 for AA faces on CLUT8; no code change.** The evidence does not show the stamped AA text
to be worse than the 1 bpp text. A lower threshold is worse; a ramp needs palette entries the games do not have.
