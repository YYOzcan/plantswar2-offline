"""Re-encrypt game data tables with the additions that used to come from server-side updates."""
import sys
sys.path.insert(0, '/home/yigit/pw2/server'); sys.path.insert(0, '/home/yigit/pw2/patches')
import pw2crypto
from extra_strings import EXTRA
from xml.sax.saxutils import quoteattr
SRC = '/home/yigit/pw2/apk/assets/Common/'
DST = '/home/yigit/pw2/apk_smali/assets/Common/'

def patch(rel, fn):
    plain, ok = pw2crypto.decode(open(SRC + rel, 'rb').read()); assert ok, rel
    prefix, xml = plain[:4], plain[4:].decode('utf-8')
    xml2 = fn(xml); assert xml2 != xml, rel
    open(DST + rel, 'w').write(pw2crypto.encode(prefix + xml2.encode('utf-8')))
    print('patched', rel)

def strings(xml):
    add = ''.join(f'\t<string id="{k}" ' + ' '.join(f'{l}={quoteattr(v)}' for l in ('kor','eng','chn_sc','chn_tw','jpn','tha')) + '/>\r\n'
                  for k, v in EXTRA.items())
    return xml.replace('</string_table>', add + '</string_table>')

COMMON_EXTRA = {"max_stage_level": 5, "max_challenge": 40}
def common(xml):
    add = ''.join(f'\t<data name = "{k}" value_1 = "{v}"/>\r\n' for k, v in COMMON_EXTRA.items())
    i = xml.rindex('</')
    return xml[:i] + add + xml[i:]

patch('String/string_normal_3.x', strings)
patch('Data/common.x', common)
