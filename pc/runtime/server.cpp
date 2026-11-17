// Embedded Plants War 2 game server (C++ port of server/handlers.py + pw2server.py + pw2crypto.py).
// Called in-process through the virtual sockets of rt_libc.cpp: no real networking at all.
#include "server.h"
#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <ctime>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <stdexcept>
#include <map>
#include <random>

namespace {
// ============================================================== dynamic values (python-like)
struct Val;
using P = std::shared_ptr<Val>;
struct Val {
    enum T { NUL, INT, DBL, STR, ARR, OBJ } t = NUL;
    int64_t i = 0; double d = 0; std::string s;
    std::vector<P> a;
    std::vector<std::pair<std::string, P>> o;
    P get(const std::string &k) const { for (auto &kv : o) if (kv.first == k) return kv.second; return nullptr; }
    bool has(const std::string &k) const { for (auto &kv : o) if (kv.first == k) return true; return false; }
    void set(const std::string &k, P v) { for (auto &kv : o) if (kv.first == k) { kv.second = v; return; } o.emplace_back(k, v); }
    void erase(const std::string &k) { for (size_t n = 0; n < o.size(); n++) if (o[n].first == k) { o.erase(o.begin() + n); return; } }
};
P I(int64_t v) { auto p = std::make_shared<Val>(); p->t = Val::INT; p->i = v; return p; }
P S(const std::string &v) { auto p = std::make_shared<Val>(); p->t = Val::STR; p->s = v; return p; }
P O() { auto p = std::make_shared<Val>(); p->t = Val::OBJ; return p; }
P L() { auto p = std::make_shared<Val>(); p->t = Val::ARR; return p; }
P OBJ(std::initializer_list<std::pair<std::string, P>> kv) { P p = O(); for (auto &x : kv) p->o.push_back(x); return p; }

std::string str(const P &v) {           // python str()
    if (!v || v->t == Val::NUL) return "None";
    switch (v->t) {
    case Val::INT: return std::to_string(v->i);
    case Val::DBL: { char b[64]; snprintf(b, sizeof b, "%.17g", v->d); return b; }
    case Val::STR: return v->s;
    default: return "?";
    }
}
int64_t num(const P &v) {               // python int()
    if (!v || v->t == Val::NUL) throw std::runtime_error("int(None)");
    if (v->t == Val::INT) return v->i;
    if (v->t == Val::DBL) return (int64_t)v->d;
    if (v->t == Val::STR) {
        std::string s = v->s; size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
        if (b == std::string::npos) throw std::runtime_error("int('')");
        s = s.substr(b, e - b + 1);
        size_t k = 0; if (s[0] == '+' || s[0] == '-') k = 1;
        if (k >= s.size()) throw std::runtime_error("int");
        for (size_t q = k; q < s.size(); q++) if (s[q] < '0' || s[q] > '9') throw std::runtime_error("int('" + s + "')");
        return std::stoll(s);
    }
    throw std::runtime_error("int(?)");
}
int64_t num(const std::string &s) { return num(S(s)); }
bool truthy(const P &v) {
    if (!v) return false;
    switch (v->t) {
    case Val::NUL: return false; case Val::INT: return v->i != 0; case Val::DBL: return v->d != 0;
    case Val::STR: return !v->s.empty(); case Val::ARR: return !v->a.empty(); default: return !v->o.empty();
    }
}
P setdef(const P &m, const std::string &k, P d) { P v = m->get(k); if (!m->has(k)) { m->set(k, d); return d; } return v; }
P sub(const P &m, const std::string &k) { return setdef(m, k, O()); }
P getd(const P &m, const std::string &k, P d) { return m->has(k) ? m->get(k) : d; }
bool contains(const P &lst, const P &v) {
    for (auto &x : lst->a) {
        if (!x || !v) { if (!x && !v) return true; continue; }
        if (x->t != v->t) continue;
        if (x->t == Val::INT && x->i == v->i) return true;
        if (x->t == Val::STR && x->s == v->s) return true;
    }
    return false;
}

// ============================================================== JSON
struct JP {
    const std::string &s; size_t i = 0;
    explicit JP(const std::string &x) : s(x) {}
    void ws() { while (i < s.size() && isspace((unsigned char)s[i])) i++; }
    std::string strv() {
        ws(); if (s[i] != '"') throw std::runtime_error("json"); i++;
        std::string r;
        while (s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { r += c; continue; }
            char e = s[i++];
            switch (e) {
            case 'n': r += '\n'; break; case 't': r += '\t'; break; case 'r': r += '\r'; break;
            case 'b': r += '\b'; break; case 'f': r += '\f'; break;
            case 'u': {
                unsigned cp = std::stoul(s.substr(i, 4), nullptr, 16); i += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && s.compare(i, 2, "\\u") == 0) {
                    unsigned lo = std::stoul(s.substr(i + 2, 4), nullptr, 16); i += 6;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                if (cp < 0x80) r += (char)cp;
                else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 63)); }
                else if (cp < 0x10000) { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 63)); r += (char)(0x80 | (cp & 63)); }
                else { r += (char)(0xF0 | (cp >> 18)); r += (char)(0x80 | ((cp >> 12) & 63)); r += (char)(0x80 | ((cp >> 6) & 63)); r += (char)(0x80 | (cp & 63)); }
                break;
            }
            default: r += e;
            }
        }
        i++; return r;
    }
    P val() {
        ws(); char c = s[i];
        if (c == '{') {
            i++; P m = O(); ws(); if (s[i] == '}') { i++; return m; }
            for (;;) { std::string k = strv(); ws(); i++; m->o.emplace_back(k, val()); ws(); char d = s[i++]; if (d == '}') return m; }
        }
        if (c == '[') {
            i++; P l = L(); ws(); if (s[i] == ']') { i++; return l; }
            for (;;) { l->a.push_back(val()); ws(); char d = s[i++]; if (d == ']') return l; }
        }
        if (c == '"') return S(strv());
        if (s.compare(i, 4, "true") == 0) { i += 4; return I(1); }
        if (s.compare(i, 5, "false") == 0) { i += 5; return I(0); }
        if (s.compare(i, 4, "null") == 0) { i += 4; return std::make_shared<Val>(); }
        size_t st = i; while (i < s.size() && strchr("+-0123456789.eE", s[i])) i++;
        std::string n = s.substr(st, i - st);
        if (n.find_first_of(".eE") != std::string::npos) { auto p = std::make_shared<Val>(); p->t = Val::DBL; p->d = atof(n.c_str()); return p; }
        return I(std::stoll(n));
    }
};
void jq(std::string &o, const std::string &s) {
    o += '"';
    for (unsigned char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r"; else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    o += '"';
}
void jw(std::string &o, const P &v, int ind) {
    if (!v || v->t == Val::NUL) { o += "null"; return; }
    std::string pad(ind + 1, ' ');
    switch (v->t) {
    case Val::INT: o += std::to_string(v->i); return;
    case Val::DBL: { char b[64]; snprintf(b, sizeof b, "%.17g", v->d); o += b; return; }
    case Val::STR: jq(o, v->s); return;
    case Val::ARR:
        if (v->a.empty()) { o += "[]"; return; }
        o += "[";
        for (size_t k = 0; k < v->a.size(); k++) { o += "\n" + pad; jw(o, v->a[k], ind + 1); if (k + 1 < v->a.size()) o += ","; }
        o += "\n" + std::string(ind, ' ') + "]"; return;
    default:
        if (v->o.empty()) { o += "{}"; return; }
        o += "{";
        for (size_t k = 0; k < v->o.size(); k++) {
            o += "\n" + pad; jq(o, v->o[k].first); o += ": "; jw(o, v->o[k].second, ind + 1);
            if (k + 1 < v->o.size()) o += ",";
        }
        o += "\n" + std::string(ind, ' ') + "}";
    }
}

// ============================================================== store
std::mutex mu;
std::string save_path;
P db;
void save() {
    std::string out; jw(out, db, 0);
    std::string tmp = save_path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    fwrite(out.data(), 1, out.size(), f); fclose(f);
    remove(save_path.c_str());
    rename(tmp.c_str(), save_path.c_str());
}
P DB() {
    if (!db) {
        FILE *f = fopen(save_path.c_str(), "rb");
        if (f) {
            std::string s; char b[65536]; size_t n;
            while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
            fclose(f);
            try { db = JP(s).val(); } catch (...) { rename(save_path.c_str(), (save_path + ".broken").c_str()); db = nullptr; }
        }
        if (!db) db = OBJ({{"next_idx", I(1001)}, {"users", O()}});
    }
    return db;
}
P users() { return sub(DB(), "users"); }
P user(const std::string &idx) { return users()->get(idx); }

// ============================================================== helpers
int64_t now() { return (int64_t)time(nullptr); }
struct tm local_now() { time_t t = time(nullptr); struct tm r;
#ifdef _WIN32
    localtime_s(&r, &t);
#else
    localtime_r(&t, &r);
#endif
    return r; }
int64_t midnight_ts() { struct tm t = local_now(); t.tm_mday += 1; t.tm_hour = t.tm_min = t.tm_sec = 0; t.tm_isdst = -1; return (int64_t)mktime(&t); }
int64_t secs_to_midnight() { return std::max<int64_t>(60, midnight_ts() - now()); }
std::string today() { struct tm t = local_now(); char b[16]; snprintf(b, sizeof b, "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday); return b; }
int64_t week_no() {   // Monday-based week number (same formula as handlers.py: (ordinal - ordinal(1970-01-01) + 3) // 7)
    struct tm t = local_now(); t.tm_hour = 12; t.tm_min = t.tm_sec = 0; t.tm_isdst = -1;
    struct tm e{}; e.tm_year = 70; e.tm_mon = 0; e.tm_mday = 1; e.tm_hour = 12; e.tm_isdst = -1;
    int64_t days = (int64_t)((mktime(&t) - mktime(&e) + 43200) / 86400);
    return (days + 3) / 7;
}
std::string esc(const std::string &s) {
    std::string r;
    for (char c : s) { if (c == '&') r += "&amp;"; else if (c == '<') r += "&lt;"; else if (c == '>') r += "&gt;"; else if (c == '"') r += "&quot;"; else r += c; }
    return r;
}
std::string esc(const P &v) { return esc(str(v)); }
std::string attrs(const P &d) {
    std::string r;
    for (auto &kv : d->o) { if (!r.empty()) r += ' '; r += kv.first + "=\"" + esc(kv.second) + "\""; }
    return r;
}
std::vector<std::pair<std::string, P>> sorted(const P &m) { auto v = m->o; std::sort(v.begin(), v.end(), [](auto &x, auto &y) { return x.first < y.first; }); return v; }

// request parameters (ordered, values are strings)
struct Args {
    std::vector<std::pair<std::string, std::string>> kv;
    const std::string *find(const std::string &k) const { const std::string *r = nullptr; for (auto &x : kv) if (x.first == k) r = &x.second; return r; }
    bool has(const std::string &k) const { return find(k) != nullptr; }
    P get(const std::string &k, P d = nullptr) const { auto p = find(k); return p ? S(*p) : d; }
    std::string gets(const std::string &k) const { auto p = find(k); return p ? *p : std::string(); }
};
std::string orv(const Args &a, const std::string &k, const std::string &dflt) { auto p = a.find(k); return (p && !p->empty()) ? *p : dflt; }

// ============================================================== player model
const std::vector<std::pair<std::string, std::string>> RES_FIELDS = {
    {"11", "iap"}, {"13", "gold"}, {"14", "key"}, {"15", "pcoin"}, {"21", "hp_p"}, {"22", "mp_p"}, {"23", "cool_p"},
    {"24", "return_s"}, {"25", "rebirth_s"}, {"28", "change_op"}, {"31", "tonic_2"}, {"32", "tonic_3"}, {"33", "tonic_4"},
    {"36", "enchant_2"}, {"37", "enchant_3"}, {"38", "enchant_4"}, {"51", "unit_1_t"}, {"52", "unit_2_t"},
    {"61", "parts_1_t"}, {"62", "parts_2_t"}, {"63", "armor_1_t"}, {"64", "armor_2_t"}};
const char *CURRENCIES[] = {"gold", "iap", "key", "pcoin"};
const int64_t CURRENCY_FULL = 9000000, STAMINA_MAX = 30, STAMINA_REGEN = 300;
P start_res() {
    return OBJ({{"gold", I(CURRENCY_FULL)}, {"iap", I(CURRENCY_FULL)}, {"key", I(CURRENCY_FULL)}, {"pcoin", I(CURRENCY_FULL)},
                {"hp_p", I(3)}, {"mp_p", I(3)}, {"cool_p", I(1)}, {"return_s", I(1)}, {"rebirth_s", I(1)}});
}
const char *flag_act8(int64_t k) {
    switch (k) { case 1: return "tutorial"; case 2: return "pvp"; case 3: return "arena"; case 4: return "challenge";
                 case 5: return "superman"; case 10: return "review"; default: return nullptr; }
}

P Ux(const std::string &idx) {
    P u = user(idx);
    if (!u) return nullptr;
    setdef(u, "res", start_res());
    setdef(u, "flags", OBJ({{"tutorial", I(1)}, {"pvp", I(0)}, {"arena", I(0)}, {"challenge", I(0)}, {"superman", I(0)}, {"review", I(1)}}));
    setdef(u, "heroes", O()); setdef(u, "hero_type", I(1)); setdef(u, "units", O()); setdef(u, "mount_units", O());
    setdef(u, "home", OBJ({{"hp", I(0)}, {"heal_hp", I(0)}})); setdef(u, "tower", OBJ({{"hp", I(0)}, {"attack", I(0)}}));
    setdef(u, "parts", O()); setdef(u, "skins", O()); setdef(u, "unit_book", L()); setdef(u, "parts_book", L());
    setdef(u, "scripts", L()); setdef(u, "stamina", OBJ({{"cnt", I(STAMINA_MAX)}, {"t", I(now())}}));
    setdef(u, "clear_ticket", OBJ({{"cnt", I(0)}})); setdef(u, "quests", O()); setdef(u, "achieve", L());
    setdef(u, "inven", OBJ({{"unit", I(0)}, {"parts", I(0)}})); setdef(u, "boxes", O()); setdef(u, "next_id", I(100001));
    return u;
}
P U(const Args &g) { auto p = g.find("idx_id"); return Ux(p ? *p : "0"); }
int64_t new_id(const P &u) { int64_t i = num(u->get("next_id")); u->set("next_id", I(i + 1)); return i; }
int cur_hero_type(const P &u, const Args &g) {
    int64_t t = num(orv(g, "type", "0"));
    return (t >= 1 && t <= 3) ? (int)t : (int)num(getd(u, "hero_type", I(1)));
}
const int64_t STARTER_PARTS[4][3] = {{0, 0, 0}, {30000, 32000, 31000}, {40000, 42000, 41000}, {50000, 52000, 51000}};
const char *STARTER_SKILL[4] = {"", "stonefall", "icespike", "poisonshot"};

P hero(const P &u, int t) {
    std::string ts = std::to_string(t);
    P heroes = u->get("heroes");
    if (!heroes->has(ts))
        heroes->set(ts, OBJ({{"info", OBJ({{"league", I(1)}, {"ldp", I(0)}, {"l_rank", I(0)}, {"area", I(1)}, {"stage", I(0)}})},
                             {"status", OBJ({{"hp", I(0)}, {"mp", I(0)}, {"attack", I(0)}, {"armor", I(0)}, {"speed", I(0)}, {"a_speed", I(0)}})},
                             {"skill", O()}, {"skill_names", O()}, {"parts", O()}, {"skin", OBJ({{"skin_id", I(0)}, {"level", I(0)}})},
                             {"stages", O()}, {"power", I(0)}}));
    P h = heroes->get(ts);
    if (!truthy(h->get("starter_given"))) {
        h->set("starter_given", I(1));
        P inv = sub(u->get("parts"), ts), hp = h->get("parts");
        for (int slot = 1; slot <= 3; slot++) {
            if (!hp->has(std::to_string(slot))) {
                int64_t pid = new_id(u);
                inv->set(std::to_string(pid), OBJ({{"parts_id", I(pid)}, {"name", I(STARTER_PARTS[t][slot - 1])}, {"level", I(1)}, {"exp", I(0)}}));
                hp->set(std::to_string(slot), S(std::to_string(pid)));
            }
        }
        setdef(h->get("skill"), "11", I(1));
        setdef(h->get("skill_names"), "1", S(STARTER_SKILL[t]));
    }
    return h;
}
std::pair<int64_t, int64_t> stamina(const P &u) {
    P s = u->get("stamina"); int64_t cnt = num(s->get("cnt")), t = num(s->get("t"));
    if (cnt < STAMINA_MAX) {
        int64_t d = now() - t, gained = d > 0 ? d / STAMINA_REGEN : 0;
        if (gained > 0) { cnt = std::min(STAMINA_MAX, cnt + gained); t += gained * STAMINA_REGEN; s->set("cnt", I(cnt)); s->set("t", I(t)); }
    } else { t = now(); s->set("t", I(t)); }
    return {cnt, cnt >= STAMINA_MAX ? 0 : STAMINA_REGEN - (now() - t)};
}
std::string res_xml(const P &u) {
    P r = u->get("res"); std::string x;
    for (auto &f : RES_FIELDS) x += "<" + f.second + ">" + std::to_string(num(getd(r, f.second, I(0)))) + "</" + f.second + ">";
    return x;
}
int64_t change_res(const P &u, const P &typ0, const P &chk, const P &cnt0) {
    int64_t typ = num(typ0), cnt = num(cnt0); bool add = num(chk) != 0;
    if (typ == 26) { int64_t c = stamina(u).first; int64_t v = std::max<int64_t>(0, add ? c + cnt : c - cnt); u->get("stamina")->set("cnt", I(v)); return v; }
    std::string f = "item_" + std::to_string(typ);
    for (auto &kv : RES_FIELDS) if (kv.first == std::to_string(typ)) f = kv.second;
    P r = u->get("res"); int64_t v = std::max<int64_t>(0, num(getd(r, f, I(0))) + (add ? cnt : -cnt));
    r->set(f, I(v)); return v;
}
std::string hero_xml(const P &u, int t) {
    P h = hero(u, t);
    std::string x = "<info " + attrs(h->get("info")) + "/><status " + attrs(h->get("status")) + "/><skill " + attrs(h->get("skill")) + "/>";
    for (auto &kv : sorted(h->get("skill_names"))) x += "<skill_" + kv.first + " name=\"" + esc(kv.second) + "\"/>";
    P parts = u->get("parts")->get(std::to_string(t)); if (!parts) parts = O();
    for (auto &kv : sorted(h->get("parts"))) { P p = parts->get(str(kv.second)); if (p) x += "<parts_" + kv.first + " " + attrs(p) + "/>"; }
    return x + "<skin " + attrs(h->get("skin")) + "/>";
}
P parts_of(const P &u, const Args &g) { return sub(u->get("parts"), std::to_string(cur_hero_type(u, g))); }
P pvp(const P &u) { return setdef(u, "pvp", OBJ({{"league", I(1)}, {"score", I(0)}, {"win_cnt", I(0)}, {"match_cnt", I(0)}})); }
std::string log_tag(const std::string &t) { return "<" + t + " log_idx=\"" + std::to_string(now()) + "\" log_key=\"offline\"/>"; }
const char *BOXES[] = {"luckybox", "s_partsbox", "s_armorbox", "s_unitbox"};
const int64_t BOX_CD[] = {4 * 3600, 24 * 3600, 24 * 3600, 24 * 3600};
std::string boxes_xml(const P &u) {
    std::string x; P bx = u->get("boxes");
    for (int k = 0; k < 4; k++) x += std::string("<") + BOXES[k] + " time=\"" + std::to_string(std::max<int64_t>(0, num(getd(bx, BOXES[k], I(0))) + BOX_CD[k] - now())) + "\"/>";
    return x;
}
const char *UNIT_KEYS[] = {"name", "max_grade", "grade", "level", "exp"};

// ============================================================== acts
bool dispatch(int act, const Args &a, const Args &g, std::string &x) {
    switch (act) {
    case 2: x = "<date time=\"" + std::to_string(now()) + "\"/>"; return true;
    case 3: { struct tm t = local_now(); x = "<date date=\"" + std::to_string((t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday) + "\" week=\"" + std::to_string(t.tm_wday) + "\"/>"; return true; }
    case 4: x = "<user gift=\"0\"/>"; return true;
    case 5: case 7: case 16: case 19: case 9: case 10: case 23: case 30: case 31: case 91: case 135: return true;
    case 8: { P u = U(g); const char *f = flag_act8(num(a.get("chk", I(0)))); if (u && f) { u->get("flags")->set(f, I(!strcmp(f, "tutorial") || !strcmp(f, "review") ? 0 : 1)); save(); } return true; }
    case 20: {
        P d = DB(); int64_t idx = num(d->get("next_idx")); d->set("next_idx", I(idx + 1));
        P u = OBJ({{"idx_id", I(idx)}, {"idx_time", I(now())}, {"nick", S(a.has("nick") ? a.gets("nick") : "Player")}, {"sns_type", S(a.has("sns_type") ? a.gets("sns_type") : "0")},
                   {"sns_id", S(a.gets("id"))}, {"skey", S("offline" + std::to_string(idx))}, {"created", I(now())}});
        users()->set(std::to_string(idx), u); save();
        x = "<idx_id>" + std::to_string(idx) + "</idx_id><idx_time>" + str(u->get("idx_time")) + "</idx_time>"; return true;
    }
    case 24: {
        std::string id = a.gets("id");
        for (auto &kv : users()->o) if (!id.empty() && str(kv.second->get("sns_id")) == id) { x = "<idx_id>" + str(kv.second->get("idx_id")) + "</idx_id><idx_time>" + str(kv.second->get("idx_time")) + "</idx_time>"; break; }
        return true;
    }
    case 21: { P u = U(g); if (u) { int t = cur_hero_type(u, g); hero(u, t); u->set("hero_type", I(t)); save(); } return true; }
    case 6: { P u = U(g); if (u) { int t = (int)num(a.get("hero_type", I(1))); hero(u, t); u->set("hero_type", I(t)); save(); } return true; }
    case 22: {
        auto ip = a.find("idx_id"); P u = Ux(ip ? *ip : (g.find("idx_id") ? *g.find("idx_id") : "None"));
        if (!u) { x = "<block chk=\"3\"/>"; return true; }
        P fl = u->get("flags");
        x = "<skey>" + esc(u->get("skey")) + "</skey><nick>" + esc(u->get("nick")) + "</nick><sns_list></sns_list>";
        for (const char *k : {"tutorial", "pvp", "arena", "challenge", "superman"}) x += std::string("<") + k + ">" + str(getd(fl, k, I(0))) + "</" + k + ">";
        x += "<superman_chk>1</superman_chk><crm_chk>0</crm_chk><review>" + str(getd(fl, "review", I(0))) + "</review>";
        x += "<last_hero>" + (truthy(u->get("heroes")) ? str(u->get("hero_type")) : std::string("0")) + "</last_hero><alarm>3</alarm><pvp_result>0</pvp_result><win_restore>0</win_restore>";
        P res = u->get("res");
        for (const char *cur : CURRENCIES) if (num(getd(res, cur, I(0))) < CURRENCY_FULL) res->set(cur, I(CURRENCY_FULL));
        u->set("last_login", I(now())); save(); return true;
    }
    case 29: x = "<change code=\"OFFLINE\" l_t=\"600\"/>"; return true;
    case 89: { P u = U(g); if (u) x = res_xml(u); return true; }
    case 15: x = "<item_list><item type=\"26\" max=\"" + std::to_string(STAMINA_MAX) + "\"/></item_list>"; return true;
    case 87: { P u = U(g); if (!u) return true; int64_t v = change_res(u, a.get("money_type", I(13)), a.get("money_chk", I(0)), a.get("money_cnt", I(0))); save();
               x = "<money type=\"" + str(a.get("money_type")) + "\" count=\"" + std::to_string(v) + "\"/>"; return true; }
    case 88: { P u = U(g); if (!u) return true; int64_t v = change_res(u, a.get("item_type", I(0)), a.get("item_chk", I(0)), a.get("item_cnt", I(0))); save();
               x = "<item type=\"" + str(a.get("item_type")) + "\" count=\"" + std::to_string(v) + "\"/>"; return true; }
    case 59: { P u = U(g); if (!u) return true; auto st = stamina(u);
               x = "<stamina cnt=\"" + std::to_string(st.first) + "\" r_t=\"" + std::to_string(st.second) + "\" max_cnt=\"" + std::to_string(STAMINA_MAX) + "\" re_t=\"" + std::to_string(STAMINA_REGEN) + "\"/>"; return true; }
    case 58: {
        P u = U(g); if (!u) return true;
        int64_t c = stamina(u).first, n = num(a.get("cnt", I(0)));
        c = num(a.get("chk", I(0))) ? c + n : std::max<int64_t>(0, c - n);
        P st = u->get("stamina");
        if (num(st->get("cnt")) >= STAMINA_MAX && c < STAMINA_MAX) st->set("t", I(now()));
        st->set("cnt", I(c));
        int64_t rt = stamina(u).second; save();
        x = "<stamina cnt=\"" + std::to_string(c) + "\" r_t=\"" + std::to_string(rt) + "\"/>"; return true;
    }
    case 133: { P u = U(g); x = "<clear_t r_t=\"" + std::to_string(secs_to_midnight()) + "\" cnt=\"" + (u ? str(u->get("clear_ticket")->get("cnt")) : std::string("0")) + "\" max_cnt=\"5\" add_cnt=\"0\" bpc_cnt=\"0\" add_max_cnt=\"0\" max_r_t=\"86400\"/>"; return true; }
    case 134: { P u = U(g); if (u) { int64_t n = num(a.get("cnt", I(0))); P ct = u->get("clear_ticket"); int64_t c = num(ct->get("cnt"));
                ct->set("cnt", I(num(a.get("chk", I(0))) ? c + n : std::max<int64_t>(0, c - n))); save(); } return true; }
    case 138: { P u = U(g); if (!u) return true; std::string kind = a.gets("sort") == "1" ? "unit" : "parts"; P inv = u->get("inven");
                inv->set(kind, I(num(getd(inv, kind, I(0))) + 5)); save();
                x = "<inven sort=\"" + str(a.get("sort")) + "\" add_cnt=\"" + str(inv->get(kind)) + "\"/>"; return true; }
    case 85: {
        auto fp = a.find("f_idx_id"); std::string idx = (fp && !fp->empty()) ? *fp : (g.find("idx_id") ? *g.find("idx_id") : "None");
        P u = Ux(idx); if (!u) return true;
        int ft = (int)num(orv(a, "f_type", "0")); std::vector<int> types = ft ? std::vector<int>{ft} : std::vector<int>{1, 2, 3};
        std::string b; for (int t : types) b += "<hero type=\"" + std::to_string(t) + "\">" + hero_xml(u, t) + "</hero>";
        save(); x = "<user idx_id=\"" + str(u->get("idx_id")) + "\" nick=\"" + esc(u->get("nick")) + "\">" + b + "</user>"; return true;
    }
    case 82: { P u = U(g); if (!u) return true; P st = hero(u, cur_hero_type(u, g))->get("status");
               for (auto &kv : st->o) if (a.has(kv.first)) kv.second = I(num(a.gets(kv.first)));
               save(); x = "<status " + attrs(st) + "/>"; return true; }
    case 83: { P u = U(g); if (!u) return true; P sk = hero(u, cur_hero_type(u, g))->get("skill");
               int64_t n = num(a.get("skill", I(0))); std::string key = std::to_string(n); int64_t lv = num(getd(sk, key, I(0))) + 1;
               sk->set(key, I(lv)); save(); x = "<hero skill=\"" + std::to_string(n) + "\" lv=\"" + std::to_string(lv) + "\"/>"; return true; }
    case 90: {
        P u = U(g); if (!u) return true; int t = cur_hero_type(u, g); P h = hero(u, t);
        for (auto &kv : a.kv) {
            const std::string &k = kv.first, &v = kv.second;
            if (k.rfind("parts_", 0) == 0) { std::string slot = k.substr(6); if (v == "0" || v.empty()) h->get("parts")->erase(slot); else h->get("parts")->set(slot, S(v)); }
            else if (k.rfind("skill_", 0) == 0) h->get("skill_names")->set(k.substr(6), S(v));
            else if (k == "skin_id") {
                P skin = h->get("skin"); skin->set("skin_id", I(num(v)));
                P skT = u->get("skins")->get(std::to_string(t)); P sk = skT ? skT->get(v) : nullptr;
                skin->set("level", I(truthy(sk) ? num(getd(sk, "level", I(0))) : 0));
            } else if (k == "power") h->set("power", I(num(v)));
        }
        save(); return true;
    }
    case 81: { P u = U(g); if (!u) return true;
               const char *f[4][3] = {{"home_hp", "home", "hp"}, {"home_heal_hp", "home", "heal_hp"}, {"tower_hp", "tower", "hp"}, {"tower_attack", "tower", "attack"}};
               for (auto &e : f) if (a.has(e[0])) u->get(e[1])->set(e[2], I(num(a.gets(e[0]))));
               save(); x = "<home " + attrs(u->get("home")) + "/><tower " + attrs(u->get("tower")) + "/>"; return true; }
    case 94: { P u = U(g); if (!u) return true; std::string b; for (auto &kv : u->get("units")->o) b += "<unit " + attrs(kv.second) + "/>";
               x = "<unit_list add_cnt=\"" + str(getd(u->get("inven"), "unit", I(0))) + "\" bpc_add_cnt=\"0\" max_add_cnt=\"100\">" + b + "</unit_list>"; return true; }
    case 98: { P u = U(g); if (!u) return true; int64_t i = new_id(u); P un = OBJ({{"unit_id", I(i)}});
               for (const char *k : UNIT_KEYS) un->set(k, a.get(k, I(0)));
               u->get("units")->set(std::to_string(i), un);
               P book = u->get("unit_book"); if (!contains(book, un->get("name"))) book->a.push_back(un->get("name"));
               save(); x = "<unit idx=\"" + str(a.get("idx", I(0))) + "\" id=\"" + std::to_string(i) + "\"/>"; return true; }
    case 99: { P u = U(g); if (u && u->get("units")->has(str(a.get("unit_id")))) { P un = u->get("units")->get(str(a.get("unit_id")));
               for (const char *k : UNIT_KEYS) if (a.has(k)) un->set(k, a.get(k)); save(); } return true; }
    case 100: { P u = U(g); if (u) { std::string uid = str(a.get("unit_id")); u->get("units")->erase(uid); P mu_ = u->get("mount_units");
                for (size_t n = mu_->o.size(); n-- > 0;) if (str(mu_->o[n].second) == uid) mu_->o.erase(mu_->o.begin() + n); save(); } return true; }
    case 84: { P u = U(g); if (u) { P mu_ = u->get("mount_units");
               for (auto &kv : a.kv) if (kv.first.rfind("unit_", 0) == 0) { std::string slot = kv.first.substr(5);
                   if (kv.second == "0" || kv.second.empty()) mu_->erase(slot); else mu_->set(slot, S(kv.second)); }
               save(); } return true; }
    case 86: {
        auto fp = a.find("f_idx_id"); std::string idx = (fp && !fp->empty()) ? *fp : (g.find("idx_id") ? *g.find("idx_id") : "None");
        P u = Ux(idx); if (!u) return true;
        std::string b; P units = u->get("units");
        for (auto &kv : sorted(u->get("mount_units"))) { P un = units->get(str(kv.second)); if (un) b += "<unit_" + kv.first + " " + attrs(un) + "/>"; }
        x = "<user idx_id=\"" + str(u->get("idx_id")) + "\"><home " + attrs(u->get("home")) + "/><tower " + attrs(u->get("tower")) + "/><unit_list>" + b + "</unit_list></user>"; return true;
    }
    case 74: { P u = U(g); std::string b; if (u) for (auto &n : u->get("unit_book")->a) b += "<book name=\"" + esc(n) + "\"/>"; x = "<book_list>" + b + "</book_list>"; return true; }
    case 92: { P u = U(g); std::string b; if (u) for (auto &n : u->get("parts_book")->a) b += "<p_book name=\"" + esc(n) + "\"/>"; x = "<p_book_list>" + b + "</p_book_list>"; return true; }
    case 93: { P u = U(g); if (!u) return true; std::string b; for (auto &kv : parts_of(u, g)->o) b += "<parts " + attrs(kv.second) + "/>";
               x = "<parts_list add_cnt=\"" + str(getd(u->get("inven"), "parts", I(0))) + "\" bpc_add_cnt=\"0\" max_add_cnt=\"100\">" + b + "</parts_list>"; return true; }
    case 95: { P u = U(g); if (!u) return true; int64_t i = new_id(u); P p = OBJ({{"parts_id", I(i)}});
               for (auto &kv : a.kv) if (kv.first != "act" && kv.first != "idx" && kv.first != "link") p->set(kv.first, S(kv.second));
               parts_of(u, g)->set(std::to_string(i), p);
               P book = u->get("parts_book"); if (truthy(p->get("name")) && !contains(book, p->get("name"))) book->a.push_back(p->get("name"));
               save(); x = "<parts idx=\"" + str(a.get("idx", I(0))) + "\" id=\"" + std::to_string(i) + "\"/>"; return true; }
    case 96: { P u = U(g); if (u) { P p = parts_of(u, g)->get(str(a.get("parts_id")));
               if (p) { for (auto &kv : a.kv) if (kv.first != "act" && kv.first != "link" && kv.first != "parts_id") p->set(kv.first, S(kv.second)); save(); } } return true; }
    case 97: { P u = U(g); if (u) { std::string pid = str(a.get("parts_id")); parts_of(u, g)->erase(pid); P hp = hero(u, cur_hero_type(u, g))->get("parts");
               for (size_t n = hp->o.size(); n-- > 0;) if (str(hp->o[n].second) == pid) hp->o.erase(hp->o.begin() + n); save(); } return true; }
    case 130: { P u = U(g); if (!u) return true; P sk = u->get("skins")->get(std::to_string(cur_hero_type(u, g)));
                x = "<skin_list>"; if (sk) for (auto &kv : sk->o) x += "<skin " + attrs(kv.second) + "/>"; x += "</skin_list>"; return true; }
    case 131: { P u = U(g); if (u) { P sk = sub(u->get("skins"), std::to_string(cur_hero_type(u, g)));
                sk->set(str(a.get("skin_id")), OBJ({{"skin_id", a.get("skin_id")}, {"level", a.get("level", I(1))}, {"e_cnt", I(0)}})); save(); } return true; }
    case 132: { P u = U(g); if (u) { P sk = sub(u->get("skins"), std::to_string(cur_hero_type(u, g)));
                P s = setdef(sk, str(a.get("skin_id")), OBJ({{"skin_id", a.get("skin_id")}}));
                s->set("level", a.get("level", getd(s, "level", I(1)))); s->set("e_cnt", a.get("e_cnt", I(0)));
                P skin = hero(u, cur_hero_type(u, g))->get("skin");
                if (str(skin->get("skin_id")) == str(a.get("skin_id"))) skin->set("level", I(num(s->get("level"))));
                save(); } return true; }
    case 61: x = "<stage log_idx=\"" + std::to_string(now()) + "\" log_key=\"offline\"/>"; return true;
    case 60: {
        P u = U(g); int64_t result = num(a.get("result", I(0)));
        if (u && (result == 2 || result == 4)) {
            P h = hero(u, cur_hero_type(u, g));
            int64_t area = num(a.get("area", I(1))), st = num(a.get("stage", I(1))), star = num(a.get("star", I(0)));
            std::string key = std::to_string(area) + "-" + std::to_string(st);
            P stages = h->get("stages"); stages->set(key, I(std::max(num(getd(stages, key, I(0))), star)));
            P inf = h->get("info"); int64_t ia = num(inf->get("area")), is = num(inf->get("stage"));
            if (area > ia || (area == ia && st > is)) { inf->set("area", I(area)); inf->set("stage", I(st)); }
            save();
        }
        return true;
    }
    case 62: {
        P u = U(g); int64_t area = num(a.get("area", I(1))); std::string body; P rw = O();
        if (u) {
            P h = hero(u, cur_hero_type(u, g)); P r = sub(h, "star_rwd")->get(std::to_string(area)); if (r) rw = r;
            for (auto &kv : sorted(h->get("stages"))) {
                size_t d = kv.first.find('-');
                if (num(kv.first.substr(0, d)) == area) body += "<stage stage=\"" + std::to_string(num(kv.first.substr(d + 1))) + "\" star=\"" + str(kv.second) + "\"/>";
            }
        }
        std::string r; for (int k = 1; k <= 4; k++) { if (k > 1) r += ' '; r += "rwd_" + std::to_string(k) + "=\"" + str(getd(rw, std::to_string(k), I(0))) + "\""; }
        x = "<stage_list area=\"" + std::to_string(area) + "\" " + r + ">" + body + "</stage_list>"; return true;
    }
    case 57: { P u = U(g); if (u) { P h = hero(u, cur_hero_type(u, g)); sub(sub(h, "star_rwd"), str(a.get("area", I(1))))->set(str(a.get("rwd_type", I(1))), I(1)); save(); } return true; }
    case 70: { P u = U(g); P sid = a.get("script_id"); if (u && truthy(sid) && !contains(u->get("scripts"), sid)) { u->get("scripts")->a.push_back(sid); save(); } return true; }
    case 71: { P u = U(g); x = "<script>"; if (u) for (auto &s_ : u->get("scripts")->a) x += "<s>" + esc(s_) + "</s>"; x += "</script>"; return true; }
    case 76: { P u = U(g); if (u) { std::string key = str(a.get("type")) + "-" + str(a.get("id"));
               P q = setdef(u->get("quests"), key, OBJ({{"type", a.get("type")}, {"id", a.get("id")}, {"cnt", I(0)}, {"chk", I(0)}}));
               q->set("chk", I(1)); q->set("day", S(today())); save(); } return true; }
    case 77: case 75: {
        P u = U(g); std::string td = today(), b;
        if (u) for (auto &kv : u->get("quests")->o) {
            P q = kv.second, ty = q->get("type");
            bool daily = ty && ((ty->t == Val::STR && ty->s == "1") || (ty->t == Val::INT && ty->i == 1));
            if (daily && str(q->get("day")) != td) continue;
            b += "<quest type=\"" + str(q->get("type")) + "\" id=\"" + str(q->get("id")) + "\" cnt=\"" + str(getd(q, "cnt", I(0))) + "\" chk=\"" + str(getd(q, "chk", I(0))) + "\"/>";
        }
        x = "<quest_list>" + b + "</quest_list>"; return true;
    }
    case 79: { P u = U(g); P id = a.get("id"); if (u && truthy(id) && !contains(u->get("achieve"), id)) { u->get("achieve")->a.push_back(id); save(); } return true; }
    case 78: { P u = U(g); if (u && !a.gets("type").empty()) { P ar = sub(u, "a_r"); std::string t = a.gets("type");
               ar->set(t, I(num(getd(ar, t, I(0))) + num(orv(a, "chg", "0")))); save(); } return true; }
    case 80: { P u = U(g); std::string b; if (u) for (auto &i_ : u->get("achieve")->a) b += "<a_c id=\"" + esc(i_) + "\"/>";
               x = "<a_r " + (u ? attrs(getd(u, "a_r", O())) : std::string()) + "/><a_c_list>" + b + "</a_c_list>"; return true; }
    case 120: { P u = U(g); if (!u) return true; P tr = setdef(u, "today", OBJ({{"cnt", I(0)}, {"day", S("")}}));
                int chk = str(tr->get("day")) == today() ? 1 : 0;
                x = "<tdr cnt=\"" + str(tr->get("cnt")) + "\" chk=\"" + std::to_string(chk) + "\" r_t=\"" + std::to_string(midnight_ts() - now()) + "\"/>"; return true; }
    case 72: { P u = U(g); if (u) { P tr = setdef(u, "today", OBJ({{"cnt", I(0)}, {"day", S("")}})); std::string td = today();
               if (str(tr->get("day")) != td) { tr->set("day", S(td)); tr->set("cnt", I(a.has("cnt") ? num(a.gets("cnt")) : num(tr->get("cnt")) + 1)); }
               save(); } return true; }
    case 125: { P u = U(g); if (u) x = boxes_xml(u); return true; }
    case 126: { P u = U(g); if (!u) return true; std::string bt = str(a.get("box_type")); const char *name = nullptr;
                if (bt == "1") name = "luckybox"; else if (bt == "2") name = "s_partsbox"; else if (bt == "3") name = "s_armorbox"; else if (bt == "4") name = "s_unitbox";
                if (name) { u->get("boxes")->set(name, I(now())); save(); }
                x = boxes_xml(u); return true; }
    case 122: x = "<event_list></event_list>"; return true;
    case 32: x = "<month_iap_list chk=\"0\" r_t=\"" + std::to_string(secs_to_midnight()) + "\"></month_iap_list>"; return true;
    case 34: x = "<premium_iap_list max=\"0\" r_t=\"" + std::to_string(secs_to_midnight()) + "\"></premium_iap_list>"; return true;
    case 51: x = "<key_list></key_list>"; return true;
    case 54: x = "<gift_list></gift_list>"; return true;
    case 50: case 55: case 56: case 139: case 33: case 37: case 38: case 41: case 42: case 43: case 44: case 63:
    case 67: case 104: case 108: case 113: case 116: case 73: case 112: case 121: case 137: return true;
    case 124: x = "<coupon code=\"0\" msg=\"Coupons are not available offline.\"/>"; return true;
    case 40: x = "<chksum>0</chksum>"; return true;
    case 45: case 46: case 47: case 48: case 49: x = "<frd_list></frd_list>"; return true;
    case 150: x = "<mission_list></mission_list>"; return true;
    case 102: x = "<arena r_t=\"" + std::to_string(secs_to_midnight()) + "\" win_cnt=\"0\"/><win_list></win_list>"; return true;
    case 110: x = "<revenge_list></revenge_list>"; return true;
    case 64: x = log_tag("pvp"); return true;
    case 103: x = log_tag("arena"); return true;
    case 107: x = log_tag("challenge"); return true;
    case 115: x = log_tag("superman"); return true;
    case 111: x = log_tag("revenge"); return true;
    case 65: {
        P u = U(g); P p = u ? pvp(u) : OBJ({{"league", I(1)}, {"score", I(0)}, {"win_cnt", I(0)}, {"match_cnt", I(0)}});
        x = "<pvp_rank league=\"" + str(p->get("league")) + "\" total=\"1\" rank=\"1\" score=\"" + str(p->get("score")) + "\" ex_ud=\"0\" enter_rwd=\"0\" win_cnt=\"" +
            str(p->get("win_cnt")) + "\" match_cnt=\"" + str(p->get("match_cnt")) + "\" l_t=\"" + std::to_string(secs_to_midnight()) + "\"/>";
        if (u && (!p->has("reward_week") || num(p->get("reward_week")) != week_no())) {
            if (p->has("reward_week") && num(p->get("match_cnt")) > 0)
                x += "<pvp_reward league=\"" + str(p->get("league")) + "\" total=\"1\" rank=\"1\" score=\"" + str(p->get("score")) + "\"/>";
            p->set("reward_week", I(week_no())); save();
        }
        return true;
    }
    case 66: { P u = U(g); if (!u) return true; P p = pvp(u); p->set("match_cnt", I(num(p->get("match_cnt")) + 1));
               if (str(a.get("result")) == "2") p->set("win_cnt", I(num(p->get("win_cnt")) + 1));
               p->set("score", I(std::max<int64_t>(0, num(p->get("score")) + num(orv(a, "score", "0"))))); save();
               x = "<pvp rank=\"1\" score=\"" + str(p->get("score")) + "\" win_cnt=\"" + str(p->get("win_cnt")) + "\"/>"; return true; }
    case 68: x = "<char_list></char_list>"; return true;
    case 101: x = "<arena idx_id=\"0\"/>"; return true;
    case 136: { P u = U(g); if (u && !a.gets("league").empty()) { pvp(u)->set("league", I(num(a.gets("league")))); save(); } return true; }
    case 106: x = "<challenge b_w=\"0\" cnt=\"0\" max_cnt=\"3\" r_t=\"" + std::to_string(secs_to_midnight()) + "\"/>"; return true;
    case 114: x = "<superman cnt=\"0\" max_cnt=\"5\" r_t=\"" + std::to_string(secs_to_midnight()) + "\"/>"; return true;
    }
    return false;
}

// ============================================================== transport cipher (pw2crypto.py)
int swp(int b) { return ((b & 0xF) << 4) | ((b >> 4) & 0xF); }
int next_step(int s) { return s == 0xFD ? 0 : s == 0xFE ? 1 : s + 2; }
std::string b64e(const std::vector<uint8_t> &b) {
    static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-/";   // '+' -> '-'
    std::string r;
    for (size_t i = 0; i < b.size(); i += 3) {
        uint32_t n = b[i] << 16 | (i + 1 < b.size() ? b[i + 1] << 8 : 0) | (i + 2 < b.size() ? b[i + 2] : 0);
        r += T[n >> 18 & 63]; r += T[n >> 12 & 63];
        r += i + 1 < b.size() ? T[n >> 6 & 63] : '='; r += i + 2 < b.size() ? T[n & 63] : '=';
    }
    return r;
}
std::vector<uint8_t> b64d(const std::string &s) {
    std::vector<uint8_t> o; uint32_t acc = 0; int bits = 0;
    for (char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26; else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-' || c == ' ') v = 62; else if (c == '/') v = 63; else continue;
        acc = acc << 6 | v; bits += 6;
        if (bits >= 8) { bits -= 8; o.push_back((uint8_t)(acc >> bits)); }
    }
    return o;
}
std::string decode(const std::string &b64) {
    std::vector<uint8_t> d = b64d(b64);
    if (d.size() < 12) return "";
    int key[8]; for (int j = 0; j < 8; j++) key[j] = swp(d[j]) ^ 0x69;
    std::string out; int st = 0, s = 0x10;
    for (size_t i = 12; i < d.size(); i++) {
        int b = d[i], k = key[(i - 12) % 8], v;
        if (st == 0 || st == 4) { v = swp(b ^ k); st = 1; } else if (st == 1) { v = swp(swp(b) ^ k); st = 2; }
        else if (st == 2) { v = swp(b) ^ k; st = 3; } else { v = b ^ k; st = 4; }
        out += (char)(v ^ s); s = next_step(s);
    }
    return out;
}
std::string encode(const std::string &plain) {
    static std::mt19937 rng((unsigned)time(nullptr));
    int key[8]; for (int j = 0; j < 8; j++) key[j] = (int)(rng() % 0xF0) + 0x10;
    std::vector<uint8_t> out(12 + plain.size());
    int64_t c = 0; int st = 0, s = 0x10;
    for (size_t i = 0; i < plain.size(); i++) {
        int p = (uint8_t)plain[i], k = key[i % 8], x = p ^ s, o;
        if (c > 0x7FFFFEFE) c = 0;
        c += p;
        if (st == 0 || st == 4) { o = swp(x) ^ k; st = 1; } else if (st == 1) { o = swp(swp(x) ^ k); st = 2; }
        else if (st == 2) { o = swp(x ^ k); st = 3; } else { o = x ^ k; st = 4; }
        out[12 + i] = (uint8_t)o; s = next_step(s);
    }
    for (int j = 0; j < 8; j++) out[j] = (uint8_t)swp(key[j] ^ 0x69);
    out[8] = (uint8_t)((c & 0xFF) ^ 0x2F ^ key[0] ^ key[4]); out[9] = (uint8_t)(((c >> 8) & 0xFF) ^ 0xB3 ^ key[1] ^ key[5]);
    out[10] = (uint8_t)(((c >> 16) & 0xFF) ^ 0x58 ^ key[2] ^ key[6]); out[11] = (uint8_t)(((c >> 24) & 0xFF) ^ 0xD8 ^ key[3] ^ key[7]);
    return b64e(out);
}

// ============================================================== HTTP
int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
std::string unquote(const std::string &s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') r += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) { r += (char)(hexv(s[i + 1]) * 16 + hexv(s[i + 2])); i += 2; }
        else r += s[i];
    }
    return r;
}
Args parse_qs(const std::string &s, bool keep_blank) {
    Args a; size_t p = 0;
    while (p <= s.size()) {
        size_t e = s.find('&', p); if (e == std::string::npos) e = s.size();
        std::string part = s.substr(p, e - p); p = e + 1;
        if (part.empty()) { if (e == s.size()) break; continue; }
        size_t q = part.find('=');
        std::string k, v;
        if (q == std::string::npos) { if (!keep_blank) continue; k = part; }
        else { k = part.substr(0, q); v = part.substr(q + 1); }
        if (v.empty() && !keep_blank) continue;
        a.kv.emplace_back(unquote(k), unquote(v));
        if (e == s.size()) break;
    }
    return a;
}
std::string handle_action(const std::string &plain) {
    std::string acts = plain, glob;
    size_t k = plain.find("&&&");
    if (k != std::string::npos) { acts = plain.substr(0, k); glob = plain.substr(k + 3); }
    Args g = parse_qs(glob, true);
    std::string out = "<result><msg>1</msg>";
    size_t p = 0;
    while (p <= acts.size()) {
        size_t e = acts.find("&&", p); if (e == std::string::npos) e = acts.size();
        std::string chunk = acts.substr(p, e - p); p = e + 2;
        if (!chunk.empty()) {
            Args a = parse_qs(chunk, true);
            std::string x, actv = a.has("act") ? a.gets("act") : "None";
            try {
                int act = std::stoi(actv);
                if (!dispatch(act, a, g, x)) fprintf(stderr, "[pw2 server] unhandled act %s\n", chunk.c_str());
            } catch (std::exception &ex) { x.clear(); fprintf(stderr, "[pw2 server] act %s: %s\n", chunk.c_str(), ex.what()); }
            out += "<act v=\"" + actv + "\">" + x + "</act>";
        }
        if (e == acts.size()) break;
    }
    return out + "</result>";
}
std::string route(int port, const std::string &path, const std::string &body, bool &found) {
    found = true;
    std::string lp = path; for (auto &c : lp) c = (char)tolower((unsigned char)c);
    auto ends = [&](const char *suf) { size_t n = strlen(suf); return lp.size() >= n && lp.compare(lp.size() - n, n, suf) == 0; };
    if (port == 12380) {
        Args f = parse_qs(body, true);
        auto pd = f.find("PD");
        if (!pd) { found = false; return ""; }
        std::string plain = decode(*pd), xml;
        if (ends("update.php")) {
            std::string act = parse_qs(plain, false).gets("act");
            if (act == "12") xml = "<result><msg>1</msg><act><file_list></file_list></act></result>";
            else if (act == "11") xml = "<result><msg>1</msg><act v=\"11\"><cdn ver=\"1\" name=\"filemap.xml\"/></act></result>";
            else xml = "<result><msg>0</msg></result>";
        } else if (ends("action.php") || ends("login.php")) {
            std::lock_guard<std::mutex> l(mu);
            xml = handle_action(plain);
        } else { found = false; return ""; }
        return encode(xml);
    }
    if (ends("getserviceinfo.aspx")) return "{\"Code\": \"0\", \"Url\": \"http://127.0.0.1:12380\"}";
    if (ends("gettermsinfo.aspx")) return "{\"Terms\": \"\", \"Privacy\": \"\"}";
    if (ends("getnoticelist.aspx")) return "{\"Event\": {\"Count\": \"0\", \"List\": []}, \"Notice\": {\"Count\": \"0\", \"List\": []}}";
    if (port == 10080) return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plantswar2>\n</plantswar2>\n";
    if (ends("getendinginfo.aspx")) return "{\"ImageURL\": \"\", \"PageURL\": \"\", \"LinkURL\": \"\"}";
    found = false; return "";
}
}  // namespace

void server_init(const std::string &path) { std::lock_guard<std::mutex> l(mu); save_path = path; db = nullptr; DB(); }

bool server_http(int port, const std::string &req, std::string &resp) {
    size_t he = req.find("\r\n\r\n");
    if (he == std::string::npos) return false;
    size_t clen = 0;
    std::string head = req.substr(0, he);
    for (size_t p = 0; p < head.size();) {
        size_t e = head.find("\r\n", p); if (e == std::string::npos) e = head.size();
        std::string line = head.substr(p, e - p); p = e + 2;
        std::string low = line; for (auto &c : low) c = (char)tolower((unsigned char)c);
        if (low.rfind("content-length:", 0) == 0) clen = (size_t)atol(line.c_str() + 15);
    }
    if (req.size() < he + 4 + clen) return false;                         // body not complete yet
    std::string body = req.substr(he + 4, clen);
    size_t s1 = head.find(' '), s2 = head.find(' ', s1 + 1);
    std::string path = head.substr(s1 + 1, s2 - s1 - 1);
    size_t q = path.find('?'); if (q != std::string::npos) path = path.substr(0, q);
    bool found; std::string out = route(port, path, body, found);
    resp = std::string(found ? "HTTP/1.1 200 OK" : "HTTP/1.1 404 Not Found") + "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
           std::to_string(out.size()) + "\r\nConnection: close\r\n\r\n" + out;
    return true;
}

std::string server_replay(const std::string &plain) { std::lock_guard<std::mutex> l(mu); return handle_action(plain); }
