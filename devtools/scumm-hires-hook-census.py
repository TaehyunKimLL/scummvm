#!/usr/bin/env python3
"""Which charset renderers reach the hi-res layer, and which do not?

The FM-Towns bug was not a wrong line of code - it was a renderer nobody
had checked. Its fonts loaded, its log looked right, and every double-byte
glyph came from the ROM regardless. It stayed that way for weeks because
nothing measures which renderers are wired up.

This is that measurement: every class deriving from CharsetRenderer, and
whether its drawing path calls into _hiResText. A renderer that legitimately
has no hook needs a reason recorded here, so the list is a decision rather
than an oversight.

Run it after touching charset.cpp. Exit code is non-zero when a renderer is
unaccounted for.
"""
import re
import sys
import pathlib

SRC = pathlib.Path(__file__).resolve().parent.parent
if not (SRC / 'engines/scumm/charset.cpp').exists():
    sys.exit('run this from a ScummVM checkout: engines/scumm/charset.cpp not found')

HDR = (SRC / 'engines/scumm/charset.h').read_text()
CPP = (SRC / 'engines/scumm/charset.cpp').read_text()

# Renderers that draw no glyphs of their own, with the reason.
EXEMPT = {
    'CharsetRenderer':
        'abstract base',
    'CharsetRendererCommon':
        'abstract; setCurID feeds the layer but draws nothing',
    'CharsetRendererPC':
        'abstract; drawBits1 is used through its subclasses',
    'CharsetRendererV2':
        'font is compiled into ScummVM at a fixed 8px, not a game resource',
    'CharsetRendererNES':
        'NES tile font, single byte, no replacement path designed',
    'CharsetRendererMac':
        'draws every glyph twice and uses _textSurface as a stencil; '
        'see the notes on the inverted Mac data flow',
    'CharsetRendererPCE':
        'PC-Engine System Card font; hooked for a paletted destination, '
        'left alone at 16bpp where there is no index to write',
    'CharsetRendererV7':
        'v7 text goes through TextRenderer_v7, not printChar; open',
    'CharsetRendererNut':
        'v8/SMUSH NutRenderer; open',
}

classes = re.findall(r'^class (CharsetRenderer\w*)\s*:?[^{]*\{', HDR, re.M)
classes = ['CharsetRenderer'] + [c for c in classes if c != 'CharsetRenderer']
classes = list(dict.fromkeys(classes))

# Method bodies per class, so a hook is attributed to the right renderer.
bodies = {}
for m in re.finditer(r'^(?:\w[\w:<>,&*\s]*?)?(CharsetRenderer\w*)::(\w+)\(', CPP, re.M):
    cls, meth = m.group(1), m.group(2)
    start = CPP.find('{', m.end())
    if start < 0:
        continue
    depth, i = 0, start
    while i < len(CPP):
        if CPP[i] == '{':
            depth += 1
        elif CPP[i] == '}':
            depth -= 1
            if depth == 0:
                break
        i += 1
    bodies.setdefault(cls, []).append((meth, CPP[start:i]))

DRAWING = ('printChar', 'drawChar', 'drawBits1', 'drawBitsN', 'printCharIntern',
           'printCharInternal', 'drawCharV7', 'draw2byte')

print('%-32s %-8s %s' % ('renderer', 'hook', 'where / why'))
print('-' * 78)
missing = []
for cls in classes:
    hooks = []
    for meth, body in bodies.get(cls, []):
        if meth in DRAWING and '_hiResText.drawChar' in body:
            hooks.append(meth)
    if hooks:
        print('%-32s %-8s %s' % (cls, 'yes', ', '.join(sorted(set(hooks)))))
    elif cls in EXEMPT:
        print('%-32s %-8s %s' % (cls, 'exempt', EXEMPT[cls]))
    else:
        print('%-32s %-8s %s' % (cls, 'NO', '*** unaccounted for ***'))
        missing.append(cls)

print()
drawn = sum(1 for c in classes
            if any(m in DRAWING and '_hiResText.drawChar' in b
                   for m, b in bodies.get(c, [])))
print('%d renderers: %d hooked, %d exempt, %d unaccounted' %
      (len(classes), drawn, len(classes) - drawn - len(missing), len(missing)))

if missing:
    print()
    print('Add a hook, or record why it needs none:')
    for c in missing:
        print('   ', c)
    sys.exit(1)
