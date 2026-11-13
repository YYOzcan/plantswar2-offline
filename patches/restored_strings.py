"""Text ids the 1.0.6 client asks StringManager::GetString for that no bundled string table defines.

They used to arrive with the (now dead) update server's string patches.  Three kinds:
  ALIAS    -> copy every language column of an existing id (same thing under another name)
  DERIVED  -> computed from the original tables (elite bosses, ticket names, building variants)
  TEXT     -> written here (English in every language column)
Found with scripts/scan_getstring.py (literal ids at GetString call sites), scripts/text_audit_data.py
(data-table columns holding text ids) and the PW2Text logcat tag of the patched miss branch.
"""
import re

LANGS = ("kor", "eng", "chn_sc", "chn_tw", "jpn", "tha")

ALIAS = {
    # reward / item types used in achievement, quest, star, today and package tables
    "iap": "leaf", "cash": "leaf",
    "high_armor_ticket": "armor_high_ticket", "high_weapon_ticket": "weapon_high_ticket",
    "high_unit_ticket": "unit_high_ticket", "normal_unit_ticket": "unit_low_ticket",
    "normal_weapon_ticket": "weapon_low_ticket", "normal_armor_ticket": "armor_low_ticket",
    "special_armor_ticket": "armor_special_ticket", "special_weapon_ticket": "weapon_special_ticket",
    "special_unit_ticket": "unit_special_ticket", "set_armor_ticket": "armor_set_ticket",
    "set_weapon_ticket": "weapon_set_ticket",
    "n_cash": "n_leaf", "n_iap": "n_leaf",
    # league reward boxes / league ids
    "bronze": "league_bronze", "silver": "league_silver", "platinum": "league_platinum",
    # quest modes
    "arena": "arena_mode", "challenge": "challenge_mode", "super": "superman_mode",
    # part types
    "weapon": "parts_type_weapon", "hat": "parts_type_hat", "hand": "parts_type_hand",
    # stats
    "speed": "move_speed",
    "home": "home_upgrade", "tower": "tower_upgrade",
    # game-over tip shortcuts (<scene>_info is the button label)
    "pvp_info": "pvp_mode", "stats_info": "stats_upgrade_title",
    # mode titles
    "superman_title": "superman_mode", "superman_title_shadow": "superman_mode",
    "superman_lock_title": "superman_mode",
}

