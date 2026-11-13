"""patch_apk.py <apktool dir>: Java/manifest edits on an apktool-decoded Plants War 2 1.0.6 (idempotent).

- XIGNCODE anti-cheat off (its servers are gone): ProjectSetting._useXigncode = false, cookie = "offline"
- GameActivity.onCreate starts the embedded offline server (offline/src) before the native game runs
- targetSdkVersion 23 -> 24 (Android 15+ refuses to install apps targeting < 24)
"""
import os, re, sys

D = sys.argv[1]

def edit(rel, fn):
    p = os.path.join(D, rel)
    s = open(p, encoding="utf-8").read()
    t = fn(s)
    if t != s:
        open(p, "w", encoding="utf-8").write(t)
        print("patched", rel)

def once(old, new):
    def f(s):
        if new in s: return s
        assert s.count(old) == 1, old
        return s.replace(old, new)
    return f

edit("smali/com/ideabox/Library/ProjectSetting.smali", once(
    "    const/4 v0, 0x1\n\n    sput-boolean v0, Lcom/ideabox/Library/ProjectSetting;->_useXigncode:Z",
    "    const/4 v0, 0x0\n\n    sput-boolean v0, Lcom/ideabox/Library/ProjectSetting;->_useXigncode:Z"))

edit("smali/com/ideabox/Library/DefaultFunction.smali", once(
    "    invoke-static {}, Lcom/wellbia/xigncode/XigncodeClient;->getInstance()Lcom/wellbia/xigncode/XigncodeClient;\n\n"
    "    move-result-object v0\n\n"
    "    invoke-virtual {v0}, Lcom/wellbia/xigncode/XigncodeClient;->getCookie()Ljava/lang/String;\n\n"
    "    move-result-object v0\n",
    '    const-string v0, "offline"\n'))

HOOK = "    invoke-super {p0, p1}, Landroid/app/Activity;->onCreate(Landroid/os/Bundle;)V\n"
edit("smali/com/ftt/pw2/global/gl/GameActivity.smali", once(HOOK, HOOK +
    "\n    # PW2 offline: start the embedded game server on 127.0.0.1 before the native game runs\n"
    "    invoke-static {p0}, Lpw2/offline/OfflineServer;->start(Ljava/lang/Object;)V\n"))

def manifest(s):
    if "<uses-sdk" in s: return s
    i = s.index(">", s.index("<manifest")) + 1
    return s[:i] + '\n    <uses-sdk android:minSdkVersion="14" android:targetSdkVersion="24"/>' + s[i:]
edit("AndroidManifest.xml", manifest)
edit("apktool.yml", lambda s: re.sub(r"targetSdkVersion: '?23'?", "targetSdkVersion: 24", s))
