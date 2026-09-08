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
        'reaches CharsetRendererV3::printChar, but its font is compiled into '
        'ScummVM at a fixed 8px rather than read from the game, so a '
        'replacement is governed by the whole-multiple scale policy',
    'CharsetRendererNES':
        'NES tile font, single byte, no replacement path designed',
    'CharsetRendererMac':
        'draws every glyph twice and uses _textSurface as a stencil; '
        'see the notes on the inverted Mac data flow',
    'CharsetRendererPCE':
        'inherits the CharsetRendererV3::printChar hook, which offers the '
        'same character before drawBits1 runs; its own hook would be dead '
        'code. Its 16bpp branch draws to the VirtScreen, and the layer '
        'writes byte indices, so that path stays with the System Card font',
    'CharsetRendererV7':
        'overrides printChar with an error() stub - v7 text goes through '
        'TextRenderer_v7 and lands in draw2byte/drawCharV7, neither hooked; '
        'open work, and the reason FT/Dig have no hi-res text',
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

# Which class each one derives from, so an inherited hook counts. A renderer
# that overrides only drawBits1 still reaches the layer when its base class
# offers the character in printChar first - and adding a second hook there
# would be dead code, which is how the PCE hook was written and reverted.
BASE = dict(re.findall(r'^class (CharsetRenderer\w*)\s*:\s*public\s+(\w+)', HDR, re.M))

# Methods each class declares in the header, including inline bodies. A hook
# in a base class does not run for a subclass that overrides the method
# carrying it - and CharsetRendererV7 overrides printChar inline, with a body
# that calls error(), so scanning charset.cpp alone would miss it.
decls = {}
for m in re.finditer(r'^class (CharsetRenderer\w*)\b', HDR, re.M):
    cls = m.group(1)
    start = HDR.find('{', m.end())
    if start < 0:
        continue
    depth, i = 0, start
    while i < len(HDR):
        if HDR[i] == '{':
            depth += 1
        elif HDR[i] == '}':
            depth -= 1
            if depth == 0:
                break
        i += 1
    decls[cls] = set(re.findall(r'\b(\w+)\s*\([^;{]*\)\s*(?:const\s*)?override', HDR[start:i]))


def declares(cls, meth):
    return meth in decls.get(cls, ()) or any(m == meth for m, _ in bodies.get(cls, []))


def own_hooks(cls):
    return sorted({m for m, b in bodies.get(cls, [])
                   if m in DRAWING and '_hiResText.drawChar' in b})


def inherited_hook(cls):
    """The nearest ancestor whose hook this class actually reaches.

    An ancestor's hook only counts when this class does not override the
    method carrying it. CharsetRendererV2 and CharsetRendererV7 both define
    their own printChar, so the hook in their base never runs for them.
    """
    seen = set()
    child = cls
    cur = BASE.get(cls)
    while cur and cur not in seen:
        seen.add(cur)
        for meth in own_hooks(cur):
            # walk from cls up to cur; an override anywhere in between cuts it
            overridden = False
            walk = cls
            while walk != cur:
                if declares(walk, meth):
                    overridden = True
                    break
                walk = BASE.get(walk)
                if walk is None:
                    break
            if not overridden:
                return cur, meth
        child = cur
        cur = BASE.get(cur)
    return None, None


print('%-32s %-10s %s' % ('renderer', 'hook', 'where / why'))
print('-' * 78)
missing = []
hooked = 0
for cls in classes:
    hooks = own_hooks(cls)
    via, meth = inherited_hook(cls)
    if hooks:
        hooked += 1
        print('%-32s %-10s %s' % (cls, 'yes', ', '.join(hooks)))
    elif via:
        hooked += 1
        print('%-32s %-10s %s' % (cls, 'inherited', 'via %s::%s' % (via, meth)))
    elif cls in EXEMPT:
        print('%-32s %-10s %s' % (cls, 'exempt', EXEMPT[cls]))
    else:
        print('%-32s %-10s %s' % (cls, 'NO', '*** unaccounted for ***'))
        missing.append(cls)

print()
print('%d renderers: %d reach the layer, %d exempt, %d unaccounted' %
      (len(classes), hooked, len(classes) - hooked - len(missing), len(missing)))

if missing:
    print()
    print('Add a hook, or record why it needs none:')
    for c in missing:
        print('   ', c)
    sys.exit(1)
