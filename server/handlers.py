"""Per-act response builders for the Plants War 2 game server (action.php / login.php).

Every request carries one or more "acts" (see pw2server.parse_actions). Each act_N function
receives the act's params `a`, the request-global params `g` (idx_id, type=hero, skey...) and
returns the inner XML of <act v="N">...</act>.  Schemas were reversed from
ServerManager::NetworkMethod and the various *::Load(XMLParser*) functions in libGame.so.

The client is the source of truth for game rules: it computes rewards / costs locally and
reports every change (AddGold, UseItem, AddUnit, StageResult...).  The server stores the
resulting state and replays it on the next login, assigning ids for new units/parts.
"""
import time, datetime, random
import state

def now(): return int(time.time())

def secs_to_midnight():
    t = datetime.datetime.combine(datetime.date.today() + datetime.timedelta(days=1), datetime.time())
    return max(60, int(t.timestamp()) - now())

def esc(s):
    return (str(s).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            .replace('"', "&quot;"))

def attrs(d, skip=()):
    return " ".join(f'{k}="{esc(v)}"' for k, v in d.items() if k not in skip)

# ---------------------------------------------------------------- player model
RES_FIELDS = {  # server item type -> LoadResource element (act 89)
    11: "iap", 13: "gold", 14: "key", 15: "pcoin",
    21: "hp_p", 22: "mp_p", 23: "cool_p", 24: "return_s", 25: "rebirth_s",
    28: "change_op", 31: "tonic_2", 32: "tonic_3", 33: "tonic_4",
    36: "enchant_2", 37: "enchant_3", 38: "enchant_4",
    51: "unit_1_t", 52: "unit_2_t", 61: "parts_1_t", 62: "parts_2_t", 63: "armor_1_t", 64: "armor_2_t",
}
CURRENCIES, CURRENCY_FULL = ("gold", "iap", "key", "pcoin"), 9000000  # purchases are gone: keep these topped up
START_RES = {"gold": CURRENCY_FULL, "iap": CURRENCY_FULL, "key": CURRENCY_FULL, "pcoin": CURRENCY_FULL, "hp_p": 3, "mp_p": 3, "cool_p": 1,
             "return_s": 1, "rebirth_s": 1}
STAMINA_MAX, STAMINA_REGEN = 30, 300     # action points, seconds per point
FLAG_ACT8 = {1: "tutorial", 2: "pvp", 3: "arena", 4: "challenge", 5: "superman", 10: "review"}

def U(g):
    """Current user for this request (creates the rich part of the record lazily)."""
    u = state.user(g.get("idx_id", "0"))
    if u is None:
        return None
    u.setdefault("res", dict(START_RES))
    u.setdefault("flags", {"tutorial": 1, "pvp": 0, "arena": 0, "challenge": 0, "superman": 0, "review": 1})
    u.setdefault("heroes", {})
    u.setdefault("hero_type", 1)
    u.setdefault("units", {})          # id -> unit attrs
    u.setdefault("mount_units", {})    # slot -> unit id
    u.setdefault("home", {"hp": 0, "heal_hp": 0})
    u.setdefault("tower", {"hp": 0, "attack": 0})
    u.setdefault("parts", {})          # hero_type -> id -> attrs
    u.setdefault("skins", {})          # hero_type -> skin_id -> attrs
    u.setdefault("unit_book", [])
    u.setdefault("parts_book", [])
    u.setdefault("scripts", [])
    u.setdefault("stamina", {"cnt": STAMINA_MAX, "t": now()})
    u.setdefault("clear_ticket", {"cnt": 0})
    u.setdefault("quests", {})
    u.setdefault("achieve", [])
    u.setdefault("inven", {"unit": 0, "parts": 0})
    u.setdefault("boxes", {})
    u.setdefault("next_id", 100001)
    return u

def new_id(u):
    i = u["next_id"]; u["next_id"] += 1
    return i

def cur_hero_type(u, g):
    t = int(g.get("type") or 0)
    return t if t in (1, 2, 3) else int(u.get("hero_type", 1))

# starter kit per hero type: (weapon, hat, hand) part indexes from parts_<hero>.xml, first skill
STARTER = {1: ((30000, 32000, 31000), "stonefall"),
           2: ((40000, 42000, 41000), "icespike"),
           3: ((50000, 52000, 51000), "poisonshot")}   # first skill of each class in hero_skill.xml

