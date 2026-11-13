package pw2.offline;

import java.util.ArrayList;
import java.util.Calendar;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;

/**
 * Per-act response builders (Java port of server/handlers.py; keep the two in sync).
 *
 * Every request carries one or more "acts"; each returns the inner XML of {@code <act v="N">...</act>}.
 * The client is the source of truth for game rules: it computes rewards/costs locally and reports
 * every change, the server stores the resulting state and assigns ids for new units/parts.
 */
@SuppressWarnings("unchecked")
final class Handlers {
    private Handlers() {}

    // ------------------------------------------------------------ helpers
    static long now() { return System.currentTimeMillis() / 1000L; }

    private static Calendar midnight() {
        Calendar c = Calendar.getInstance();
        c.add(Calendar.DAY_OF_MONTH, 1);
        c.set(Calendar.HOUR_OF_DAY, 0); c.set(Calendar.MINUTE, 0);
        c.set(Calendar.SECOND, 0); c.set(Calendar.MILLISECOND, 0);
        return c;
    }

    /** Monday-based local week number (same formula as server/handlers.py week_no) */
    static long weekNo() {
        long ms = System.currentTimeMillis();
        long epochDay = (ms + java.util.TimeZone.getDefault().getOffset(ms)) / 86400000L;   // positive, so / == floor
        return (epochDay + 3) / 7;
    }

    static long secsToMidnight() { return Math.max(60, midnight().getTimeInMillis() / 1000L - now()); }

    static String today() {
        Calendar c = Calendar.getInstance();
        return String.format(Locale.US, "%04d-%02d-%02d", c.get(Calendar.YEAR), c.get(Calendar.MONTH) + 1,
                c.get(Calendar.DAY_OF_MONTH));
    }

    /** Python str() */
    static String s(Object o) { return o == null ? "None" : String.valueOf(o); }

    static String esc(Object o) {
        return s(o).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("\"", "&quot;");
    }

    static String attrs(Map<String, Object> d) {
        StringBuilder b = new StringBuilder();
        for (Map.Entry<String, Object> e : d.entrySet()) {
            if (b.length() > 0) b.append(' ');
            b.append(e.getKey()).append("=\"").append(esc(e.getValue())).append('"');
        }
        return b.toString();
    }

    /** Python int() */
    static long num(Object o) {
        if (o instanceof Number) return ((Number) o).longValue();
        if (o instanceof Boolean) return ((Boolean) o) ? 1 : 0;
        if (o == null) throw new NumberFormatException("None");
        return Long.parseLong(o.toString().trim());
    }

    static boolean truthy(Object o) {
        if (o == null) return false;
        if (o instanceof Boolean) return (Boolean) o;
        if (o instanceof Number) return ((Number) o).doubleValue() != 0;
        if (o instanceof String) return ((String) o).length() > 0;
        if (o instanceof Map) return !((Map<?, ?>) o).isEmpty();
        if (o instanceof List) return !((List<?>) o).isEmpty();
        return true;
    }

    static Map<String, Object> M(Object o) { return (Map<String, Object>) o; }
    static List<Object> L(Object o) { return (List<Object>) o; }

    static Map<String, Object> map(Object... kv) {
        Map<String, Object> m = new LinkedHashMap<String, Object>();
        for (int i = 0; i < kv.length; i += 2) m.put((String) kv[i], kv[i + 1]);
        return m;
    }

    static Object setdef(Map<String, Object> m, String k, Object d) {
        if (!m.containsKey(k)) m.put(k, d);
        return m.get(k);
    }

    static Map<String, Object> sub(Map<String, Object> m, String k) {
        return M(setdef(m, k, new LinkedHashMap<String, Object>()));
    }

    static Object get(Map<String, ?> m, String k, Object d) { return m.containsKey(k) ? m.get(k) : d; }
    static String ga(Map<String, String> a, String k) { return a.get(k); }
    static String or(String x, String y) { return x == null || x.length() == 0 ? y : x; }

    static <V> TreeMap<String, V> sorted(Map<String, V> m) { return new TreeMap<String, V>(m); }

    private static void save() { Store.save(); }

    // ------------------------------------------------------------ player model
    static final Map<String, String> RES_FIELDS = new LinkedHashMap<String, String>();
    static {
        String[] r = {"11", "iap", "13", "gold", "14", "key", "15", "pcoin",
                "21", "hp_p", "22", "mp_p", "23", "cool_p", "24", "return_s", "25", "rebirth_s",
                "28", "change_op", "31", "tonic_2", "32", "tonic_3", "33", "tonic_4",
                "36", "enchant_2", "37", "enchant_3", "38", "enchant_4",
                "51", "unit_1_t", "52", "unit_2_t", "61", "parts_1_t", "62", "parts_2_t", "63", "armor_1_t", "64", "armor_2_t"};
        for (int i = 0; i < r.length; i += 2) RES_FIELDS.put(r[i], r[i + 1]);
    }
    /** Gold, leaf (iap), key and crown (pcoin): purchases are gone, so these are kept topped up. */
    static final String[] CURRENCIES = {"gold", "iap", "key", "pcoin"};
    static final long CURRENCY_FULL = 9000000;   // enough to max every stat and building, far from int32 limits

