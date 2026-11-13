"""Persistent player state for the offline PW2 server (JSON file)."""
import json, os, threading, time
PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "save", "save.json")
_lock = threading.RLock()
_db = None

def db():
    global _db
    with _lock:
        if _db is None:
            if os.path.exists(PATH):
                _db = json.load(open(PATH, encoding="utf-8"))
            else:
                _db = {"next_idx": 1001, "users": {}}
        return _db

def save():
    with _lock:
        os.makedirs(os.path.dirname(PATH), exist_ok=True)
        tmp = PATH + ".tmp"
        json.dump(db(), open(tmp, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        os.replace(tmp, PATH)

def user(idx):
    return db()["users"].get(str(idx))

def new_user(nick, sns_type, sns_id):
    with _lock:
        d = db()
        idx = d["next_idx"]; d["next_idx"] += 1
        u = {"idx_id": idx, "idx_time": int(time.time()), "nick": nick, "sns_type": sns_type,
             "sns_id": sns_id, "skey": "offline%d" % idx, "created": int(time.time())}
        d["users"][str(idx)] = u
        save()
        return u