def _starter_skill(t):
    return STARTER[t][1]

def hero(u, t):
    t = str(t)
    if t not in u["heroes"]:
        u["heroes"][t] = {
            "info": {"league": 1, "ldp": 0, "l_rank": 0, "area": 1, "stage": 0},
            "status": {"hp": 0, "mp": 0, "attack": 0, "armor": 0, "speed": 0, "a_speed": 0},
            "skill": {}, "skill_names": {}, "parts": {}, "skin": {"skin_id": 0, "level": 0},
            "stages": {},   # "area-stage" -> star
            "power": 0,
        }
    h = u["heroes"][t]
    if not h.get("starter_given"):               # one-time starter kit (also migrates old saves)
        h["starter_given"] = 1
        idxs, _ = STARTER[int(t)]
        inv = u["parts"].setdefault(t, {})
        for slot, idx in zip((1, 2, 3), idxs):     # weapon, hat, hand
            if str(slot) not in h["parts"]:
                pid = new_id(u)
                inv[str(pid)] = {"parts_id": pid, "name": idx, "level": 1, "exp": 0}
                h["parts"][str(slot)] = str(pid)
        h["skill"].setdefault("11", 1)
        h["skill_names"].setdefault("1", _starter_skill(int(t)))
    return h

def stamina(u):
    s = u["stamina"]
    if s["cnt"] < STAMINA_MAX:
        gained = (now() - s["t"]) // STAMINA_REGEN
        if gained > 0:
            s["cnt"] = min(STAMINA_MAX, s["cnt"] + gained)
            s["t"] += gained * STAMINA_REGEN
    else:
        s["t"] = now()
    r_t = 0 if s["cnt"] >= STAMINA_MAX else STAMINA_REGEN - (now() - s["t"])
    return s["cnt"], r_t

# ---------------------------------------------------------------- time / misc
def act_2(a, g):   # LoadServerTime
    return f'<date time="{now()}"/>'

def act_3(a, g):   # LoadServerDay
    d = datetime.datetime.now()
    return f'<date date="{d.year*10000 + d.month*100 + d.day}" week="{d.isoweekday() % 7}"/>'

def act_4(a, g):   # CheckNew (new gifts?)
    return '<user gift="0"/>'

def act_5(a, g):   return ""   # SendSMS (push alarm setting)
def act_7(a, g):   return ""   # CheckXignCode
def act_16(a, g):  return ""   # LetencyCheck
def act_19(a, g):  return ""   # SendLoginLog

def act_8(a, g):   # DisableTutorial / Enable*Mode / DisableReview
    u = U(g)
    f = FLAG_ACT8.get(int(a.get("chk", 0)))
    if u and f:
        u["flags"][f] = 0 if f in ("tutorial", "review") else 1
        state.save()
    return ""

def act_9(a, g):   return ""   # ChangeHeroData (checksum only)
def act_10(a, g):  return ""   # CheckHeroData

# ---------------------------------------------------------------- account
def act_20(a, g):  # Join (guest account)
    u = state.new_user(a.get("nick", "Player"), a.get("sns_type", "0"), a.get("id", ""))
    return f'<idx_id>{u["idx_id"]}</idx_id><idx_time>{u["idx_time"]}</idx_time>'

def act_24(a, g):  # Recovery (re-login with sns id) -> return existing account if any
    for u in state.db()["users"].values():
        if a.get("id") and u.get("sns_id") == a.get("id"):
            return f'<idx_id>{u["idx_id"]}</idx_id><idx_time>{u["idx_time"]}</idx_time>'
    return ""

def act_21(a, g):  # CreateHero -> client builds the hero locally
    u = U(g)
    if u:
        t = cur_hero_type(u, g); hero(u, t); u["hero_type"] = t; state.save()
    return ""

def act_6(a, g):   # SelectHero
    u = U(g)
    if u:
        t = int(a.get("hero_type", 1)); hero(u, t); u["hero_type"] = t; state.save()
    return ""

