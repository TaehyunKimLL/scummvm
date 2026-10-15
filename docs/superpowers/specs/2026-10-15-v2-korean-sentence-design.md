# SCUMM v2 Korean sentence line (Maniac Mansion, Zak McKracken)

Date: 2026-10-15. Code: `engines/scumm/korean_sentence.{h,cpp}` (pure, unit-tested in
`test/engines/scumm/korean_sentence.h`), wired in `ScummEngine_v2::drawSentence()` (`string.cpp`).

## Problem
The v2 engine builds the sentence line itself: verb resource + object A's name + a hard-coded
preposition (" in", " with", " on", " to"; `drawPreposition()`) + object B's name, in English order.
Only the verb was translated (when its resource was made); object names came from the OBCD (English),
and `drawString()` looked the whole line up in the bundle, which never matched: "주기 ticket to".
The line was cut at 40 bytes, which can split a CP949 pair.

## Where it applies
`koreanSentenceApplies()`: `version == 2`, not NES, `language == ko`, a translation bundle loaded
(CP949 or UTF-8). MM v1/C64 (v0, v1), the NES version and every other language keep the old path; the
old 40/60-character cut is now `cutV2Sentence()`, the same loop (a regression test pins it), bounded
by its buffer.

## Parts
- verb: the verb resource as it is (already translated); '@' padding and centring spaces dropped.
- A, B: `getObjOrActorName()` passed through `translateText()` (the same lookup the bundle's other
  lines get: script/room context, then a full search). A name a script set (already translated)
  finds no entry and stays as it is.
- preposition: the v2 index (1 in, 2 with, 3 on, 4 to) becomes a particle on B (table below).

## Order and particles (verb last; A takes no particle, as in "티켓 집기")

| state | Korean | example |
|---|---|---|
| verb only | `<V>` | 집기 |
| verb + A | `<A> <V>` | 티켓 집기, 문 향하기 |
| verb ending in '?' (What is) | `<V> <A>` (question first) | 이게 뭐지? 램프 |
| to, B a person (actor) | `<B>에게 <A> <V>` | 왕에게 티켓 주기 |
| to, B a thing | `<B>에 <A> <V>` | 문에 티켓 주기 |
| in / on | `<B>에 <A> <V>` | 녹음 붐박스에 DAT 테입 사용하기 |
| with (Use) | `<B>에 <A> <V>` | 새 모이통에 빵가루 사용하기 |
| with, B is the tool (MM Unlock, Fix) | `<B>(으)로 <A> <V>` | 열쇠로 문 잠금 풀기, 진공관으로 전화 고치기 |
| preposition, B not chosen yet | `...<particle> <A> <V>` | ...에게 티켓 주기, ...에 티켓 사용하기, ...(으)로 문 잠금 풀기 |
| two objects, no preposition | `<A> <B> <V>` | (not seen in MM/Zak) |

The particle table is data (`kPrepRules` in korean_sentence.cpp: person / thing / tool column per
preposition). "Tool" is chosen per verb: MM verb ids 6 (Fix) and 8 (Unlock), read from
VAR_SENTENCE_VERB in play (`ScummEngine_v2::sentenceToolIsObject2()`); Use's "with" names the target
as B, and Zak has no other two-object "with" verb (`ScummEngine_v0::verbPrepIdType()` lists the same
four MM verbs: Use, Give, Unlock, Fix).

Partial line: B shows as "..." with the particle it will take (both forms when it varies), so the
line keeps its shape while the player picks B and only "..." is replaced. "Walk to"/"What is" hover
works as before (the scripts set A), translated.

## Particle helper
`koreanFinalOf()` reads the last syllable's final consonant: UTF-8 by the code point
((cp - 0xAC00) % 28), CP949 by the KS X 1001 jongsung table (`checkJongsung()`, the 2350 syllables
the translations use). `koreanJosa()` gives 을/를, 은/는, 이/가, 과/와, (으)로 (ㄹ takes 로), 에, 에게.
- Latin letters, digits, symbols and CP949-extension syllables: read as vowel-final (를, 는, 가, 와, 로),
  with no pronunciation rule ("key로", "TV로"). MM's object names are still English in its korean.trs
  (the MM patch never translated them), so MM shows "door mat로 front door 잠금 풀기".
- nothing to judge (the "..." placeholder): both forms, 을(를), (으)로, 과(와) ...

## Width
The line is measured as `drawString()` draws it: `CharsetRenderer::getStringWidth()` with charset 1
(hi-res advances), against the verb screen's width (320 game px). If it is wider, the wider of A and B
loses its last whole character (trailing spaces with it) until it fits; B keeps the particle of its
full name. If both names are down to one character the line is cut after its last whole character
that fits. No step splits a CP949 pair or a UTF-8 sequence. With Zak's longest names (16 bytes) and the
longest verb the line is ~219 px (9 px a syllable), so the fit is a safety net; the old 40-byte cut no
longer applies to Korean lines.

## Not changed
Inventory (`redrawV2Inventory`), verb panel, dialogue, v0/v1/NES, non-Korean languages.