TEXT = {
    # ---- literal ids at GetString call sites
    "already_have_friend": "This player is already on your friend list.",
    "block_forever": "This account has been suspended.",
    "cost_bonus": "Bonus",
    "facebook_invite_title": "Invite Friends",
    "free_open_time_error": "The free chest is not ready yet.",
    "friend_list_title": "Friends",
    "request_list_title": "Friend Requests",
    "search_friend_have_facebook": "Find Friends",
    "grade_skin_iap": "Premium Skin",
    "input_device_link_code": "Enter Code",
    "logout_google": "Sign Out",
    "next_level": "Next Level",
    "pre_level": "Current Level",
    "before_level": "Current Level",
    "shop_info": "Shop",
    "armor": "Defense",
    "move_speed": "Movement Speed",
    "policy_url_global": "https://www.google.com/search?q=Plants+War+2",
    "armor_ticket": "Gear Ticket", "weapon_ticket": "Weapon Ticket",
    "parts_weapon_6_set_select_ticket": "6 Star Set Weapon Selection Ticket",
    "parts_armor_6_set_select_ticket": "6 Star Set Gear Selection Ticket",
    "parts_weapon_6_select_ticket": "6 Star Weapon Selection Ticket",
    "parts_armor_6_select_ticket": "6 Star Gear Selection Ticket",
    "n_unit": "Minion x[a]", "n_parts": "Gear x[a]", "n_item": "Item x[a]", "n_giftbox": "Lucky Chest x[a]",
    # ---- internal skills/weapons (names are looked up while loading skill data)
    "enemy_body": "Body Attack", "boss_body": "Boss Attack", "fighter_body": "Body Attack", "unit_body": "Body Attack",
    "playertower": "Tower Shot", "enemytower": "Tower Shot", "flyingmarianne": "Flying Marianne",
    "commonunit": "Attack", "team_heal": "Team Heal", "flametree_buff": "Flame Tree Aura",
    "thornprotecter_buff": "Thorn Protector Aura",
    # ---- skill side effects (attribute types)
    "poison": "Poison", "poison_info": "Poisons the target, dealing damage over time.",
    "poison_detail": "Damage over time",
    "stun": "Stun", "stun_info": "Stuns the target so it cannot move or attack.",
    "stun_detail": "Stun",
    "slow": "Slow", "slow_info": "Slows down the target's movement.", "slow_detail": "Movement speed decreased",
    "speed_move": "Haste", "speed_move_info": "Increases movement speed.", "speed_move_detail": "Movement speed increased",
    "vampire": "Life Drain", "vampire_info": "Restores HP based on the damage dealt.",
    "vampire_detail": "HP absorbed from damage",
    # ---- enemies without an encyclopedia entry
    "woodpecker_info": "Woodpeckers peck through anything in their way, Towers included. Keep them away from your Sanctuary!",
    "kangaroo_info": "Kangaroos hop into battle with powerful kicks. Don't let their bouncy steps fool you.",
    "wolf_info": "Wolves hunt in packs and strike fast. Never fight them alone!",
    "anteater_info": "Ant Eaters are slow but sturdy. Their long claws can tear through your minions' defense.",
    "blackbear_info": "Black Bears are strong and stubborn. They will not stop until the forest is theirs.",
    "capybara_info": "Capybaras look calm, but they throw rocks from a distance with surprising accuracy.",
    "eagle_info": "Eagles attack from the sky with sharp feathers. Take them down before they reach your Tower.",
    "vampirebat_info": "Vampire Bats drain the life of their targets from afar. Defeat them quickly!",
    "matamataturtle_info": "Matamata Turtles hide in their shells and spit water at the enemy from behind the front line.",
    "meerkat_info": "Meerkats always keep watch and throw pebbles at anyone who gets too close.",
    "hippo_info": "Hippos charge straight at buildings. Their huge bodies can crush a Tower in no time.",
    "buffalo_info": "Buffaloes ram everything in their path. Protect your Towers from their charge!",
    "gorilla_info": "Gorillas pound buildings with their mighty fists. Stop them before they reach your Sanctuary.",
    "wildpanda_info": "Wild Pandas are much stronger than they look. They specialize in tearing down buildings.",
    "crocodile_info": "Crocodiles lurk near the front line and snap at Towers with their powerful jaws.",
    "skunk_info": "Skunks support their allies with a terrible smell that weakens nearby plants.",
    "platypus_info": "Platypuses heal and support the animals around them. Defeat them first!",
    "chipmunk_info": "Squirrels gather supplies for the animal army and help their allies keep fighting.",
    "bat_info": "Bats flutter around the battlefield and support the animals with sonic waves.",
    "yeti_info": "The Yeti of the Frigid Forest strengthens the animals around it with freezing winds.",
    "bear": "Bear", "bear_info": "A huge bear guarding its territory. Approach with caution!",
    "leshy": "Leshy", "leshy_info": "Leshy, the trickster spirit of the woods, has sided with the animals.",
    "fenrir": "Fenrir", "fenrir_info": "Fenrir, the legendary wolf, leads the wild pack into battle.",
    "shade": "Shade", "shade_info": "A shadowy creature born from the Tainted Forest. It strikes from the dark.",
    "gru": "Gru", "gru_info": "Gru is a fearsome brute that smashes everything with brute force.",
    "statue_1": "Ancient Statue", "statue_2": "Ancient Statue", "statue_3": "Ancient Statue", "statue_4": "Ancient Statue",
    "house_01": "House", "house_02": "House", "totem": "Totem",
    "wall_01": "Wall", "wall_02": "Wall", "wall_03": "Wall",
    "player_tower_info": "Your Tower attacks enemies that come near. Upgrade it to make it stronger.",
    "enemy_tower_info": "The enemy Tower attacks anything that comes near. Destroy it to advance.",
    "player_home_info": "Your Sanctuary. If it is destroyed, you lose the battle!",
    "enemy_home_info": "The enemy Sanctuary. Destroy it to win the battle!",
    "home_info": "Upgrade the Sanctuary's HP and healing power.",
    "tower_info": "Upgrade the Tower's HP and attack power.",
    "boss_info": "A powerful boss. Defeat it to turn the tide of battle!",
    # ---- boxes from randombox.x
    "stagebox_1": "Stage Chest", "stagebox_2": "Stage Chest", "stagebox_3": "Stage Chest",
    "stagebox_4": "Stage Chest", "stagebox_5": "Stage Chest",
    "unit_1": "Minion Chest", "unit_2": "Minion Chest", "unit_3": "Minion Chest",
    "parts_weapon_1": "Weapon Chest", "parts_weapon_2": "Weapon Chest", "parts_weapon_3": "Weapon Chest",
    "parts_weapon_4": "Weapon Chest", "parts_armor_1": "Gear Chest", "parts_armor_2": "Gear Chest",
    "parts_armor_3": "Gear Chest", "parts_armor_4": "Gear Chest",
    "honor_weapon_1": "Honor Weapon Chest", "honor_weapon_2": "Honor Weapon Chest", "honor_weapon_3": "Honor Weapon Chest",
    "honor_armor_1": "Honor Gear Chest", "honor_armor_2": "Honor Gear Chest", "honor_armor_3": "Honor Gear Chest",
    "bronze_continue": "Bronze Winning Streak", "silver_continue": "Silver Winning Streak",
    "gold_continue": "Gold Winning Streak", "platinum_continue": "Platinum Winning Streak",
    "master_continue": "Master Winning Streak",
    # ---- area intros (InfoScene::ShowIntro, shown the first time an area opens)
    "stage_intro_0": "The animals have invaded the Dryad Forest! Defeat them and protect the Sanctuary.",
    "stage_intro_1": "Thick Forest\\nRabbits and pandas have gone wild in the Thick Forest. Drive them out and save the trees!",
    "stage_intro_2": "Frigid Forest\\nSnow and ice cover the Frigid Forest. Penguins and polar bears are waiting for you!",
    "stage_intro_3": "Tainted Forest\\nA dark power has tainted this forest. Stay strong and purify it!",
    "stage_intro_4": "Desert\\nThe scorching Desert is ruled by fierce beasts. Don't let the heat slow you down!",
    "stage_intro_5": "Jungle\\nThe last stand! The mightiest animals gather in the Jungle. Bring peace back to the forest!",
    "stage_intro_6": "A new area awaits. Keep fighting for the forest!",
    # ---- story intro (ScriptManager <play type="intro"/>: loading/scene_1..6 + caption intro_1..6)
    "intro_1": "Long ago, a terrible war between plants and animals burned the great forest to ashes.",
    "intro_2": "At last, the leaders of both sides met and swore an oath of peace.",
    "intro_3": "Plants and animals lived side by side, and the forest grew green once again.",
    "intro_4": "But the animal chiefs grew greedy and wanted the whole forest for themselves.",
    "intro_5": "They broke the oath and stormed the plants' homes. The war has begun again!",
    "intro_6": "The forest needs a hero. Rise up and protect the forest!",
}