def act_22(a, g):  # Login
    u = U(dict(g, idx_id=a.get("idx_id", g.get("idx_id"))))
    if u is None:
        return '<block chk="3"/>'
    fl = u["flags"]
    x = [f'<skey>{esc(u["skey"])}</skey>', f'<nick>{esc(u["nick"])}</nick>', "<sns_list></sns_list>"]
    for k in ("tutorial", "pvp", "arena", "challenge", "superman"):
        x.append(f"<{k}>{fl.get(k, 0)}</{k}>")
    x += [f'<superman_chk>1</superman_chk>', '<crm_chk>0</crm_chk>', f'<review>{fl.get("review", 0)}</review>',
          f'<last_hero>{u["hero_type"] if u["heroes"] else 0}</last_hero>', '<alarm>3</alarm>',
          '<pvp_result>0</pvp_result>', '<win_restore>0</win_restore>']
    for c in CURRENCIES:                        # refill gold / leaf / key / crown on every login
        if int(u["res"].get(c, 0)) < CURRENCY_FULL: u["res"][c] = CURRENCY_FULL
    u["last_login"] = now(); state.save()
    return "".join(x)

def act_23(a, g):  return ""   # RegisterSNS
def act_29(a, g):  return '<change code="OFFLINE" l_t="600"/>'   # GetDeviceLinkCode
def act_30(a, g):  return ""   # CheckDeviceLinkCode
def act_31(a, g):  return ""   # Disconnect / Unregister SNS

# ---------------------------------------------------------------- resources
def res_xml(u):
    r = u["res"]
    return "".join(f"<{f}>{int(r.get(f, 0))}</{f}>" for f in RES_FIELDS.values())

def act_89(a, g):  # LoadResource
    u = U(g)
    return res_xml(u) if u else ""

def act_15(a, g):  # LoadResourceMax: caps shown in the top bar (26 = action point / stamina)
    return f'<item_list><item type="26" max="{STAMINA_MAX}"/></item_list>'

def change_res(u, typ, chk, cnt):
    typ, cnt = int(typ), int(cnt)
    if typ == 26:
        c, _ = stamina(u)
        u["stamina"]["cnt"] = max(0, c + cnt if int(chk) else c - cnt)
        return u["stamina"]["cnt"]
    f = RES_FIELDS.get(typ, f"item_{typ}")
    v = int(u["res"].get(f, 0)) + (cnt if int(chk) else -cnt)
    u["res"][f] = max(0, v)
    return u["res"][f]

def act_87(a, g):  # Add/Use money
    u = U(g)
    if not u: return ""
    v = change_res(u, a.get("money_type", 13), a.get("money_chk", 0), a.get("money_cnt", 0)); state.save()
    return f'<money type="{a.get("money_type")}" count="{v}"/>'

def act_88(a, g):  # Add/Use item / ticket / enchant
    u = U(g)
    if not u: return ""
    v = change_res(u, a.get("item_type", 0), a.get("item_chk", 0), a.get("item_cnt", 0)); state.save()
    return f'<item type="{a.get("item_type")}" count="{v}"/>'

def act_59(a, g):  # LoadStamina
    u = U(g)
    if not u: return ""
    c, r_t = stamina(u)
    return f'<stamina cnt="{c}" r_t="{r_t}" max_cnt="{STAMINA_MAX}" re_t="{STAMINA_REGEN}"/>'

def act_58(a, g):  # Use/Add action point
    u = U(g)
    if not u: return ""
    c, _ = stamina(u)
    n = int(a.get("cnt", 0))
    c = c + n if int(a.get("chk", 0)) else max(0, c - n)
    if u["stamina"]["cnt"] >= STAMINA_MAX and c < STAMINA_MAX:
        u["stamina"]["t"] = now()
    u["stamina"]["cnt"] = c
    _, r_t = stamina(u); state.save()
    return f'<stamina cnt="{c}" r_t="{r_t}"/>'

def act_133(a, g):  # LoadClearTicket
    u = U(g)
    c = u["clear_ticket"]["cnt"] if u else 0
    return f'<clear_t r_t="{secs_to_midnight()}" cnt="{c}" max_cnt="5" add_cnt="0" bpc_cnt="0" add_max_cnt="0" max_r_t="86400"/>'