    static Map<String, Object> startRes() {
        return map("gold", CURRENCY_FULL, "iap", CURRENCY_FULL, "key", CURRENCY_FULL, "pcoin", CURRENCY_FULL,
                "hp_p", 3L, "mp_p", 3L, "cool_p", 1L, "return_s", 1L, "rebirth_s", 1L);
    }

    static final long STAMINA_MAX = 30, STAMINA_REGEN = 300;

    static String flagAct8(long chk) {
        switch ((int) chk) {
            case 1: return "tutorial";
            case 2: return "pvp";
            case 3: return "arena";
            case 4: return "challenge";
            case 5: return "superman";
            case 10: return "review";
            default: return null;
        }
    }

    static Map<String, Object> U(Map<String, String> g) { return Ux(get(g, "idx_id", "0")); }

    static Map<String, Object> Ux(Object idx) {
        Map<String, Object> u = Store.user(idx);
        if (u == null) return null;
        setdef(u, "res", startRes());
        setdef(u, "flags", map("tutorial", 1L, "pvp", 0L, "arena", 0L, "challenge", 0L, "superman", 0L, "review", 1L));
        setdef(u, "heroes", new LinkedHashMap<String, Object>());
        setdef(u, "hero_type", 1L);
        setdef(u, "units", new LinkedHashMap<String, Object>());
        setdef(u, "mount_units", new LinkedHashMap<String, Object>());
        setdef(u, "home", map("hp", 0L, "heal_hp", 0L));
        setdef(u, "tower", map("hp", 0L, "attack", 0L));
        setdef(u, "parts", new LinkedHashMap<String, Object>());
        setdef(u, "skins", new LinkedHashMap<String, Object>());
        setdef(u, "unit_book", new ArrayList<Object>());
        setdef(u, "parts_book", new ArrayList<Object>());
        setdef(u, "scripts", new ArrayList<Object>());
        setdef(u, "stamina", map("cnt", STAMINA_MAX, "t", now()));
        setdef(u, "clear_ticket", map("cnt", 0L));
        setdef(u, "quests", new LinkedHashMap<String, Object>());
        setdef(u, "achieve", new ArrayList<Object>());
        setdef(u, "inven", map("unit", 0L, "parts", 0L));
        setdef(u, "boxes", new LinkedHashMap<String, Object>());
        setdef(u, "next_id", 100001L);
        return u;
    }

    static long newId(Map<String, Object> u) {
        long i = num(u.get("next_id"));
        u.put("next_id", i + 1);
        return i;
    }

    static int curHeroType(Map<String, Object> u, Map<String, String> g) {
        long t = num(or(g.get("type"), "0"));
        return t >= 1 && t <= 3 ? (int) t : (int) num(get(u, "hero_type", 1L));
    }

    /** starter kit per hero type: (weapon, hat, hand) part indexes from parts_<hero>.xml, first skill */
    static final long[][] STARTER_PARTS = {null, {30000, 32000, 31000}, {40000, 42000, 41000}, {50000, 52000, 51000}};
    static final String[] STARTER_SKILL = {null, "stonefall", "icespike", "poisonshot"};

    static Map<String, Object> hero(Map<String, Object> u, int t) {
        String ts = String.valueOf(t);
        Map<String, Object> heroes = M(u.get("heroes"));
        if (!heroes.containsKey(ts)) {
            heroes.put(ts, map(
                    "info", map("league", 1L, "ldp", 0L, "l_rank", 0L, "area", 1L, "stage", 0L),
                    "status", map("hp", 0L, "mp", 0L, "attack", 0L, "armor", 0L, "speed", 0L, "a_speed", 0L),
                    "skill", new LinkedHashMap<String, Object>(), "skill_names", new LinkedHashMap<String, Object>(),
                    "parts", new LinkedHashMap<String, Object>(), "skin", map("skin_id", 0L, "level", 0L),
                    "stages", new LinkedHashMap<String, Object>(),
                    "power", 0L));
        }
        Map<String, Object> h = M(heroes.get(ts));
        if (!truthy(h.get("starter_given"))) {          // one-time starter kit (also migrates old saves)
            h.put("starter_given", 1L);
            Map<String, Object> inv = sub(M(u.get("parts")), ts);
            Map<String, Object> hp = M(h.get("parts"));
            for (int slot = 1; slot <= 3; slot++) {   // weapon, hat, hand
                if (!hp.containsKey(String.valueOf(slot))) {
                    long pid = newId(u);
                    inv.put(String.valueOf(pid), map("parts_id", pid, "name", STARTER_PARTS[t][slot - 1], "level", 1L, "exp", 0L));
                    hp.put(String.valueOf(slot), String.valueOf(pid));
                }
            }
            setdef(M(h.get("skill")), "11", 1L);
            setdef(M(h.get("skill_names")), "1", STARTER_SKILL[t]);
        }
        return h;
    }

    /** returns {cnt, r_t} */
    static long[] stamina(Map<String, Object> u) {
        Map<String, Object> s = M(u.get("stamina"));
        long cnt = num(s.get("cnt")), t = num(s.get("t"));
        if (cnt < STAMINA_MAX) {
            long d = now() - t;
            long gained = d > 0 ? d / STAMINA_REGEN : 0;
            if (gained > 0) {
                cnt = Math.min(STAMINA_MAX, cnt + gained);
                t += gained * STAMINA_REGEN;
                s.put("cnt", cnt); s.put("t", t);
            }
        } else {
            t = now(); s.put("t", t);
        }
        long rt = cnt >= STAMINA_MAX ? 0 : STAMINA_REGEN - (now() - t);
        return new long[]{cnt, rt};
    }