def build(tables):
    """tables: id -> {lang: text} from the original string tables.  Returns the ids to add."""
    out = {}
    def put(i, cols):
        if i not in tables and i not in out:
            out[i] = cols
    for i, src in ALIAS.items():
        if src in tables: put(i, dict(tables[src]))
    # elite bosses (bigrabbit_2 ...) and building variants (player_tower_2, enemy_home_pvp ...)
    names = set(tables)
    for base in ("bigrabbit", "lesserpanda", "armadillo", "emperorpenguin", "polarbear", "tigris", "madgorilla",
                 "gnome", "dodobird", "leopard", "rhino", "devilram", "rabbit", "madbuffalo"):
        for suf in ("", "_info"):
            if base + suf in names: put(f"{base}_2{suf}", dict(tables[base + suf]))
    for b in ("player_tower", "enemy_tower", "player_home", "enemy_home"):
        for v in ("_2", "_3", "_4", "_pvp"):
            if b in names: put(b + v, dict(tables[b]))
            if b + "_info" in names: put(b + v + "_info", dict(tables[b + "_info"]))
    # "n_<ticket>" = "<ticket name> x[a]"  ->  "<ticket>"
    for i, cols in tables.items():
        if i.startswith("n_") and i.endswith("_ticket"):
            put(i[2:], {l: re.sub(r"\s*[xX×]\s*\[a\]\s*$", "", t) for l, t in cols.items()})
    for i, t in TEXT.items():
        put(i, {l: t for l in LANGS})
    for i in ALIAS:      # alias targets that themselves come from TEXT (e.g. home -> home_upgrade exists)
        if i not in tables and i not in out and ALIAS[i] in out: out[i] = dict(out[ALIAS[i]])
    # every ticket also has a quantity form "n_<ticket>" = "<name> x[a]"
    for i in list(tables) + list(out):
        if i.endswith("_ticket") and not i.startswith("n_") and "n_" + i not in tables and "n_" + i not in out:
            cols = tables.get(i) or out[i]
            out["n_" + i] = {l: t + " x[a]" for l, t in cols.items()}
    # after TEXT so bosses' _2_info etc. also exist when the base had no _info
    for i in list(out):
        if i.endswith("_info") and i[:-5] in ("player_tower", "enemy_tower", "player_home", "enemy_home"):
            for v in ("_2", "_3", "_4", "_pvp"):
                put(i[:-5] + v + "_info", dict(out[i]))
    return out