def act_134(a, g):  # Use/Add clear ticket
    u = U(g)
    if u:
        n = int(a.get("cnt", 0))
        ct = u["clear_ticket"]
        ct["cnt"] = ct["cnt"] + n if int(a.get("chk", 0)) else max(0, ct["cnt"] - n); state.save()
    return ""

def act_135(a, g): return ""   # BuyClearTicketMax

def act_138(a, g):  # AddInventory (extend inventory)
    u = U(g)
    if not u: return ""
    kind = "unit" if str(a.get("sort")) == "1" else "parts"
    u["inven"][kind] = u["inven"].get(kind, 0) + 5; state.save()
    return f'<inven sort="{a.get("sort")}" add_cnt="{u["inven"][kind]}"/>'

# ---------------------------------------------------------------- hero
import os as _os
def _strip():
    try: return set(open(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "strip.txt")).read().split())
    except Exception: return set()

def hero_xml(u, t):
    h = hero(u, t)
    st = _strip()
    x = [f'<info {attrs(h["info"])}/>', f'<status {attrs(h["status"])}/>']
    if "skill" not in st: x.append(f'<skill {attrs(h["skill"])}/>')
    if "skillname" not in st:
        for slot, name in sorted(h["skill_names"].items()):
            x.append(f'<skill_{slot} name="{esc(name)}"/>')
    parts = u["parts"].get(str(t), {})
    if "parts" not in st:
        for slot, pid in sorted(h["parts"].items()):
            p = parts.get(str(pid))
            if p:
                x.append(f'<parts_{slot} {attrs(p)}/>')
    if "skin" not in st: x.append(f'<skin {attrs(h["skin"])}/>')
    return "".join(x)

def act_85(a, g):  # LoadHero (f_idx_id, f_type 0=all)
    u = U(dict(g, idx_id=a.get("f_idx_id") or g.get("idx_id")))
    if not u: return ""
    ft = int(a.get("f_type") or 0)
    types = [ft] if ft else [1, 2, 3]          # every account owns all three heroes
    body = "".join(f'<hero type="{t}">{hero_xml(u, t)}</hero>' for t in types)
    state.save()
    return f'<user idx_id="{u["idx_id"]}" nick="{esc(u["nick"])}">{body}</user>'

def act_82(a, g):  # UpgradeStatus
    u = U(g)
    if not u: return ""
    h = hero(u, cur_hero_type(u, g))
    for k in h["status"]:
        if k in a: h["status"][k] = int(a[k])
    state.save()
    return f'<status {attrs(h["status"])}/>'

def act_83(a, g):  # UpgradeSkill: skill = packed number from SkillToNumber
    u = U(g)
    if not u: return ""
    h = hero(u, cur_hero_type(u, g))
    num = int(a.get("skill", 0))
    key = str(num)
    lv = int(h["skill"].get(key, 0)) + 1
    h["skill"][key] = lv; state.save()
    return f'<hero skill="{num}" lv="{lv}"/>'

def act_90(a, g):  # MountParts / MountSkill / MountSkin (one act, different params)
    u = U(g)
    if not u: return ""
    t = cur_hero_type(u, g); h = hero(u, t)
    for k, v in a.items():
        if k.startswith("parts_"):
            slot = k[6:]
            if str(v) in ("0", ""): h["parts"].pop(slot, None)
            else: h["parts"][slot] = str(v)
        elif k.startswith("skill_"):
            h["skill_names"][k[6:]] = v
        elif k == "skin_id":
            h["skin"]["skin_id"] = int(v)
            sk = u["skins"].get(str(t), {}).get(str(v))
            h["skin"]["level"] = int(sk.get("level", 0)) if sk else 0
        elif k == "power":
            h["power"] = int(v)
    state.save()
    return ""

def act_91(a, g):  return ""   # Send parts/unit power (ranking)

# ---------------------------------------------------------------- buildings / units
def act_81(a, g):  # UpgradeBuilding
    u = U(g)
    if not u: return ""
    for k, (sec, f) in {"home_hp": ("home", "hp"), "home_heal_hp": ("home", "heal_hp"),
                        "tower_hp": ("tower", "hp"), "tower_attack": ("tower", "attack")}.items():
        if k in a: u[sec][f] = int(a[k])
    state.save()
    return f'<home {attrs(u["home"])}/><tower {attrs(u["tower"])}/>'

