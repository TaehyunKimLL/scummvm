#!/usr/bin/env python3
"""python3 tools/korean/test_charsetchars.py"""

import json
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import charsetchars as C  # noqa: E402
import scummscript as S  # noqa: E402
import scumm_trs_from_patch as T  # noqa: E402


def cs(n):
    return bytes([0x2C, 0x0D, n])


def cs_var(v):
    return bytes([0x2C, 0x0D | 0x80]) + struct.pack('<H', v)


def say(text):
    return bytes([0x14, 0xFF, 0x0F]) + text + b'\0'


def ego(text):
    return bytes([0xD8, 0x0F]) + text + b'\0'


def start(n):
    return bytes([0x0A, n, 0xFF])


def objname(text):
    return bytes([0x54]) + struct.pack('<H', 1) + text + b'\0'


def verb_new_named(text, vid=1):
    return bytes([0x7A, vid, 0x07, 0x02]) + text + b'\0' + bytes([0xFF])


def branch(then):
    """if (var 1 == 0) skip `then`."""
    return bytes([0x28]) + struct.pack('<H', 1) + struct.pack('<h', len(then)) + then


STOP = bytes([0x00])


def unit(ctx, tag, code):
    return S.Unit(ctx, tag, code, 0, len(code), [0], 0, 0, 5)


def glob(n, code):
    return unit((0, S.WIO_GLOBAL, n), 'SCRP', code)


def room(r, code, tag='ENCD'):
    return unit((r, S.WIO_ROOM, 0), tag, code)


def local(r, n, code):
    return unit((r, S.WIO_LOCAL, n), 'LSCR', code)


BOOT = glob(1, cs(2) + STOP)


def drawn(units, sentence=1, bundle=None):
    an = C.analyze(units)
    sets, unknown, used = C.decorate(an, bundle, sentence)
    return {k: ''.join(sorted(v)) for k, v in sets.items()}, ''.join(sorted(unknown)), an


