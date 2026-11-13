"""Re-encrypt game data tables with the additions that used to come from server-side updates."""
import sys
import os as _os; ROOT = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))
sys.path.insert(0, ROOT + '/server'); sys.path.insert(0, ROOT + '/patches')
import pw2crypto
from extra_strings import EXTRA
import restored_strings, glob, re, html
from xml.sax.saxutils import quoteattr
SRC = ROOT + '/apk/assets/Common/'
DST = ROOT + '/apk_smali/assets/Common/'

def patch(rel, fn):
    plain, ok = pw2crypto.decode(open(SRC + rel, 'rb').read()); assert ok, rel
    prefix, xml = plain[:4], plain[4:].decode('utf-8')
    xml2 = fn(xml); assert xml2 != xml, rel
    open(DST + rel, 'w').write(pw2crypto.encode(prefix + xml2.encode('utf-8')))
    print('patched', rel)

def original_tables():
    t = {}
    for p in sorted(glob.glob(SRC + 'String/*.x')):
        plain, ok = pw2crypto.decode(open(p, 'rb').read())
        for m in re.finditer(r'<string\s+([^>]*?)/?>', plain[4:].decode('utf-8', 'replace')):
            a = {k: html.unescape(v) for k, v in re.findall(r'(\w+)\s*=\s*"([^"]*)"', m.group(1))}
            if 'id' in a: t[a.pop('id')] = a
    return t

def strings(xml):
    LANGS = restored_strings.LANGS
    add = {k: {l: v for l in LANGS} for k, v in EXTRA.items()}
    tables = original_tables()
    tables.update(add)
    for k, cols in restored_strings.build(tables).items():
        add[k] = {l: cols.get(l) or cols.get('eng', '') for l in LANGS}
    print(f'  +{len(add)} strings')
    rows = ''.join(f'\t<string id="{k}" ' + ' '.join(f'{l}={quoteattr(c[l])}' for l in LANGS) + '/>\r\n'
                   for k, c in add.items())
    return xml.replace('</string_table>', rows + '</string_table>')

COMMON_EXTRA = {"max_stage_level": 5, "max_challenge": 40}
def common(xml):
    add = ''.join(f'\t<data name = "{k}" value_1 = "{v}"/>\r\n' for k, v in COMMON_EXTRA.items())
    i = xml.rindex('</')
    return xml[:i] + add + xml[i:]

patch('String/string_normal_3.x', strings)
patch('Data/common.x', common)

# "_global" data tables that the global build fetched from the update server
import shutil
def copy_as(src_rel, dst_rel):
    shutil.copyfile(SRC + src_rel, DST + dst_rel); print('created', dst_rel)
copy_as('Data/shop.x', 'Data/shop_global.x')
copy_as('Data/package.x', 'Data/package_global.x')

# the story intro (6 illustrated scenes) played before Gaia's welcome; only the server-pushed script had it
INTRO = '<script id="1" tutorial = "true" area = "0" stage = "0" mode = "select">'
def script(xml):
    assert xml.count(INTRO) == 1
    xml = xml.replace(INTRO, INTRO + '\r\n\t\t<play type = "intro" />')
    # the global client opens its shop as UI "shop_global": with "shop" the stage-6 minion-ticket
    # tutorial never finds its button and waits forever (softlock)
    assert xml.count('ui_name = "shop"') >= 1
    xml = xml.replace('ui_name = "shop"', 'ui_name = "shop_global"')
    # the global shop shows 3 cards instead of 4, so the 4th card (normal minion chest, which takes the
    # minion ticket) is off-screen and the list is locked during the tutorial: scroll it into view first
    old = 'ui_name = "shop_global" scroll_name="list" value = "3"'
    assert xml.count(old) == 1
    return xml.replace(old, 'ui_name = "shop_global" scroll_name="list" scroll_rate = "100" value = "3"')
patch('Data/script.x', script)
plain, ok = pw2crypto.decode(open(DST + 'Data/script.x', 'rb').read())
open(DST + 'Data/script_global.x', 'w').write(pw2crypto.encode(plain)); print('created Data/script_global.x')

LANGS = [(1, "language_korea"), (2, "language_english"), (3, "language_japan"),
         (4, "language_china_simple"), (5, "language_china"), (6, "language_thai")]
xml = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\r\n<language_list>\r\n' + \
      ''.join(f'\t<language id="{i}" name="{n}"/>\r\n' for i, n in LANGS) + '</language_list>\r\n'
open(DST + 'Data/language_table.x', 'w').write(pw2crypto.encode(b'\0\0\0\0' + xml.encode()))
print('created Data/language_table.x')