UNIT_KEYS = ("unit_id", "name", "max_grade", "grade", "level", "exp")

def act_94(a, g):  # LoadUnitInventory
    u = U(g)
    if not u: return ""
    body = "".join(f'<unit {attrs(un)}/>' for un in u["units"].values())
    n = u["inven"].get("unit", 0)
    return f'<unit_list add_cnt="{n}" bpc_add_cnt="0" max_add_cnt="100">{body}</unit_list>'

def act_98(a, g):  # AddUnit -> assign id
    u = U(g)
    if not u: return ""
    i = new_id(u)
    un = {"unit_id": i}
    for k in UNIT_KEYS[1:]:
        un[k] = a.get(k, 0)
    u["units"][str(i)] = un
    if un["name"] not in u["unit_book"]: u["unit_book"].append(un["name"])
    state.save()
    return f'<unit idx="{a.get("idx", 0)}" id="{i}"/>'

def act_99(a, g):  # ModifyUnit
    u = U(g)
    if u and str(a.get("unit_id")) in u["units"]:
        un = u["units"][str(a["unit_id"])]
        for k in UNIT_KEYS[1:]:
            if k in a: un[k] = a[k]
        state.save()
    return ""

def act_100(a, g):  # RemoveUnit
    u = U(g)
    if u:
        uid = str(a.get("unit_id"))
        u["units"].pop(uid, None)
        for s, v in list(u["mount_units"].items()):
            if str(v) == uid: u["mount_units"].pop(s)
        state.save()
    return ""

def act_84(a, g):  # MountUnit unit_N=id
    u = U(g)
    if u:
        for k, v in a.items():
            if k.startswith("unit_"):
                if str(v) in ("0", ""): u["mount_units"].pop(k[5:], None)
                else: u["mount_units"][k[5:]] = str(v)
        state.save()
    return ""

def act_86(a, g):  # LoadMountUnit (f_idx_id)
    u = U(dict(g, idx_id=a.get("f_idx_id") or g.get("idx_id")))
    if not u: return ""
    units = []
    for slot, uid in sorted(u["mount_units"].items()):
        un = u["units"].get(str(uid))
        if un:
            units.append(f'<unit_{slot} {attrs(un)}/>')
    return (f'<user idx_id="{u["idx_id"]}"><home {attrs(u["home"])}/><tower {attrs(u["tower"])}/>'
            f'<unit_list>{"".join(units)}</unit_list></user>')

def act_74(a, g):  # LoadUnitMemory (unit encyclopedia)
    u = U(g)
    body = "".join(f'<book name="{esc(n)}"/>' for n in (u["unit_book"] if u else []))
    return f"<book_list>{body}</book_list>"

def act_92(a, g):  # LoadPartsMemory
    u = U(g)
    body = "".join(f'<p_book name="{esc(n)}"/>' for n in (u["parts_book"] if u else []))
    return f"<p_book_list>{body}</p_book_list>"

# ---------------------------------------------------------------- parts / skins
def parts_of(u, g):
    return u["parts"].setdefault(str(cur_hero_type(u, g)), {})

def act_93(a, g):  # LoadPartsInventory (current hero)
    u = U(g)
    if not u: return ""
    body = "".join(f'<parts {attrs(p)}/>' for p in parts_of(u, g).values())
    n = u["inven"].get("parts", 0)
    return f'<parts_list add_cnt="{n}" bpc_add_cnt="0" max_add_cnt="100">{body}</parts_list>'

def act_95(a, g):  # AddParts -> assign id, keep every attribute the client sent (att_N_name...)
    u = U(g)
    if not u: return ""
    i = new_id(u)
    p = {"parts_id": i}
    for k, v in a.items():
        if k not in ("act", "idx", "link"): p[k] = v
    parts_of(u, g)[str(i)] = p
    if p.get("name") and p["name"] not in u["parts_book"]: u["parts_book"].append(p["name"])
    state.save()
    return f'<parts idx="{a.get("idx", 0)}" id="{i}"/>'

def act_96(a, g):  # ModifyParts
    u = U(g)
    if u:
        p = parts_of(u, g).get(str(a.get("parts_id")))
        if p:
            for k, v in a.items():
                if k not in ("act", "link", "parts_id"): p[k] = v
            state.save()
    return ""