class Attribution(unittest.TestCase):
    def test_straight_line(self):
        sets, unknown, _ = drawn([BOOT, glob(9, cs(4) + say(b'AB') + cs(2) + say(b'CD') + STOP)])
        self.assertEqual(sets[4], 'AB')
        self.assertIn('CD', sets[2])
        self.assertNotIn('A', sets[2])
        self.assertEqual(unknown, '')

    def test_entry_charset_is_the_one_the_game_rests_in(self):
        sets, _, an = drawn([BOOT, glob(9, say(b'XY') + STOP)])
        self.assertEqual(an.rest, {2})
        self.assertEqual(sets[2], 'XY')

    def test_game_that_never_sets_a_charset_draws_with_zero(self):
        sets, _, _ = drawn([glob(9, say(b'XY') + STOP)])
        self.assertEqual(sets[0], 'XY')

    def test_branch_merges_the_charsets_of_both_paths(self):
        code = cs(2) + branch(cs(5)) + say(b'M') + STOP
        sets, _, _ = drawn([BOOT, glob(9, code)])
        self.assertIn('M', sets[2])
        self.assertIn('M', sets[5])

    def test_loop_back_edge(self):
        body = say(b'L') + cs(6)
        back = bytes([0x18]) + struct.pack('<h', -(len(body) + 3))
        sets, _, _ = drawn([BOOT, glob(9, body + back + STOP)])
        self.assertIn('L', sets[2])
        self.assertIn('L', sets[6])

    def test_charset_after_a_branch_is_not_polluted_by_the_other_path(self):
        code = branch(cs(5)) + cs(3) + say(b'Z') + STOP
        sets, _, _ = drawn([BOOT, glob(9, code)])
        self.assertEqual(sets[3], 'Z')
        self.assertNotIn('Z', sets[5])

    def test_override_handler_sees_every_charset_of_the_cutscene(self):
        # beginOverride(1) + the jump to the ESC handler (skipped when running), body, handler
        body = cs(4) + say(b'B') + STOP
        handler = say(b'H') + STOP
        code = bytes([0x58, 1, 0x18]) + struct.pack('<h', len(body)) + body + handler
        sets, _, _ = drawn([BOOT, glob(9, code)])
        self.assertEqual(sets[4], 'BH')
        self.assertIn('H', sets[2])
        self.assertNotIn('B', sets[2])

    def test_callee_inherits_the_callers_charset(self):
        sets, _, _ = drawn([BOOT, glob(9, cs(6) + start(30) + STOP), glob(30, say(b'C') + STOP)])
        self.assertIn('C', sets[6])
        self.assertIn('C', sets[2])         # also entered from nowhere: the resting charset

    def test_charset_a_callee_leaves_reaches_the_caller_after_the_call(self):
        sets, _, _ = drawn([BOOT, glob(9, start(31) + say(b'P') + STOP), glob(31, cs(7) + STOP)])
        self.assertIn('P', sets[7])

    def test_local_scripts_are_called_in_their_room(self):
        units = [BOOT, room(5, cs(4) + start(201) + STOP), local(5, 201, say(b'Q') + STOP), local(6, 201, say(b'R') + STOP)]
        sets, _, _ = drawn(units)
        self.assertIn('Q', sets[4])
        self.assertNotIn('R', sets[4])

    def test_room_entry_script_sets_the_charset_for_its_room(self):
        units = [BOOT, room(5, cs(5) + STOP), local(5, 200, say(b'Q') + STOP), local(6, 200, say(b'R') + STOP)]
        sets, _, _ = drawn(units)
        self.assertIn('Q', sets[5])
        self.assertNotIn('R', sets[5])
        self.assertIn('R', sets[2])

    def test_variable_charset_is_unknown_and_in_every_charset(self):
        sets, unknown, _ = drawn([BOOT, glob(9, cs(4) + cs_var(0x10) + say(b'V') + STOP)])
        self.assertEqual(unknown, 'V')
        for k in (1, 2, 4):
            self.assertIn('V', sets[k])

    def test_print_ego_counts(self):
        sets, _, _ = drawn([BOOT, glob(9, cs(3) + ego(b'E') + STOP)])
        self.assertEqual(sets[3], 'E')

    def test_verb_names_go_to_the_charset_verbs_are_created_with(self):
        sets, _, an = drawn([BOOT, glob(9, cs(3) + verb_new_named(b'Open') + cs(2) + say(b'x') + STOP)])
        self.assertEqual(an.verbnew, {3})
        self.assertEqual(sets[3], 'Oenp')
        self.assertIn('O', sets[1])         # and the sentence line
        self.assertNotIn('O', sets[2])

    def test_object_names_go_to_the_sentence_line_and_to_verbs_that_show_names(self):
        code = (cs(3) + verb_new_named(b'\xff\x06\x86\x00') + cs(5) + verb_new_named(b'Look', 2) + cs(2) + objname(b'Key') + STOP)
        sets, _, _ = drawn([BOOT, glob(9, code)])
        self.assertIn('K', sets[1])
        self.assertIn('K', sets[3])         # an inventory verb: its text is a name
        self.assertNotIn('K', sets[5])      # a plain verb
        self.assertNotIn('K', sets[2])

    def test_verb_names_follow_their_verb_id(self):
        a = bytes([0x7A, 0x01, 0x07, 0x02]) + b'Aa\0' + bytes([0xFF])
        b = bytes([0x7A, 0x02, 0x07, 0x02]) + b'Bb\0' + bytes([0xFF])
        sets, _, an = drawn([BOOT, glob(9, cs(3) + a + cs(5) + b + STOP)])
        self.assertEqual(an.verbcs[1], {3})
        self.assertIn('A', sets[3])
        self.assertNotIn('A', sets[5])
        self.assertIn('B', sets[5])
        self.assertNotIn('B', sets[3])

    def test_names_inside_dialogue_reach_the_dialogue_charset(self):
        sets, _, _ = drawn([BOOT, glob(9, cs(4) + say(b'See \xff\x06\x01\x00!') + cs(2) + objname(b'Key') + STOP)])
        self.assertIn('K', sets[4])

    def test_object_block_names(self):
        ob = S.Unit((5, S.WIO_ROOM, 0), 'obname', b'Hook\0', 0, 4, [], 0, 0, 5)
        sets, _, _ = drawn([BOOT, ob])
        self.assertIn('H', sets[1])


def setstr(sid, text):
    return bytes([0x27, 0x01, sid]) + text + b'\0'