    // ------------------------------------------------------------ dispatch
    static String dispatch(Map<String, String> a, Map<String, String> g) {
        int act;
        try { act = Integer.parseInt(a.get("act")); } catch (Exception e) { return null; }
        switch (act) {
            // time / misc
            case 2: return "<date time=\"" + now() + "\"/>";
            case 3: {
                Calendar c = Calendar.getInstance();
                int d = c.get(Calendar.YEAR) * 10000 + (c.get(Calendar.MONTH) + 1) * 100 + c.get(Calendar.DAY_OF_MONTH);
                return "<date date=\"" + d + "\" week=\"" + (c.get(Calendar.DAY_OF_WEEK) - 1) + "\"/>";
            }
            case 4: return "<user gift=\"0\"/>";
            case 5: case 7: case 16: case 19: case 9: case 10: return "";
            case 8: return act8(a, g);
            // account
            case 20: {
                Map<String, Object> u = Store.newUser(s(get(a, "nick", "Player")), s(get(a, "sns_type", "0")), s(get(a, "id", "")));
                return "<idx_id>" + u.get("idx_id") + "</idx_id><idx_time>" + u.get("idx_time") + "</idx_time>";
            }
            case 24: {
                for (Object o : Store.users().values()) {
                    Map<String, Object> u = M(o);
                    if (truthy(a.get("id")) && a.get("id").equals(u.get("sns_id")))
                        return "<idx_id>" + u.get("idx_id") + "</idx_id><idx_time>" + u.get("idx_time") + "</idx_time>";
                }
                return "";
            }
            case 21: {
                Map<String, Object> u = U(g);
                if (u != null) { int t = curHeroType(u, g); hero(u, t); u.put("hero_type", (long) t); save(); }
                return "";
            }
            case 6: {
                Map<String, Object> u = U(g);
                if (u != null) { int t = (int) num(get(a, "hero_type", 1L)); hero(u, t); u.put("hero_type", (long) t); save(); }
                return "";
            }
            case 22: return act22(a, g);
            case 23: case 30: case 31: return "";
            case 29: return "<change code=\"OFFLINE\" l_t=\"600\"/>";
            // resources
            case 89: { Map<String, Object> u = U(g); return u != null ? resXml(u) : ""; }
            case 15: return "<item_list><item type=\"26\" max=\"" + STAMINA_MAX + "\"/></item_list>";
            case 87: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                long v = changeRes(u, get(a, "money_type", 13L), get(a, "money_chk", 0L), get(a, "money_cnt", 0L)); save();
                return "<money type=\"" + s(a.get("money_type")) + "\" count=\"" + v + "\"/>";
            }
            case 88: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                long v = changeRes(u, get(a, "item_type", 0L), get(a, "item_chk", 0L), get(a, "item_cnt", 0L)); save();
                return "<item type=\"" + s(a.get("item_type")) + "\" count=\"" + v + "\"/>";
            }
            case 59: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                long[] st = stamina(u);
                return "<stamina cnt=\"" + st[0] + "\" r_t=\"" + st[1] + "\" max_cnt=\"" + STAMINA_MAX + "\" re_t=\"" + STAMINA_REGEN + "\"/>";
            }
            case 58: return act58(a, g);
            case 133: {
                Map<String, Object> u = U(g);
                Object c = u != null ? M(u.get("clear_ticket")).get("cnt") : 0L;
                return "<clear_t r_t=\"" + secsToMidnight() + "\" cnt=\"" + s(c) + "\" max_cnt=\"5\" add_cnt=\"0\" bpc_cnt=\"0\" add_max_cnt=\"0\" max_r_t=\"86400\"/>";
            }
            case 134: {
                Map<String, Object> u = U(g);
                if (u != null) {
                    long n = num(get(a, "cnt", 0L));
                    Map<String, Object> ct = M(u.get("clear_ticket"));
                    long c = num(ct.get("cnt"));
                    ct.put("cnt", num(get(a, "chk", 0L)) != 0 ? c + n : Math.max(0, c - n)); save();
                }
                return "";
            }
            case 135: return "";
            case 138: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                String kind = "1".equals(s(a.get("sort"))) ? "unit" : "parts";
                Map<String, Object> inv = M(u.get("inven"));
                inv.put(kind, num(get(inv, kind, 0L)) + 5); save();
                return "<inven sort=\"" + s(a.get("sort")) + "\" add_cnt=\"" + inv.get(kind) + "\"/>";
            }
            // hero
            case 85: return act85(a, g);
            case 82: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                Map<String, Object> st = M(hero(u, curHeroType(u, g)).get("status"));
                for (String k : new ArrayList<String>(st.keySet())) if (a.containsKey(k)) st.put(k, num(a.get(k)));
                save();
                return "<status " + attrs(st) + "/>";
            }
            case 83: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                Map<String, Object> sk = M(hero(u, curHeroType(u, g)).get("skill"));
                long n = num(get(a, "skill", 0L));
                String key = String.valueOf(n);
                long lv = num(get(sk, key, 0L)) + 1;
                sk.put(key, lv); save();
                return "<hero skill=\"" + n + "\" lv=\"" + lv + "\"/>";
            }
            case 90: return act90(a, g);
            case 91: return "";
            // buildings / units
            case 81: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                String[][] f = {{"home_hp", "home", "hp"}, {"home_heal_hp", "home", "heal_hp"},
                        {"tower_hp", "tower", "hp"}, {"tower_attack", "tower", "attack"}};
                for (String[] x : f) if (a.containsKey(x[0])) M(u.get(x[1])).put(x[2], num(a.get(x[0])));
                save();
                return "<home " + attrs(M(u.get("home"))) + "/><tower " + attrs(M(u.get("tower"))) + "/>";
            }
            case 94: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                StringBuilder b = new StringBuilder();
                for (Object un : M(u.get("units")).values()) b.append("<unit ").append(attrs(M(un))).append("/>");
                return "<unit_list add_cnt=\"" + s(get(M(u.get("inven")), "unit", 0L)) + "\" bpc_add_cnt=\"0\" max_add_cnt=\"100\">" + b + "</unit_list>";
            }
            case 98: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                long i = newId(u);
                Map<String, Object> un = map("unit_id", i);
                for (String k : UNIT_KEYS) un.put(k, get(a, k, 0L));
                M(u.get("units")).put(String.valueOf(i), un);
                List<Object> book = L(u.get("unit_book"));
                if (!book.contains(un.get("name"))) book.add(un.get("name"));
                save();
                return "<unit idx=\"" + s(get(a, "idx", 0L)) + "\" id=\"" + i + "\"/>";
            }
            case 99: {
                Map<String, Object> u = U(g);
                if (u != null && M(u.get("units")).containsKey(s(a.get("unit_id")))) {
                    Map<String, Object> un = M(M(u.get("units")).get(s(a.get("unit_id"))));
                    for (String k : UNIT_KEYS) if (a.containsKey(k)) un.put(k, a.get(k));
                    save();
                }
                return "";
            }
            case 100: {
                Map<String, Object> u = U(g);
                if (u != null) {
                    String uid = s(a.get("unit_id"));
                    M(u.get("units")).remove(uid);
                    Map<String, Object> mu = M(u.get("mount_units"));
                    for (String k : new ArrayList<String>(mu.keySet())) if (s(mu.get(k)).equals(uid)) mu.remove(k);
                    save();
                }
                return "";
            }
            case 84: {
                Map<String, Object> u = U(g);
                if (u != null) {
                    Map<String, Object> mu = M(u.get("mount_units"));
                    for (Map.Entry<String, String> e : a.entrySet()) {
                        if (!e.getKey().startsWith("unit_")) continue;
                        String slot = e.getKey().substring(5);
                        if (e.getValue().equals("0") || e.getValue().length() == 0) mu.remove(slot);
                        else mu.put(slot, e.getValue());
                    }
                    save();
                }
                return "";
            }
            case 86: {
                Map<String, Object> u = Ux(or(a.get("f_idx_id"), g.get("idx_id")));
                if (u == null) return "";
                StringBuilder b = new StringBuilder();
                Map<String, Object> units = M(u.get("units"));
                for (Map.Entry<String, Object> e : sorted(M(u.get("mount_units"))).entrySet()) {
                    Object un = units.get(s(e.getValue()));
                    if (un != null) b.append("<unit_").append(e.getKey()).append(' ').append(attrs(M(un))).append("/>");
                }
                return "<user idx_id=\"" + u.get("idx_id") + "\"><home " + attrs(M(u.get("home"))) + "/><tower "
                        + attrs(M(u.get("tower"))) + "/><unit_list>" + b + "</unit_list></user>";
            }
            case 74: {
                Map<String, Object> u = U(g);
                StringBuilder b = new StringBuilder();
                if (u != null) for (Object n : L(u.get("unit_book"))) b.append("<book name=\"").append(esc(n)).append("\"/>");
                return "<book_list>" + b + "</book_list>";
            }
            case 92: {
                Map<String, Object> u = U(g);
                StringBuilder b = new StringBuilder();
                if (u != null) for (Object n : L(u.get("parts_book"))) b.append("<p_book name=\"").append(esc(n)).append("\"/>");
                return "<p_book_list>" + b + "</p_book_list>";
            }
            // parts / skins
            case 93: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                StringBuilder b = new StringBuilder();
                for (Object p : partsOf(u, g).values()) b.append("<parts ").append(attrs(M(p))).append("/>");
                return "<parts_list add_cnt=\"" + s(get(M(u.get("inven")), "parts", 0L)) + "\" bpc_add_cnt=\"0\" max_add_cnt=\"100\">" + b + "</parts_list>";
            }
            case 95: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                long i = newId(u);
                Map<String, Object> p = map("parts_id", i);
                for (Map.Entry<String, String> e : a.entrySet()) {
                    String k = e.getKey();
                    if (!k.equals("act") && !k.equals("idx") && !k.equals("link")) p.put(k, e.getValue());
                }
                partsOf(u, g).put(String.valueOf(i), p);
                List<Object> book = L(u.get("parts_book"));
                if (truthy(p.get("name")) && !book.contains(p.get("name"))) book.add(p.get("name"));
                save();
                return "<parts idx=\"" + s(get(a, "idx", 0L)) + "\" id=\"" + i + "\"/>";
            }
            case 96: {
                Map<String, Object> u = U(g);
                if (u != null) {
                    Object po = partsOf(u, g).get(s(a.get("parts_id")));
                    if (po != null) {
                        Map<String, Object> p = M(po);
                        for (Map.Entry<String, String> e : a.entrySet()) {
                            String k = e.getKey();
                            if (!k.equals("act") && !k.equals("link") && !k.equals("parts_id")) p.put(k, e.getValue());
                        }
                        save();
                    }
                }
                return "";
            }
            case 97: {
                Map<String, Object> u = U(g);
                if (u != null) {
                    String pid = s(a.get("parts_id"));
                    partsOf(u, g).remove(pid);
                    Map<String, Object> hp = M(hero(u, curHeroType(u, g)).get("parts"));
                    for (String k : new ArrayList<String>(hp.keySet())) if (s(hp.get(k)).equals(pid)) hp.remove(k);
                    save();
                }
                return "";
            }
            case 130: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                Object sk = M(u.get("skins")).get(String.valueOf(curHeroType(u, g)));
                StringBuilder b = new StringBuilder("<skin_list>");
                if (sk != null) for (Object x : M(sk).values()) b.append("<skin ").append(attrs(M(x))).append("/>");
                return b.append("</skin_list>").toString();
            }
            case 131: {
                Map<String, Object> u = U(g);
                if (u != null) {
                    Map<String, Object> sk = sub(M(u.get("skins")), String.valueOf(curHeroType(u, g)));
                    sk.put(s(a.get("skin_id")), map("skin_id", a.get("skin_id"), "level", get(a, "level", 1L), "e_cnt", 0L));
                    save();
                }
                return "";
            }
            case 132: {
                Map<String, Object> u = U(g);
                if (u != null) {
                    Map<String, Object> sk = sub(M(u.get("skins")), String.valueOf(curHeroType(u, g)));
                    Map<String, Object> x = M(setdef(sk, s(a.get("skin_id")), map("skin_id", a.get("skin_id"))));
                    x.put("level", get(a, "level", get(x, "level", 1L)));
                    x.put("e_cnt", get(a, "e_cnt", 0L));
                    Map<String, Object> skin = M(hero(u, curHeroType(u, g)).get("skin"));
                    if (s(skin.get("skin_id")).equals(s(a.get("skin_id")))) skin.put("level", num(x.get("level")));
                    save();
                }
                return "";
            }
            // stages
            case 61: return "<stage log_idx=\"" + now() + "\" log_key=\"offline\"/>";
            case 60: return act60(a, g);
            case 62: return act62(a, g);
            case 57: {
                Map<String, Object> u = U(g);
                if (u != null) {
                    Map<String, Object> h = hero(u, curHeroType(u, g));
                    sub(sub(h, "star_rwd"), s(get(a, "area", 1L))).put(s(get(a, "rwd_type", 1L)), 1L);
                    save();
                }
                return "";
            }
            // scripts / quests / achievements
            case 70: {
                Map<String, Object> u = U(g);
                if (u != null && truthy(a.get("script_id")) && !L(u.get("scripts")).contains(a.get("script_id"))) {
                    L(u.get("scripts")).add(a.get("script_id")); save();
                }
                return "";
            }
            case 71: {
                Map<String, Object> u = U(g);
                StringBuilder b = new StringBuilder("<script>");
                if (u != null) for (Object x : L(u.get("scripts"))) b.append("<s>").append(esc(x)).append("</s>");
                return b.append("</script>").toString();
            }
            case 76: {
                Map<String, Object> u = U(g);
                if (u != null) {
                    String key = s(a.get("type")) + "-" + s(a.get("id"));
                    Map<String, Object> q = M(setdef(M(u.get("quests")), key,
                            map("type", a.get("type"), "id", a.get("id"), "cnt", 0L, "chk", 0L)));
                    q.put("chk", 1L); q.put("day", today()); save();
                }
                return "";
            }
            case 77: case 75: {
                Map<String, Object> u = U(g);
                String td = today();
                StringBuilder b = new StringBuilder();
                if (u != null) {
                    for (Object o : M(u.get("quests")).values()) {
                        Map<String, Object> q = M(o);
                        Object ty = q.get("type");
                        boolean daily = "1".equals(ty) || (ty instanceof Number && ((Number) ty).longValue() == 1);
                        if (daily && !td.equals(q.get("day"))) continue;
                        b.append("<quest type=\"").append(s(q.get("type"))).append("\" id=\"").append(s(q.get("id")))
                                .append("\" cnt=\"").append(s(get(q, "cnt", 0L))).append("\" chk=\"").append(s(get(q, "chk", 0L))).append("\"/>");
                    }
                }
                return "<quest_list>" + b + "</quest_list>";
            }
            case 79: {
                Map<String, Object> u = U(g);
                if (u != null && truthy(a.get("id")) && !L(u.get("achieve")).contains(a.get("id"))) {
                    L(u.get("achieve")).add(a.get("id")); save();
                }
                return "";
            }
            case 78: {
                Map<String, Object> u = U(g);
                if (u != null && truthy(a.get("type"))) {
                    Map<String, Object> ar = sub(u, "a_r");
                    ar.put(a.get("type"), num(get(ar, a.get("type"), 0L)) + num(or(a.get("chg"), "0"))); save();
                }
                return "";
            }
            case 80: {
                Map<String, Object> u = U(g);
                StringBuilder b = new StringBuilder();
                if (u != null) for (Object x : L(u.get("achieve"))) b.append("<a_c id=\"").append(esc(x)).append("\"/>");
                String ar = u != null ? attrs(M(get(u, "a_r", new LinkedHashMap<String, Object>()))) : "";
                return "<a_r " + ar + "/><a_c_list>" + b + "</a_c_list>";
            }
            // daily / boxes / events / gifts
            case 120: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                Map<String, Object> tr = M(setdef(u, "today", map("cnt", 0L, "day", "")));
                int chk = today().equals(tr.get("day")) ? 1 : 0;
                return "<tdr cnt=\"" + s(tr.get("cnt")) + "\" chk=\"" + chk + "\" r_t=\"" + (midnight().getTimeInMillis() / 1000L - now()) + "\"/>";
            }
            case 72: {
                Map<String, Object> u = U(g);
                if (u != null) {
                    Map<String, Object> tr = M(setdef(u, "today", map("cnt", 0L, "day", "")));
                    String td = today();
                    if (!td.equals(tr.get("day"))) {
                        tr.put("day", td);
                        tr.put("cnt", a.containsKey("cnt") ? num(a.get("cnt")) : num(tr.get("cnt")) + 1);
                    }
                    save();
                }
                return "";
            }
            case 125: { Map<String, Object> u = U(g); return u != null ? boxesXml(u) : ""; }
            case 126: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                String name = boxType(s(a.get("box_type")));
                if (name != null) { M(u.get("boxes")).put(name, now()); save(); }
                return boxesXml(u);
            }
            case 122: return "<event_list></event_list>";
            case 32: return "<month_iap_list chk=\"0\" r_t=\"" + secsToMidnight() + "\"></month_iap_list>";
            case 34: return "<premium_iap_list max=\"0\" r_t=\"" + secsToMidnight() + "\"></premium_iap_list>";
            case 51: return "<key_list></key_list>";
            case 54: return "<gift_list></gift_list>";
            case 50: case 55: case 56: case 139: case 33: case 37: case 38: return "";
            case 124: return "<coupon code=\"0\" msg=\"Coupons are not available offline.\"/>";
            // friends (offline: nobody)
            case 40: return "<chksum>0</chksum>";
            case 41: case 42: case 43: case 44: case 63: return "";
            case 45: case 46: case 47: case 48: case 49: return "<frd_list></frd_list>";
            case 150: return "<mission_list></mission_list>";
            // pvp / arena / challenge / super (vs. bots)
            case 102: return "<arena r_t=\"" + secsToMidnight() + "\" win_cnt=\"0\"/><win_list></win_list>";
            case 110: return "<revenge_list></revenge_list>";
            case 64: return logTag("pvp");
            case 103: return logTag("arena");
            case 107: return logTag("challenge");
            case 115: return logTag("superman");
            case 111: return logTag("revenge");
            case 65: {   // LoadPvPRank; the weekly "League Result" popup only once a week, after playing
                Map<String, Object> u = U(g);
                Map<String, Object> p = u != null ? pvp(u) : map("league", 1L, "score", 0L, "win_cnt", 0L, "match_cnt", 0L);
                String x = "<pvp_rank league=\"" + s(p.get("league")) + "\" total=\"1\" rank=\"1\" score=\"" + s(p.get("score"))
                        + "\" ex_ud=\"0\" enter_rwd=\"0\" win_cnt=\"" + s(p.get("win_cnt")) + "\" match_cnt=\"" + s(p.get("match_cnt"))
                        + "\" l_t=\"" + secsToMidnight() + "\"/>";
                if (u != null && (p.get("reward_week") == null || num(p.get("reward_week")) != weekNo())) {
                    if (p.get("reward_week") != null && num(p.get("match_cnt")) > 0)
                        x += "<pvp_reward league=\"" + s(p.get("league")) + "\" total=\"1\" rank=\"1\" score=\"" + s(p.get("score")) + "\"/>";
                    p.put("reward_week", weekNo()); save();
                }
                return x;
            }
            case 66: {
                Map<String, Object> u = U(g);
                if (u == null) return "";
                Map<String, Object> p = pvp(u);
                p.put("match_cnt", num(p.get("match_cnt")) + 1);
                if ("2".equals(s(a.get("result")))) p.put("win_cnt", num(p.get("win_cnt")) + 1);
                p.put("score", Math.max(0, num(p.get("score")) + num(or(a.get("score"), "0")))); save();
                return "<pvp rank=\"1\" score=\"" + s(p.get("score")) + "\" win_cnt=\"" + s(p.get("win_cnt")) + "\"/>";
            }
            case 67: return "";
            case 68: return "<char_list></char_list>";
            case 101: return "<arena idx_id=\"0\"/>";
            case 104: case 108: case 113: case 116: case 73: case 112: case 121: case 137: return "";
            case 136: {
                Map<String, Object> u = U(g);
                if (u != null && truthy(a.get("league"))) { pvp(u).put("league", num(a.get("league"))); save(); }
                return "";
            }
            case 106: return "<challenge b_w=\"0\" cnt=\"0\" max_cnt=\"3\" r_t=\"" + secsToMidnight() + "\"/>";
            case 114: return "<superman cnt=\"0\" max_cnt=\"5\" r_t=\"" + secsToMidnight() + "\"/>";
            default: return null;
        }
    }

    static final String[] UNIT_KEYS = {"name", "max_grade", "grade", "level", "exp"};

    static Map<String, Object> partsOf(Map<String, Object> u, Map<String, String> g) {
        return sub(M(u.get("parts")), String.valueOf(curHeroType(u, g)));
    }

    static String logTag(String tag) { return "<" + tag + " log_idx=\"" + now() + "\" log_key=\"offline\"/>"; }

    static Map<String, Object> pvp(Map<String, Object> u) {
        return M(setdef(u, "pvp", map("league", 1L, "score", 0L, "win_cnt", 0L, "match_cnt", 0L)));
    }

    static final String[] BOXES = {"luckybox", "s_partsbox", "s_armorbox", "s_unitbox"};
    static final long[] BOX_CD = {4 * 3600, 24 * 3600, 24 * 3600, 24 * 3600};

    static String boxType(String t) {
        if (t.equals("1")) return "luckybox";
        if (t.equals("2")) return "s_partsbox";
        if (t.equals("3")) return "s_armorbox";
        if (t.equals("4")) return "s_unitbox";
        return null;
    }

    static String boxesXml(Map<String, Object> u) {
        StringBuilder b = new StringBuilder();
        Map<String, Object> bx = M(u.get("boxes"));
        for (int i = 0; i < BOXES.length; i++) {
            long last = num(get(bx, BOXES[i], 0L));
            b.append('<').append(BOXES[i]).append(" time=\"").append(Math.max(0, last + BOX_CD[i] - now())).append("\"/>");
        }
        return b.toString();
    }

    static String resXml(Map<String, Object> u) {
        Map<String, Object> r = M(u.get("res"));
        StringBuilder b = new StringBuilder();
        for (String f : RES_FIELDS.values()) b.append('<').append(f).append('>').append(num(get(r, f, 0L))).append("</").append(f).append('>');
        return b.toString();
    }

    static long changeRes(Map<String, Object> u, Object typ0, Object chk, Object cnt0) {
        long typ = num(typ0), cnt = num(cnt0);
        boolean add = num(chk) != 0;
        if (typ == 26) {
            long c = stamina(u)[0];
            long v = Math.max(0, add ? c + cnt : c - cnt);
            M(u.get("stamina")).put("cnt", v);
            return v;
        }
        String f = RES_FIELDS.get(String.valueOf(typ));
        if (f == null) f = "item_" + typ;
        Map<String, Object> r = M(u.get("res"));
        long v = num(get(r, f, 0L)) + (add ? cnt : -cnt);
        r.put(f, Math.max(0, v));
        return Math.max(0, v);
    }

    static String act8(Map<String, String> a, Map<String, String> g) {
        Map<String, Object> u = U(g);
        String f = flagAct8(num(get(a, "chk", 0L)));
        if (u != null && f != null) {
            M(u.get("flags")).put(f, f.equals("tutorial") || f.equals("review") ? 0L : 1L);
            save();
        }
        return "";
    }

    static String act22(Map<String, String> a, Map<String, String> g) {
        Map<String, Object> u = Ux(get(a, "idx_id", g.get("idx_id")));
        if (u == null) return "<block chk=\"3\"/>";
        Map<String, Object> fl = M(u.get("flags"));
        StringBuilder x = new StringBuilder();
        x.append("<skey>").append(esc(u.get("skey"))).append("</skey><nick>").append(esc(u.get("nick"))).append("</nick><sns_list></sns_list>");
        for (String k : new String[]{"tutorial", "pvp", "arena", "challenge", "superman"})
            x.append('<').append(k).append('>').append(s(get(fl, k, 0L))).append("</").append(k).append('>');
        x.append("<superman_chk>1</superman_chk><crm_chk>0</crm_chk><review>").append(s(get(fl, "review", 0L))).append("</review>");
        x.append("<last_hero>").append(truthy(u.get("heroes")) ? s(u.get("hero_type")) : "0").append("</last_hero>");
        x.append("<alarm>3</alarm><pvp_result>0</pvp_result><win_restore>0</win_restore>");
        Map<String, Object> res = M(u.get("res"));            // purchases are gone: refill currencies every login
        for (String c : CURRENCIES) if (num(get(res, c, 0L)) < CURRENCY_FULL) res.put(c, CURRENCY_FULL);
        u.put("last_login", now());
        save();
        return x.toString();
    }

    static String act58(Map<String, String> a, Map<String, String> g) {
        Map<String, Object> u = U(g);
        if (u == null) return "";
        long c = stamina(u)[0];
        long n = num(get(a, "cnt", 0L));
        c = num(get(a, "chk", 0L)) != 0 ? c + n : Math.max(0, c - n);
        Map<String, Object> st = M(u.get("stamina"));
        if (num(st.get("cnt")) >= STAMINA_MAX && c < STAMINA_MAX) st.put("t", now());
        st.put("cnt", c);
        long rt = stamina(u)[1];
        save();
        return "<stamina cnt=\"" + c + "\" r_t=\"" + rt + "\"/>";
    }

    static String heroXml(Map<String, Object> u, int t) {
        Map<String, Object> h = hero(u, t);
        StringBuilder x = new StringBuilder();
        x.append("<info ").append(attrs(M(h.get("info")))).append("/>");
        x.append("<status ").append(attrs(M(h.get("status")))).append("/>");
        x.append("<skill ").append(attrs(M(h.get("skill")))).append("/>");
        for (Map.Entry<String, Object> e : sorted(M(h.get("skill_names"))).entrySet())
            x.append("<skill_").append(e.getKey()).append(" name=\"").append(esc(e.getValue())).append("\"/>");
        Object partsT = M(u.get("parts")).get(String.valueOf(t));
        Map<String, Object> parts = partsT != null ? M(partsT) : new LinkedHashMap<String, Object>();
        for (Map.Entry<String, Object> e : sorted(M(h.get("parts"))).entrySet()) {
            Object p = parts.get(s(e.getValue()));
            if (p != null) x.append("<parts_").append(e.getKey()).append(' ').append(attrs(M(p))).append("/>");
        }
        x.append("<skin ").append(attrs(M(h.get("skin")))).append("/>");
        return x.toString();
    }

    static String act85(Map<String, String> a, Map<String, String> g) {
        Map<String, Object> u = Ux(or(a.get("f_idx_id"), g.get("idx_id")));
        if (u == null) return "";
        int ft = (int) num(or(a.get("f_type"), "0"));
        int[] types = ft != 0 ? new int[]{ft} : new int[]{1, 2, 3};   // every account owns all three heroes
        StringBuilder b = new StringBuilder();
        for (int t : types) b.append("<hero type=\"").append(t).append("\">").append(heroXml(u, t)).append("</hero>");
        save();
        return "<user idx_id=\"" + u.get("idx_id") + "\" nick=\"" + esc(u.get("nick")) + "\">" + b + "</user>";
    }

    static String act90(Map<String, String> a, Map<String, String> g) {
        Map<String, Object> u = U(g);
        if (u == null) return "";
        int t = curHeroType(u, g);
        Map<String, Object> h = hero(u, t);
        for (Map.Entry<String, String> e : a.entrySet()) {
            String k = e.getKey(), v = e.getValue();
            if (k.startsWith("parts_")) {
                String slot = k.substring(6);
                if (v.equals("0") || v.length() == 0) M(h.get("parts")).remove(slot);
                else M(h.get("parts")).put(slot, v);
            } else if (k.startsWith("skill_")) {
                M(h.get("skill_names")).put(k.substring(6), v);
            } else if (k.equals("skin_id")) {
                Map<String, Object> skin = M(h.get("skin"));
                skin.put("skin_id", num(v));
                Object skT = M(u.get("skins")).get(String.valueOf(t));
                Object sk = skT != null ? M(skT).get(v) : null;
                skin.put("level", truthy(sk) ? num(get(M(sk), "level", 0L)) : 0L);
            } else if (k.equals("power")) {
                h.put("power", num(v));
            }
        }
        save();
        return "";
    }

    static String act60(Map<String, String> a, Map<String, String> g) {
        Map<String, Object> u = U(g);
        long result = num(get(a, "result", 0L));
        if (u != null && (result == 2 || result == 4)) {   // 2 = cleared, 4 = sweep (clear ticket), 3 = quit
            Map<String, Object> h = hero(u, curHeroType(u, g));
            long area = num(get(a, "area", 1L)), st = num(get(a, "stage", 1L)), star = num(get(a, "star", 0L));
            String key = area + "-" + st;
            Map<String, Object> stages = M(h.get("stages"));
            stages.put(key, Math.max(num(get(stages, key, 0L)), star));
            Map<String, Object> inf = M(h.get("info"));
            long ia = num(inf.get("area")), is = num(inf.get("stage"));
            if (area > ia || (area == ia && st > is)) { inf.put("area", area); inf.put("stage", st); }
            save();
        }
        return "";
    }

    static String act62(Map<String, String> a, Map<String, String> g) {
        Map<String, Object> u = U(g);
        long area = num(get(a, "area", 1L));
        StringBuilder body = new StringBuilder();
        Map<String, Object> rw = new LinkedHashMap<String, Object>();
        if (u != null) {
            Map<String, Object> h = hero(u, curHeroType(u, g));
            Object r = sub(h, "star_rwd").get(String.valueOf(area));
            if (r != null) rw = M(r);
            for (Map.Entry<String, Object> e : sorted(M(h.get("stages"))).entrySet()) {
                String[] p = e.getKey().split("-");
                if (Long.parseLong(p[0]) == area)
                    body.append("<stage stage=\"").append(Long.parseLong(p[1])).append("\" star=\"").append(s(e.getValue())).append("\"/>");
            }
        }
        StringBuilder r = new StringBuilder();
        for (int i = 1; i <= 4; i++) {
            if (i > 1) r.append(' ');
            r.append("rwd_").append(i).append("=\"").append(s(get(rw, String.valueOf(i), 0L))).append('"');
        }
        return "<stage_list area=\"" + area + "\" " + r + ">" + body + "</stage_list>";
    }
}