def act_97(a, g):  # RemoveParts
    u = U(g)
    if u:
        pid = str(a.get("parts_id"))
        parts_of(u, g).pop(pid, None)
        h = hero(u, cur_hero_type(u, g))
        for s, v in list(h["parts"].items()):
            if str(v) == pid: h["parts"].pop(s)
        state.save()
    return ""

def act_130(a, g):  # LoadSkinInventory
    u = U(g)
    if not u: return ""
    sk = u["skins"].get(str(cur_hero_type(u, g)), {})
    return "<skin_list>" + "".join(f'<skin {attrs(s)}/>' for s in sk.values()) + "</skin_list>"

def act_131(a, g):  # AddSkin
    u = U(g)
    if u:
        sk = u["skins"].setdefault(str(cur_hero_type(u, g)), {})
        sk[str(a.get("skin_id"))] = {"skin_id": a.get("skin_id"), "level": a.get("level", 1), "e_cnt": 0}
        state.save()
    return ""

def act_132(a, g):  # UpgradeSkin
    u = U(g)
    if u:
        sk = u["skins"].setdefault(str(cur_hero_type(u, g)), {})
        s = sk.setdefault(str(a.get("skin_id")), {"skin_id": a.get("skin_id")})
        s["level"] = a.get("level", s.get("level", 1)); s["e_cnt"] = a.get("e_cnt", 0)
        h = hero(u, cur_hero_type(u, g))
        if str(h["skin"]["skin_id"]) == str(a.get("skin_id")): h["skin"]["level"] = int(s["level"])
        state.save()
    return ""

# ---------------------------------------------------------------- stages
def act_61(a, g):  # StageEntrance -> log key
    return f'<stage log_idx="{now()}" log_key="offline"/>'

def act_60(a, g):  # StageResult
    u = U(g)
    if u and int(a.get("result", 0)) in (2, 4):     # GAME_RESULT: 2 = cleared, 4 = sweep (clear ticket), 3 = quit
        h = hero(u, cur_hero_type(u, g))
        area, st, star = int(a.get("area", 1)), int(a.get("stage", 1)), int(a.get("star", 0))
        key = f"{area}-{st}"
        h["stages"][key] = max(int(h["stages"].get(key, 0)), star)
        inf = h["info"]
        if (area, st) > (int(inf["area"]), int(inf["stage"])):
            inf["area"], inf["stage"] = area, st
        state.save()
    return ""

def act_62(a, g):  # LoadStageInfo(area)
    u = U(g)
    area = int(a.get("area", 1))
    body = ""
    rw = {}
    if u:
        h = hero(u, cur_hero_type(u, g))
        rw = h.setdefault("star_rwd", {}).get(str(area), {})
        for key, star in sorted(h["stages"].items()):
            ar, st = map(int, key.split("-"))
            if ar == area:
                body += f'<stage stage="{st}" star="{star}"/>'
    r = " ".join(f'rwd_{i}="{rw.get(str(i), 0)}"' for i in range(1, 5))
    return f'<stage_list area="{area}" {r}>{body}</stage_list>'

def act_57(a, g):  # StageStarReward(area, rwd_type)
    u = U(g)
    if u:
        h = hero(u, cur_hero_type(u, g))
        h.setdefault("star_rwd", {}).setdefault(str(a.get("area", 1)), {})[str(a.get("rwd_type", 1))] = 1
        state.save()
    return ""

# ---------------------------------------------------------------- scripts / quests / achievements
def act_70(a, g):  # AddScript (tutorial/story script completed)
    u = U(g)
    if u and a.get("script_id") and a["script_id"] not in u["scripts"]:
        u["scripts"].append(a["script_id"]); state.save()
    return ""

def act_71(a, g):  # LoadScriptList
    u = U(g)
    return "<script>" + "".join(f"<s>{esc(s)}</s>" for s in (u["scripts"] if u else [])) + "</script>"

def act_76(a, g):  # CompleteQuest(type, id)
    u = U(g)
    if u:
        q = u["quests"].setdefault(f'{a.get("type")}-{a.get("id")}', {"type": a.get("type"), "id": a.get("id"), "cnt": 0, "chk": 0})
        q["chk"] = 1; q["day"] = datetime.date.today().isoformat(); state.save()
    return ""