class StringVariables(unittest.TestCase):
    def test_a_string_goes_to_the_charsets_of_the_prints_that_show_it(self):
        code = (setstr(3, b'Sx') + setstr(4, b'Sy') + cs(4) + say(b'a\xff\x07\x03\x00') + cs(2) + say(b'b') + STOP)
        sets, _, _ = drawn([BOOT, glob(9, code)])
        self.assertIn('S', sets[4])
        self.assertIn('x', sets[4])
        self.assertNotIn('y', sets[4])      # string 4 is shown by nothing here
        self.assertNotIn('x', sets[2])

    def test_a_string_nothing_shows_stays_with_where_it_was_set(self):
        sets, _, _ = drawn([BOOT, glob(9, cs(5) + setstr(3, b'Q') + STOP)])
        self.assertIn('Q', sets[5])


class Bundle(unittest.TestCase):
    def setUp(self):
        groups = {
            (0, (S.WIO_GLOBAL << 16) | 9): {b'Hello': '안녕'.encode()},
            (0, (S.WIO_GLOBAL << 16) | 10): {b'Hello': '여보세요'.encode()},
            (5, S.WIO_ROOM << 16): {b'Key': '열쇠'.encode()},
        }
        self.dir = tempfile.mkdtemp()
        self.path = os.path.join(self.dir, 'korean.trs')
        with open(self.path, 'wb') as f:
            f.write(T.write_trs(groups))

    def test_translation_found_by_the_scripts_context(self):
        sets, _, _ = drawn([BOOT, glob(9, cs(4) + say(b'Hello') + STOP)], bundle=C.Bundle(self.path))
        self.assertEqual(sets[4], '녕안')
        self.assertNotIn('H', sets[4])

    def test_untranslated_text_is_drawn_as_it_is(self):
        sets, _, _ = drawn([BOOT, glob(9, cs(4) + say(b'Nope') + STOP)], bundle=C.Bundle(self.path))
        self.assertEqual(sets[4], 'Neop')

    def test_names_take_every_translation_of_the_original(self):
        sets, _, _ = drawn([BOOT, glob(9, objname(b'Hello') + STOP)], bundle=C.Bundle(self.path))
        self.assertEqual(sets[1], '녕보세안여요')

    def test_escapes_are_not_characters(self):
        self.assertEqual(C.text_chars(b'a\xff\x04\x01\x00b', False), {'a', 'b'})

    def test_legacy_text_is_read_like_the_engine_reads_it(self):
        self.assertEqual(C.text_chars('안'.encode('cp949') + b'!', False), {'안', '!'})
        # a lead byte takes the next byte even where the pair is no character: no stray ASCII
        self.assertEqual(C.text_chars(b'\xfa\x41', False), set())


class Validation(unittest.TestCase):
    def test_misses_are_the_characters_outside_the_charset(self):
        sets = {2: set('가나'), 4: set('다')}
        n, misses = C.validate(sets, [(2, '가나 ab'), (2, '가다'), (4, '다다'), (5, '가')])
        self.assertEqual(n, 4)
        self.assertEqual(misses, [(2, '가다', '다'), (5, '가', '가')])

    def test_ascii_and_replacement_chars_are_not_misses_unless_asked(self):
        n, misses = C.validate({2: set()}, [(2, 'abc�')])
        self.assertEqual(misses, [])
        n, misses = C.validate({2: set()}, [(2, 'abc')], ascii_too=True)
        self.assertEqual(misses, [(2, 'abc', 'abc')])

    def test_reads_json_and_engine_log(self):
        d = tempfile.mkdtemp()
        j = os.path.join(d, 'a.log.json')
        with open(j, 'w', encoding='utf-8') as f:
            json.dump([{'charset': 7, 'text': '가나', 'hex': ''}], f)
        lg = os.path.join(d, 'run.log')
        with open(lg, 'w', encoding='utf-8') as f:
            f.write("noise\nSCUMM text: loop 4, charset 2, F.SVF (1x1), decoration none, text '한\n두 줄'\n"
                    "SCUMM text: loop 5, charset 3, F.SVF, text 'it's'\nother\n")
        self.assertEqual(C.read_log(j), [(7, '가나')])
        self.assertEqual(C.read_log(lg), [(2, '한\n두 줄'), (3, "it's")])


HOT_MAP = """[fonts]
dlg=data:NGOTB21A.SVF
card=GBATB23A.SVF
card7=GBATB23A.SVF
dlgx=NGO21AX2.SVF
[fonts:clut8]
card=TBMY24.SVF
card7=TBMY24.SVF
[font]
missing=u+25a1
[font.0]
face=dlg,dlgx
[font.4]
face=card
[font.7]
face=card7
[glyphs]
0x5e = u+2026
"""


