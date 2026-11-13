"""replace_dex.py <apk> <classes.dex>: swap classes.dex inside an (unsigned) APK, keeping every other entry as-is."""
import sys, zipfile, shutil, os
apk, dex = sys.argv[1], sys.argv[2]
tmp = apk + ".new"
with zipfile.ZipFile(apk) as zi, zipfile.ZipFile(tmp, "w") as zo:
    for it in zi.infolist():
        data = open(dex, "rb").read() if it.filename == "classes.dex" else zi.read(it.filename)
        zo.writestr(it, data, compress_type=it.compress_type)
os.replace(tmp, apk)