def act_77(a, g):  # LoadQuest
    u = U(g)
    today = datetime.date.today().isoformat()
    body = ""
    if u:
        for q in u["quests"].values():
            if q.get("type") in ("1", 1) and q.get("day") != today:   # daily quests reset
                continue
            body += f'<quest type="{q["type"]}" id="{q["id"]}" cnt="{q.get("cnt", 0)}" chk="{q.get("chk", 0)}"/>'
    return f"<quest_list>{body}</quest_list>"

def act_75(a, g):  return act_77(a, g)

def act_79(a, g):  # CompleteAchievement (ids come through AchievementManager::SaveParam)
    u = U(g)
    if u and a.get("id") and a["id"] not in u["achieve"]:
        u["achieve"].append(a["id"]); state.save()
    return ""

def act_78(a, g):  # achievement progress: type=<counter name>&chg=<delta>
    u = U(g)
    if u and a.get("type"):
        ar = u.setdefault("a_r", {})
        ar[a["type"]] = int(ar.get(a["type"], 0)) + int(a.get("chg", 0) or 0); state.save()
    return ""

def act_80(a, g):  # LoadAchievement
    u = U(g)
    body = "".join(f'<a_c id="{esc(i)}"/>' for i in (u["achieve"] if u else []))
    ar = attrs(u.get("a_r", {})) if u else ""
    return f"<a_r {ar}/><a_c_list>{body}</a_c_list>"

# ---------------------------------------------------------------- daily / boxes / events / gifts
def act_120(a, g):  # LoadTodayReward
    u = U(g)
    if not u: return ""
    tr = u.setdefault("today", {"cnt": 0, "day": ""})
    chk = 1 if tr["day"] == datetime.date.today().isoformat() else 0
    tomorrow = datetime.datetime.combine(datetime.date.today() + datetime.timedelta(days=1), datetime.time())
    return f'<tdr cnt="{tr["cnt"]}" chk="{chk}" r_t="{int(tomorrow.timestamp()) - now()}"/>'

def act_72(a, g):  # ReceiveTodayReward
    u = U(g)
    if u:
        tr = u.setdefault("today", {"cnt": 0, "day": ""})
        today = datetime.date.today().isoformat()
        if tr["day"] != today:
            tr["day"] = today; tr["cnt"] = int(a.get("cnt", tr["cnt"] + 1))
        state.save()
    return ""

BOXES = {"luckybox": 4 * 3600, "s_partsbox": 24 * 3600, "s_armorbox": 24 * 3600, "s_unitbox": 24 * 3600}
BOX_TYPE = {"1": "luckybox", "2": "s_partsbox", "3": "s_armorbox", "4": "s_unitbox"}

def boxes_xml(u):
    out = ""
    for name, cd in BOXES.items():
        last = u["boxes"].get(name, 0)
        out += f'<{name} time="{max(0, last + cd - now())}"/>'
    return out

def act_125(a, g):  # LoadGiftBoxFreeTime
    u = U(g)
    return boxes_xml(u) if u else ""

def act_126(a, g):  # UseFree*Box
    u = U(g)
    if not u: return ""
    name = BOX_TYPE.get(str(a.get("box_type")))
    if name: u["boxes"][name] = now(); state.save()
    return boxes_xml(u)

def act_122(a, g):  return "<event_list></event_list>"        # LoadEventList
def act_32(a, g):   return f'<month_iap_list chk="0" r_t="{secs_to_midnight()}"></month_iap_list>'
def act_34(a, g):   return f'<premium_iap_list max="0" r_t="{secs_to_midnight()}"></premium_iap_list>'
def act_51(a, g):   return "<key_list></key_list>"            # LoadKeyList (friend keys)
def act_54(a, g):   return "<gift_list></gift_list>"          # LoadGiftList (mailbox)
def act_50(a, g):   return ""
def act_55(a, g):   return ""
def act_56(a, g):   return ""
def act_124(a, g):  return '<coupon code="0" msg="Coupons are not available offline."/>'
def act_139(a, g):  return ""
def act_33(a, g):   return ""
def act_37(a, g):   return ""
def act_38(a, g):   return ""