class HotLists(unittest.TestCase):
    sets = {0: set('가나'), 4: set('다'), 7: set('라다'), 9: set('마')}

    def test_one_list_per_face_file_the_union_of_the_charsets_that_use_it(self):
        lists = C.hot_lists(self.sets, HOT_MAP, ('U',))
        self.assertEqual(sorted(lists), ['GBATB23A', 'NGO21AX2', 'NGOTB21A'])
        ascii_ = set(range(0x20, 0x7F))
        extra = {0x2026, 0x25A1}                                # [glyphs] and missing=
        self.assertEqual(lists['NGOTB21A'][0], ascii_ | {ord(c) for c in '가나'} | extra)
        self.assertEqual(lists['GBATB23A'][0], ascii_ | {ord(c) for c in '다라'} | extra)    # cs4 and cs7 share the file
        self.assertEqual(lists['GBATB23A'][1], [('U', 4), ('U', 7)])
        self.assertNotIn(ord('마'), lists['GBATB23A'][0])           # charset 9 has no font id in the map

    def test_presets_and_face_names(self):
        both = C.hot_lists(self.sets, HOT_MAP, ('U', 'L'))
        self.assertEqual(sorted(both), ['GBATB23A', 'NGO21AX2', 'NGOTB21A', 'TBMY24'])
        self.assertEqual(sorted(C.hot_lists(self.sets, HOT_MAP, ('L',))), ['NGO21AX2', 'NGOTB21A', 'TBMY24'])  # dlg is not renamed
        only = C.hot_lists(self.sets, HOT_MAP, ('U', 'L'), only=['card'])
        self.assertEqual(sorted(only), ['GBATB23A', 'TBMY24'])

    def test_file_is_utf8_with_comments_and_every_character(self):
        d = tempfile.mkdtemp()
        mp = os.path.join(d, 'T.MAP')
        with open(mp, 'w') as f:
            f.write(HOT_MAP)
        done = C.write_hot(self.sets, mp, os.path.join(d, 'hot'), ('U',), ['card'], 'charsetchars.py g --map T.MAP')
        self.assertEqual(list(done), ['GBATB23A.HOT'])
        with open(os.path.join(d, 'hot', 'GBATB23A.HOT'), encoding='utf-8') as f:
            text = f.read()
        lines = text.split('\n')
        self.assertTrue(lines[0].startswith('# Preload list for GBATB23A'))
        self.assertIn('charsetchars.py g --map T.MAP', text)
        body = ''.join(l for l in lines if l and not l.startswith('#'))
        self.assertEqual(set(body), {chr(c) for c in range(0x21, 0x7F)} | set('다라') | {'\u2026', '\u25a1'})
        self.assertEqual(len(body), len(set(body)))            # each once
        self.assertEqual(done['GBATB23A.HOT'], len(body))

    def test_from_json_unions_earlier_runs(self):
        import subprocess
        d = tempfile.mkdtemp()
        mp = os.path.join(d, 'T.MAP')
        with open(mp, 'w') as f:
            f.write(HOT_MAP)
        for n, js in (('a.json', {'cs4': '다', 'unknown': ''}), ('b.json', {'cs4': '라', 'cs7': '마'})):
            with open(os.path.join(d, n), 'w', encoding='utf-8') as f:
                json.dump(js, f, ensure_ascii=False)
        subprocess.run([sys.executable, os.path.join(HERE, 'charsetchars.py'), '--from-json', os.path.join(d, 'a.json'),
                        os.path.join(d, 'b.json'), '--map', mp, '--preset', 'U', '--hot-faces', 'card', '--hot-dir',
                        os.path.join(d, 'hot')], check=True, capture_output=True)
        with open(os.path.join(d, 'hot', 'GBATB23A.HOT'), encoding='utf-8') as f:
            body = ''.join(l for l in f.read().split('\n') if l and not l.startswith('#'))
        self.assertTrue(set('다라마') <= set(body))      # cs4 of both runs, cs7 shares the file


class Output(unittest.TestCase):
    def test_json_has_one_key_per_charset_and_unknown(self):
        js = C.to_json({2: set('나가'), 4: set('a')}, set('z'))
        self.assertEqual(js, {'cs2': '가나', 'cs4': 'a', 'unknown': 'z'})
        self.assertEqual(C.sizes(js)['cs2'], (2, 2))


if __name__ == '__main__':
    unittest.main()
