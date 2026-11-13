# Plants War 2 — Offline Restoration

*Plants War 2* (Four33, `com.ftt.pw2.global.gl` 1.0.6) was an online-only Android game whose servers shut
down years ago. This project brings it back to life **fully offline**: the game server was reverse-engineered
from the client and re-implemented, and it now runs **inside the APK itself**. On launch the game talks to its
own `127.0.0.1`, so it works without internet on phones, tablets and the Android emulator.

> This repository contains no game files. You need your own copy of the original APK
> (`Plants War 2 1.0.6`, `com.ftt.pw2.global.gl`). Not affiliated with Four33.

## What works
- Guest account, tutorial, hero selection (all three classes), Story mode, gear, minions, skills, stats,
  skins, quests, achievements, daily rewards, Lucky Chests, shop (with in-game currencies)
- League / 3 vs 3 / Unlimited Battle / Time Attack against the client's own bots
- Gold, Leaf, Keys and Honor Points are kept at **999,999** (refilled on every login), since purchases are gone
- Progress is saved on the device; backup/restore over adb (see below)

Not available: real-money purchases, friends, Facebook / Google Play features.

## Quick start
Requirements: Linux, Python 3, a JDK (21 tested), [apktool](https://apktool.org) 3.x, Android SDK
build-tools (d8, zipalign, apksigner), LLVM (`llvm-mc`, `ld.lld`, `llvm-objcopy`).
Tool locations are read from `scripts/env.sh` (defaults: `tools/`, `android-sdk/`; override with
`JDK=… APKTOOL=… ANDROID_SDK=…`).

```sh
scripts/setup.sh /path/to/Plants_War_2_1.0.6.apk   # unpack your APK, patch data, create a signing key
scripts/build.sh                                    # -> build/pw2.apk
```

### Install on a phone / tablet
Copy `build/pw2.apk` to the device and install it (allow "unknown sources"; Play Protect may ask to confirm).
That's it — no internet or companion app needed.

Requires a device that can run **32-bit ARM** apps (the game engine is 32-bit, no source code exists).
Android 4.0 – 16 are supported (tested so far on an Android 11 emulator); 64-bit-only devices (e.g. Pixel 7
and newer) cannot run it.

### Play on PC (Android emulator)
Create an Android 11 x86_64 `google_apis` AVD named `pw2` (it translates ARM code), e.g.
```sh
sdkmanager "system-images;android-30;google_apis;x86_64" emulator platform-tools "build-tools;34.0.0"
avdmanager create avd -n pw2 -k "system-images;android-30;google_apis;x86_64"
```
then run `./play.sh` (starts the emulator, installs/updates `build/pw2.apk`, launches the game).
Use `PW2_GPU=swiftshader_indirect ./play.sh` if host GPU rendering misbehaves.

### Saves
The save lives in the app's private storage. With the game running and the device connected over adb:
```sh
scripts/save.sh export                 # -> saves/pw2-save-<date>.json
scripts/save.sh import <save.json>     # then restart the game
```
To move a save to a new device, start the game there once and create the guest account first
(the first account is always #1001), then import.

## How it works
| Part | What it does |
|---|---|
| `offline/src/pw2/offline/` | The server embedded in the APK (plain Java 8, no Android APIs): HTTP on ports 10005/10015/10030/10080/12380, the game's transport cipher, ~110 `act` handlers, JSON save |
| `server/` | The same server in Python (reference / development): `pw2server.py`, `pw2crypto.py`, `handlers.py` |
| `offline/test/replay.py` | Differential test: replays recorded client traffic through both servers and compares every response and the resulting save |
| `scripts/patch_apk.py` | Java side: XIGNCODE anti-cheat off, `GameActivity.onCreate` starts `OfflineServer`, targetSdk 24 |
| `scripts/patch_lib.py` | `libGame.so`: server URLs → `127.0.0.1`; GLES1 → `libpw2gl.so`; `StringManager::GetString` patch (`patches/getstring_miss.s`) |
| `glshim/` | `libpw2gl.so`, a GLES1 interposer that tracks the matrix stacks itself — emulated GLES1 returns garbage for `glGetFloatv(GL_MODELVIEW/PROJECTION_MATRIX)`, which made the game cull the whole map |
| `scripts/patch_data.py` | Data the update server used to push: `common` table keys, `*_global` tables, `language_table`, missing texts |
| `patches/restored_strings.py`, `patches/extra_strings.py` | ~335 restored texts (aliases, derived names, and new descriptions) |
| `scripts/build.sh` | Patches + compiles the server, merges it into `classes.dex` with d8, signs → `build/pw2.apk` |
| `scripts/save.sh` | Save export/import via `adb forward` to the game's loopback-only `/pw2/admin/save` endpoint |

### Protocol in short
`GetServiceInfo.aspx` (JSON) points the client to the game server; `update.php`, `login.php` and
`action.php` take `PD=<base64>` bodies encrypted with a custom cipher (8-byte random key, nibble-swap/XOR
states, rolling step; also used for the `.x` data files). One request batches several *acts*
(`act=22&...&&act=89&&&idx_id=...`); the answer is `<result><msg>1</msg><act v="N">…</act>…</result>`.
The client computes game logic and reports deltas; the server stores state and assigns ids.

### Missing texts
The update server also pushed string tables, which are lost. Texts that are still missing no longer show
"Can not find text id": the patched `GetString` shows a readable version of the id (`superman_title` →
"Superman title") and logs it:
```sh
adb logcat -d -s PW2Text      # add real texts to patches/restored_strings.py, rebuild
```

### Development
- `PW2_HOST=10.0.2.2 scripts/build.sh` builds an APK that uses the Python server on the host (`server/pw2server.py`).
- `adb shell setprop debug.pw2 1` (restart the game) logs every request of the embedded server: `adb logcat -s PW2Server`.
- Reverse-engineering helpers (Ghidra headless scripts, PIC string resolver, act table extraction) are in `scripts/`.