# ---------------------------------------------------------------- friends (offline: nobody)
def act_40(a, g):  return "<chksum>0</chksum>"
def act_41(a, g):  return ""
def act_42(a, g):  return ""
def act_43(a, g):  return ""
def act_44(a, g):  return ""
def act_45(a, g):  return "<frd_list></frd_list>"
def act_46(a, g):  return "<frd_list></frd_list>"
def act_47(a, g):  return "<frd_list></frd_list>"
def act_48(a, g):  return "<frd_list></frd_list>"
def act_49(a, g):  return "<frd_list></frd_list>"
def act_150(a, g): return "<mission_list></mission_list>"
def act_63(a, g):  return ""

# ---------------------------------------------------------------- pvp / arena / challenge / super (vs. bots)
def act_102(a, g):  # ArenaInfo
    return f'<arena r_t="{secs_to_midnight()}" win_cnt="0"/><win_list></win_list>'

def act_110(a, g):  return "<revenge_list></revenge_list>"

def _log(tag):
    return f'<{tag} log_idx="{now()}" log_key="offline"/>'

def pvp(u):
    return u.setdefault("pvp", {"league": 1, "score": 0, "win_cnt": 0, "match_cnt": 0})

def act_64(a, g):  return _log("pvp")          # PvPEntrance
def act_103(a, g): return _log("arena")        # ArenaEntrance
def act_107(a, g): return _log("challenge")    # ChallengeEntrance
def act_115(a, g): return _log("superman")     # SuperEnterance
def act_111(a, g): return _log("revenge")      # RevengeEnterance

def week_no():  # Monday-based week number (same formula as the Java port)
    return (datetime.date.today().toordinal() - datetime.date(1970, 1, 1).toordinal() + 3) // 7

def act_65(a, g):  # LoadPvPRank; the weekly "League Result" popup only once a week, after playing
    u = U(g)
    p = pvp(u) if u else {"league": 1, "score": 0, "win_cnt": 0, "match_cnt": 0}
    x = (f'<pvp_rank league="{p["league"]}" total="1" rank="1" score="{p["score"]}" ex_ud="0" '
         f'enter_rwd="0" win_cnt="{p["win_cnt"]}" match_cnt="{p["match_cnt"]}" l_t="{secs_to_midnight()}"/>')
    if u and p.get("reward_week") != week_no():
        if p.get("reward_week") is not None and p["match_cnt"] > 0:
            x += f'<pvp_reward league="{p["league"]}" total="1" rank="1" score="{p["score"]}"/>'
        p["reward_week"] = week_no(); state.save()
    return x

def act_66(a, g):  # PvPResult(score, result)
    u = U(g)
    if not u: return ""
    p = pvp(u)
    p["match_cnt"] += 1
    if str(a.get("result")) == "2": p["win_cnt"] += 1
    p["score"] = max(0, p["score"] + int(a.get("score", 0) or 0)); state.save()
    return f'<pvp rank="1" score="{p["score"]}" win_cnt="{p["win_cnt"]}"/>'

def act_67(a, g):  return ""                    # PvPMatching: no <char> -> client builds a bot
def act_68(a, g):  return "<char_list></char_list>"
def act_101(a, g): return '<arena idx_id="0"/>'  # ArenaMatching: 0 -> client generates random bot heroes
def act_104(a, g): return ""
def act_108(a, g): return ""
def act_113(a, g): return ""
def act_116(a, g): return ""
def act_73(a, g):  return ""
def act_112(a, g): return ""
def act_121(a, g): return ""
def act_136(a, g):
    u = U(g)
    if u and a.get("league"): pvp(u)["league"] = int(a["league"]); state.save()
    return ""
def act_137(a, g): return ""

def act_106(a, g):  # ChallengeInfo: cnt = entries used today (0 -> always full, offline has no daily cap)
    return f'<challenge b_w="0" cnt="0" max_cnt="3" r_t="{secs_to_midnight()}"/>'

def act_114(a, g):  # SuperInfo: cnt = entries used today
    return f'<superman cnt="0" max_cnt="5" r_t="{secs_to_midnight()}"/>'

def dispatch(a, g):
    if "mute" + str(a.get("act")) in _strip(): return ""
    f = globals().get("act_" + str(a.get("act")))
    return f(a, g) if f else None
