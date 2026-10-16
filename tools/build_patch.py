#!/usr/bin/env python3
"""The client side of mod-guild-levels: its rows for your realm's spell patch, and the same rows
for the server.

    python3 build_patch.py --patch patch-z80-spells-and-items.MPQ --client /path/to/client \\
        --out patch-new.MPQ --sql ../data/sql/db-world/mod_guild_levels_spells.sql

--patch is the patch that carries your realm's Spell.dbc and Item.dbc. A patch MPQ replaces whole
files, so the new rows go into its copies; every other file in it is copied unchanged. When it has
no Faction.dbc yet, the client's own (--client, newest enUS locale patch) is the base, and the
patch gains it. Spell.dbc gains the castable perks:

- 90452 Mobile Banking: summons the guild chest (gameobject 9503400) in front of you for 5 min.
  Starts as a copy of MOLL-E (54710), the engineers' mailbox. 1 hour cooldown.
- 90453 Have Group, Will Travel: 6 s cast, outdoors and out of combat, 2 hour cooldown. A dummy
  on yourself; the module's script sends the summons.
- 90454 Mass Resurrection: 10 s cast, out of combat. A dummy on yourself; the script offers the
  resurrections.

and the guild rewards' spells, each a copy of a stock spell (see REWARD_SPELLS):

- 90456 / 90457 Guild Teleport: the Renowned / Illustrious Guild Tabard's 10 s teleport to your
  capital (8 h / 2 h cooldown). A dummy; the module's script picks Stormwind or Orgrimmar.
- 90458-90460 Guild Battle Standard: plants standard 9503410-9503412 for 15 min (from the stock
  Battle Standard, 23034).
- 90461-90463 Guild Battle Standard: the standards' 45 yd area aura, +5/10/15% maximum health.
- 90464 / 90465 (Big) Cauldron of Battle: sets out cauldron 9503401 / 9503402 for 10 min (from
  Cauldron of Major Arcane Protection, 41443).
- 90466 Flask of Battle: +15 to all stats for 1 hour, through death (from Flask of the Titans).

Item.dbc gains the reward items (ITEMS), so they have their icons, and Faction.dbc the guild
reputation: 1169 "Guild", a header, and 1168 "Your Guild" under it.

--sql writes every spell's spell_dbc row and both factions' faction_dbc rows from the same rows
the client gets, so client and server can't disagree about them. The items' item_template rows
are in mod_guild_levels_rewards.sql. Running it again on its own output adds nothing. Needs
Python 3 and StormLib (`STORMLIB`, default /usr/local/lib/libstorm.dylib).
"""

import argparse
import ctypes
import os
import struct
import sys
import tempfile

GUILD_CHEST = 9503400

SPELL_FIELDS = 234
F_CATEGORY = 1
F_ATTRIBUTES, F_ATTRIBUTES_LAST = 4, 11
F_CAST_TIME, F_RECOVERY, F_CATEGORY_RECOVERY, F_INTERRUPT = 28, 29, 30, 31
F_DURATION, F_RANGE = 40, 46
F_REAGENT, F_REAGENT_COUNT = 52, 60
F_EFFECT, F_DIE_SIDES, F_BASE_POINTS = 71, 74, 80
F_TARGET_A, F_TARGET_B, F_RADIUS, F_AURA, F_MISC = 86, 89, 92, 95, 110
F_VISUAL, F_ICON, F_ACTIVE_ICON, F_NAME, F_SUBTEXT, F_DESCRIPTION, F_AURA_TEXT = 131, 133, 134, 136, 153, 170, 187
F_GCD_CATEGORY, F_GCD = 205, 206
F_PREVENTION = 214
F_SCHOOL = 225
STRING_FIELDS = set(range(136, 152)) | set(range(153, 169)) | set(range(170, 186)) | set(range(187, 203))
FLOAT_FIELDS = {47, 77, 78, 79, 101, 102, 103, 119, 120, 121, 216, 217, 218, 229, 230, 231}
# Plain `int` columns of AC's spell_dbc; every other integer column is unsigned.
SIGNED_FIELDS = {0, 13, 15, 41, *range(52, 71), 74, 75, 76, 80, 81, 82, *range(110, 116), 224, 228}

ATTR0_OUTDOORS_ONLY = 0x00008000
ATTR0_NOT_SHAPESHIFTED = 0x00010000
ATTR0_NOT_IN_COMBAT = 0x10000000
INTERRUPT_MOVE_PUSHBACK_INTERRUPT = 0x1F     # Hearthstone's: movement, pushback, interrupts
EFFECT_DUMMY, EFFECT_TRANS_DOOR = 3, 50
TARGET_UNIT_CASTER, TARGET_DEST_CASTER_FRONT = 1, 47
CAST_INSTANT, CAST_6S, CAST_10S = 1, 171, 7     # SpellCastTimes.dbc
DURATION_5_MIN = 5                              # SpellDuration.dbc
RANGE_SELF = 1
GCD_CATEGORY, GCD = 133, 1500
SCHOOL_PHYSICAL, SCHOOL_HOLY = 1, 2
PREVENTION_SILENCE = 1                          # like Resurrection: silences stop it
ICON_BOX, ICON_TWILIGHT, ICON_RESURRECTION = 2492, 164, 121  # SpellIcon.dbc
VISUAL_HEARTHSTONE, VISUAL_RESURRECTION = 220, 41
TEMPLATE = 54710                                # MOLL-E: a plain summon-object spell

F_DIE_SIDES_ALL, F_BASE_POINTS_ALL = range(74, 77), range(80, 83)
F_TARGET_A_ALL, F_TARGET_B_ALL, F_RADIUS_ALL, F_AURA_ALL = range(86, 89), range(89, 92), range(92, 95), range(95, 98)
F_MISC_ALL, F_MISC_B_ALL = range(110, 113), range(113, 116)
F_EFFECT_ALL = range(71, 74)
F_FAMILY, F_FAMILY_FLAGS = 208, range(209, 212)
MISC_ALL_STATS = 0xFFFFFFFF                     # Mod Stat's misc value -1: every stat
EFFECT_SUMMON, EFFECT_AREA_AURA_FRIEND = 28, 128
AURA_MOD_STAT, AURA_MOD_HEALTH_PERCENT = 29, 133
DURATION_INFINITE, DURATION_10_MIN, DURATION_15_MIN = 21, 6, 347
RADIUS_45_YD = 11                               # SpellRadius.dbc
CAST_10S_TELEPORT = 7
GUILD_STANDARDS = (9503410, 9503411, 9503412)   # creature_template
CAULDRONS = (9503401, 9503402)                  # gameobject_template

# The rewards' spells: a stock spell's row with these changes. Fields not named keep the stock
# spell's value; "clear_effects" empties effects 2 and 3 first.
#   icon      a SpellIcon.dbc path under Interface\Icons, or a SpellIcon id
def teleport(spell_id, hours):
    return {
        "id": spell_id, "template": 8690, "name": "Guild Teleport", "icon": "INV_Shirt_GuildTabard_01",
        "description": "Teleports you to Stormwind or Orgrimmar.",
        "fields": {F_CATEGORY: 0, F_ATTRIBUTES: ATTR0_NOT_SHAPESHIFTED | ATTR0_NOT_IN_COMBAT, F_CAST_TIME: CAST_10S_TELEPORT,
                   F_RECOVERY: hours * 3600 * 1000, F_CATEGORY_RECOVERY: 0, F_EFFECT: EFFECT_DUMMY, F_BASE_POINTS: 0,
                   F_TARGET_A: TARGET_UNIT_CASTER, F_TARGET_B: 0},
        "clear_effects": True,
    }


def standard(spell_id, rank, creature, percent, icon):
    return {
        "id": spell_id, "template": 23034, "name": "Guild Battle Standard", "icon": icon, "rank": "Rank %d" % rank,
        "description": "Plants a Guild Battle Standard for 15 min that increases the maximum health of your guild "
                       "members within 45 yards by %d%%." % percent,
        "fields": {F_DURATION: DURATION_15_MIN, F_MISC: creature, F_BASE_POINTS: 0},
    }


def standard_aura(spell_id, rank, percent, icon):
    return {
        "id": spell_id, "template": 17626, "name": "Guild Battle Standard", "icon": icon, "rank": "Rank %d" % rank,
        "description": "Increases the maximum health of guild members within 45 yards by $s1%.",
        "aura_text": "Maximum health increased by $s1%.",
        "fields": {F_ATTRIBUTES: 0, F_ATTRIBUTES + 3: 0, F_DURATION: DURATION_INFINITE, F_EFFECT: EFFECT_AREA_AURA_FRIEND,
                   F_DIE_SIDES: 1, F_BASE_POINTS: percent - 1, F_TARGET_A: TARGET_UNIT_CASTER, F_RADIUS: RADIUS_45_YD,
                   F_AURA: AURA_MOD_HEALTH_PERCENT, F_VISUAL: 0},
    }


def cauldron(spell_id, name, go, flasks):
    return {
        "id": spell_id, "template": 41443, "name": name, "icon": None,
        "description": "Sets out a cauldron for 10 min that party and raid members can use to take a Flask of "
                       "Battle. Holds %d flasks." % flasks,
        "fields": {F_DURATION: DURATION_10_MIN, F_MISC: go},
    }


REWARD_SPELLS = [
    teleport(90456, 8),
    teleport(90457, 2),
    standard(90458, 1, GUILD_STANDARDS[0], 5, "INV_Banner_01"),
    standard(90459, 2, GUILD_STANDARDS[1], 10, "INV_Banner_02"),
    standard(90460, 3, GUILD_STANDARDS[2], 15, "INV_Banner_03"),
    standard_aura(90461, 1, 5, "INV_Banner_01"),
    standard_aura(90462, 2, 10, "INV_Banner_02"),
    standard_aura(90463, 3, 15, "INV_Banner_03"),
    cauldron(90464, "Cauldron of Battle", CAULDRONS[0], 10),
    cauldron(90465, "Big Cauldron of Battle", CAULDRONS[1], 25),
    {
        "id": 90466, "template": 17626, "name": "Flask of Battle", "icon": None,
        "description": "Increases all stats by $s1 for $d. Counts as both a Battle and Guardian elixir. "
                       "This effect persists through death.",
        "aura_text": "All stats increased by $s1.",
        "fields": {F_AURA: AURA_MOD_STAT, F_MISC: MISC_ALL_STATS, F_BASE_POINTS: 14},
    },
]

# The reward items' Item.dbc rows: (id, class, subclass, material, display, inventory type, sheath).
# Their item_template rows (mod_guild_levels_rewards.sql) must say the same. The tabards wear the
# stock Guild Tabard's display (20621), whose flag makes the client draw the guild's emblem.
ITEMS = [
    (90456, 4, 0, 7, 20621, 19, 0),     # Renowned Guild Tabard
    (90457, 4, 0, 7, 20621, 19, 0),     # Illustrious Guild Tabard
    (90458, 0, 8, 4, 16263, 0, 0),      # Guild Battle Standard (INV_Banner_01)
    (90459, 0, 8, 4, 932, 0, 0),        # Greater Guild Battle Standard (INV_Banner_02)
    (90460, 0, 8, 4, 3091, 0, 0),       # Grand Guild Battle Standard (INV_Banner_03)
    (90464, 0, 8, 3, 46254, 0, 0),      # Cauldron of Battle (stock cauldron 32839's display)
    (90465, 0, 8, 3, 46255, 0, 0),      # Big Cauldron of Battle (32849's)
]

# Faction.dbc: (id, reputation index, flags, parent, name, description). Every race, base 0.
FACTION_FIELDS = 57
NAME_MASK = 0xFF01FE
FACTIONS = [
    (1169, 106, 0x0C, 0, "Guild", ""),
    (1168, 105, 0x10, 1169, "Your Guild",
     "Your standing with your guild, earned from the guild experience you bring in. It starts over "
     "when you join another guild."),
]
ALL_RACES = 1791
LOCALE_MPQS = ["patch-enUS-3.MPQ", "patch-enUS-2.MPQ", "patch-enUS.MPQ", "locale-enUS.MPQ"]

SPELLS = [
    {
        "id": 90452, "name": "Mobile Banking",
        "description": "Summons a guild chest that gives you access to your guild bank for 5 min.",
        "icon": ICON_BOX, "visual": 0, "school": SCHOOL_PHYSICAL,
        "attributes": ATTR0_NOT_SHAPESHIFTED, "cast": CAST_INSTANT, "recovery": 3600 * 1000, "interrupt": 0,
        "effect": EFFECT_TRANS_DOOR, "target": TARGET_DEST_CASTER_FRONT, "misc": GUILD_CHEST, "duration": DURATION_5_MIN,
    },
    {
        "id": 90453, "name": "Have Group, Will Travel",
        "description": "Summons all party or raid members to your location. Only usable outdoors.",
        "icon": ICON_TWILIGHT, "visual": VISUAL_HEARTHSTONE, "school": SCHOOL_PHYSICAL,
        "attributes": ATTR0_OUTDOORS_ONLY | ATTR0_NOT_SHAPESHIFTED | ATTR0_NOT_IN_COMBAT,
        "cast": CAST_6S, "recovery": 2 * 3600 * 1000, "interrupt": INTERRUPT_MOVE_PUSHBACK_INTERRUPT,
        "effect": EFFECT_DUMMY, "target": TARGET_UNIT_CASTER, "misc": 0, "duration": 0,
    },
    {
        "id": 90454, "name": "Mass Resurrection",
        "description": "Brings all dead party and raid members within 100 yards back to life with 35% health "
                       "and 35% mana. Cannot be cast in combat. A player can be resurrected this way once every 10 min.",
        "icon": ICON_RESURRECTION, "visual": VISUAL_RESURRECTION, "school": SCHOOL_HOLY,
        "attributes": ATTR0_NOT_SHAPESHIFTED | ATTR0_NOT_IN_COMBAT,
        "cast": CAST_10S, "recovery": 10 * 60 * 1000, "interrupt": INTERRUPT_MOVE_PUSHBACK_INTERRUPT,
        "effect": EFFECT_DUMMY, "target": TARGET_UNIT_CASTER, "misc": 0, "duration": 0,
        "prevention": PREVENTION_SILENCE,
    },
]


# --- DBC ---------------------------------------------------------------------------------------

def read_dbc(path):
    data = open(path, "rb").read()
    magic, count, fields, size, strsize = struct.unpack_from("<4s4I", data, 0)
    if magic != b"WDBC" or size != fields * 4:
        sys.exit("%s isn't a plain WDBC file" % path)
    rows = [list(struct.unpack_from("<%dI" % fields, data, 20 + i * size)) for i in range(count)]
    strings = bytearray(data[20 + count * size:20 + count * size + strsize])
    return rows, strings, fields


def write_dbc(path, rows, strings, fields):
    rows = sorted(rows, key=lambda r: r[0])
    with open(path, "wb") as f:
        f.write(struct.pack("<4s4I", b"WDBC", len(rows), fields, fields * 4, len(strings)))
        for r in rows:
            f.write(struct.pack("<%dI" % fields, *r))
        f.write(strings)


def string_at(strings, at):
    return strings[at:strings.index(b"\0", at)].decode("utf-8")


def add_string(strings, text):
    """Offset of text in the string block, appended unless it is already there as a whole string."""
    raw = text.encode("utf-8") + b"\0"
    at = strings.find(b"\0" + raw)
    if at >= 0:
        return at + 1
    at = len(strings)
    strings.extend(raw)
    return at


# --- MPQ ---------------------------------------------------------------------------------------

def stormlib():
    lib = ctypes.CDLL(os.environ.get("STORMLIB", "/usr/local/lib/libstorm.dylib"))
    H = ctypes.c_void_p
    lib.SFileOpenArchive.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(H)]
    lib.SFileExtractFile.argtypes = [H, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
    lib.SFileFindFirstFile.argtypes = [H, ctypes.c_char_p, ctypes.c_void_p, ctypes.c_char_p]
    lib.SFileFindFirstFile.restype = H
    lib.SFileFindNextFile.argtypes = [H, ctypes.c_void_p]
    lib.SFileFindClose.argtypes = [H]
    lib.SFileCreateArchive.argtypes = [ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(H)]
    lib.SFileAddFileEx.argtypes = [H, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_uint]
    lib.SFileCloseArchive.argtypes = [H]
    for fn in (lib.SFileOpenArchive, lib.SFileExtractFile, lib.SFileFindNextFile, lib.SFileFindClose,
               lib.SFileCreateArchive, lib.SFileAddFileEx, lib.SFileCloseArchive):
        fn.restype = ctypes.c_bool
    return lib, H


def unpack(mpq, folder):
    """Extracts every file; returns {archive name: local path}."""
    lib, H = stormlib()
    h = H()
    if not lib.SFileOpenArchive(mpq.encode(), 0, 0x100, ctypes.byref(h)):
        sys.exit("can't open " + mpq)
    names = []
    found = ctypes.create_string_buffer(2048)  # SFILE_FIND_DATA starts with the name (1024 bytes)
    find = lib.SFileFindFirstFile(h, b"*", found, None)
    while find:
        name = found.value.decode("latin-1")
        if name not in ("(listfile)", "(attributes)", "(signature)"):
            names.append(name)
        if not lib.SFileFindNextFile(find, found):
            lib.SFileFindClose(find)
            break
    files = {}
    for i, name in enumerate(names):
        local = os.path.join(folder, "%05d" % i)
        if not lib.SFileExtractFile(h, name.encode("latin-1"), local.encode(), 0):
            sys.exit("can't extract %s from %s" % (name, mpq))
        files[name] = local
    lib.SFileCloseArchive(h)
    if not files:
        sys.exit("%s has no files (or no listfile)" % mpq)
    return files


def pack(out, files):
    """files: [(local path, archive name)]. zlib-compressed, with listfile and attributes."""
    lib, H = stormlib()
    if os.path.exists(out):
        os.remove(out)
    h = H()
    if not lib.SFileCreateArchive(out.encode(), 0x00300000, max(64, len(files) * 2), ctypes.byref(h)):
        sys.exit("can't create " + out)
    for src, name in files:
        if not lib.SFileAddFileEx(h, src.encode(), name.encode("latin-1"), 0x80000200, 0x02, 0x02):
            lib.SFileCloseArchive(h)
            os.remove(out)
            sys.exit("can't add " + name)
    if not lib.SFileCloseArchive(h):
        os.remove(out)
        sys.exit("can't finish " + out)


# --- the spells --------------------------------------------------------------------------------

def add_spells(path):
    spells, strings, fields = read_dbc(path)
    if fields != SPELL_FIELDS:
        sys.exit("Spell.dbc has %d fields, not %d" % (fields, SPELL_FIELDS))
    by_id = {r[0]: r for r in spells}
    if TEMPLATE not in by_id:
        sys.exit("Spell.dbc row %d (MOLL-E) is missing" % TEMPLATE)

    made = []
    for s in SPELLS:
        old = by_id.get(s["id"])
        if old and string_at(strings, old[F_NAME]) != s["name"]:
            sys.exit("Spell.dbc row %d is taken by %r" % (s["id"], string_at(strings, old[F_NAME])))

        row = list(by_id[TEMPLATE])
        row[0] = s["id"]
        row[F_CATEGORY] = 0
        for field in range(F_ATTRIBUTES, F_ATTRIBUTES_LAST + 1):
            row[field] = 0
        row[F_ATTRIBUTES] = s["attributes"]
        row[F_CAST_TIME] = s["cast"]
        row[F_RECOVERY] = s["recovery"]
        row[F_CATEGORY_RECOVERY] = 0
        row[F_INTERRUPT] = s["interrupt"]
        row[F_DURATION] = s["duration"]
        row[F_RANGE] = RANGE_SELF
        for i in range(8):
            row[F_REAGENT + i] = 0
            row[F_REAGENT_COUNT + i] = 0
        row[F_EFFECT] = s["effect"]
        row[F_DIE_SIDES] = 1
        row[F_BASE_POINTS] = 0
        row[F_TARGET_A] = s["target"]
        row[F_TARGET_B] = 0
        row[F_RADIUS] = 0
        row[F_AURA] = 0
        row[F_MISC] = s["misc"]
        row[F_VISUAL] = s["visual"]
        row[F_ICON] = s["icon"]
        row[F_ACTIVE_ICON] = 0
        for field in STRING_FIELDS:
            row[field] = 0
        row[F_NAME] = add_string(strings, s["name"])
        row[F_SUBTEXT] = add_string(strings, "Guild Perk")
        row[F_DESCRIPTION] = add_string(strings, s["description"])
        row[F_GCD_CATEGORY] = GCD_CATEGORY
        row[F_GCD] = GCD
        row[F_SCHOOL] = s["school"]
        row[F_PREVENTION] = s.get("prevention", 0)

        spells[:] = [r for r in spells if r[0] != s["id"]]
        spells.append(row)
        made.append((s, row))
        print("Spell.dbc %d %s" % (s["id"], s["name"]))

    write_dbc(path, spells, strings, fields)
    return made


def effect_fields(index):
    """Every Spell.dbc field that belongs to effect `index` (0-2)."""
    per_effect = [71, 74, 77, 80, 83, 86, 89, 92, 95, 98, 101, 104, 107, 110, 113, 116, 119, 216, 229]
    fields = [base + index for base in per_effect]
    fields += [122 + index * 3 + i for i in range(3)]   # EffectSpellClassMask, three per effect
    return fields


def spell_icons(path):
    """SpellIcon.dbc: icon name (lower case, no folder) -> id."""
    rows, strings, _ = read_dbc(path)
    return {string_at(strings, r[1]).split("\\")[-1].lower(): r[0] for r in rows}


def add_reward_spells(path, icons):
    spells, strings, fields = read_dbc(path)
    by_id = {r[0]: r for r in spells}

    made = []
    for s in REWARD_SPELLS:
        old = by_id.get(s["id"])
        if old and string_at(strings, old[F_NAME]) != s["name"]:
            sys.exit("Spell.dbc row %d is taken by %r" % (s["id"], string_at(strings, old[F_NAME])))
        if s["template"] not in by_id:
            sys.exit("Spell.dbc row %d (template of %d) is missing" % (s["template"], s["id"]))

        row = list(by_id[s["template"]])
        row[0] = s["id"]
        if s.get("clear_effects"):
            for index in (1, 2):
                for field in effect_fields(index):
                    row[field] = 0
        row[F_FAMILY] = 0
        for field in F_FAMILY_FLAGS:
            row[field] = 0
        for field, value in s["fields"].items():
            row[field] = value & 0xFFFFFFFF
        icon = s.get("icon")
        if isinstance(icon, str):
            if icon.lower() not in icons:
                sys.exit("SpellIcon.dbc has no %s" % icon)
            row[F_ICON] = icons[icon.lower()]
        elif icon:
            row[F_ICON] = icon
        for field in STRING_FIELDS:
            row[field] = 0
        row[F_NAME] = add_string(strings, s["name"])
        row[F_SUBTEXT] = add_string(strings, s.get("rank", ""))
        row[F_DESCRIPTION] = add_string(strings, s["description"])
        row[F_AURA_TEXT] = add_string(strings, s.get("aura_text", ""))

        spells[:] = [r for r in spells if r[0] != s["id"]]
        spells.append(row)
        made.append((s, row))
        print("Spell.dbc %d %s %s" % (s["id"], s["name"], s.get("rank", "")))

    write_dbc(path, spells, strings, fields)
    return made


def add_items(path):
    rows, strings, fields = read_dbc(path)
    if fields != 8:
        sys.exit("Item.dbc has %d fields, not 8" % fields)
    ids = {item[0] for item in ITEMS}
    rows = [r for r in rows if r[0] not in ids]
    for item_id, cls, subclass, material, display, inventory, sheath in ITEMS:
        rows.append([item_id, cls, subclass, 0xFFFFFFFF, material & 0xFFFFFFFF, display, inventory, sheath])
        print("Item.dbc %d" % item_id)
    write_dbc(path, rows, strings, fields)


def add_factions(path):
    rows, strings, fields = read_dbc(path)
    if fields != FACTION_FIELDS:
        sys.exit("Faction.dbc has %d fields, not %d" % (fields, FACTION_FIELDS))
    by_id = {r[0]: r for r in rows}
    made = []
    for faction_id, rep_index, flags, parent, name, description in FACTIONS:
        old = by_id.get(faction_id)
        if old and string_at(strings, old[23]) != name:
            sys.exit("Faction.dbc row %d is taken by %r" % (faction_id, string_at(strings, old[23])))
        taken = [r[0] for r in rows if r[1] == rep_index and r[0] != faction_id]
        if taken:
            sys.exit("Faction.dbc reputation index %d is taken by faction %d" % (rep_index, taken[0]))
        row = [0] * FACTION_FIELDS
        row[0], row[1] = faction_id, rep_index
        row[2] = ALL_RACES
        row[14] = flags
        row[18] = parent
        row[21], row[22] = 5, 5     # like the stock factions
        row[23] = add_string(strings, name)
        row[39] = NAME_MASK
        row[40] = add_string(strings, description)
        row[56] = NAME_MASK
        rows = [r for r in rows if r[0] != faction_id]
        rows.append(row)
        made.append(row)
        print("Faction.dbc %d %s" % (faction_id, name))
    write_dbc(path, rows, strings, fields)
    return made


def client_faction_dbc(client, out):
    """The client's own Faction.dbc: from its newest enUS locale patch."""
    lib, H = stormlib()
    for mpq in LOCALE_MPQS:
        archive = os.path.join(client, "Data", "enUS", mpq)
        if not os.path.exists(archive):
            continue
        h = H()
        if not lib.SFileOpenArchive(archive.encode(), 0, 0x100, ctypes.byref(h)):
            continue
        ok = lib.SFileExtractFile(h, b"DBFilesClient\\Faction.dbc", out.encode(), 0)
        lib.SFileCloseArchive(h)
        if ok:
            return out
    sys.exit("no Faction.dbc in %s/Data/enUS" % client)


# --- SQL ---------------------------------------------------------------------------------------

def sql_str(s):
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


def write_sql(path, made, factions):
    def value(i, v):
        if i in STRING_FIELDS:
            return "''"
        if i in FLOAT_FIELDS:
            return "%g" % struct.unpack("<f", struct.pack("<I", v))[0]
        return str(struct.unpack("<i", struct.pack("<I", v))[0] if i in SIGNED_FIELDS else v)

    lines = []
    for s, row in made:
        values = [value(i, v) for i, v in enumerate(row)]
        values[F_NAME] = sql_str(s["name"])
        values[F_SUBTEXT] = sql_str(s.get("rank", "") if "template" in s else "Guild Perk")
        values[F_DESCRIPTION] = sql_str(s["description"])
        values[F_AURA_TEXT] = sql_str(s.get("aura_text", ""))
        lines.append("(" + ", ".join(values) + ")")

    faction_lines = []
    for _, rep_index, flags, parent, name, description in FACTIONS:
        row = next(r for r in factions if r[1] == rep_index)
        faction_lines.append("(%d, %d, %d, %d, %d, 5, 5, %s, %d, %s, %d)" % (
            row[0], rep_index, ALL_RACES, flags, parent, sql_str(name), NAME_MASK, sql_str(description), NAME_MASK))

    ids = ", ".join(str(s["id"]) for s, _ in made)
    faction_ids = ", ".join(str(f[0]) for f in FACTIONS)
    with open(path, "w", encoding="utf-8") as f:
        f.write("-- mod-guild-levels: the castable perks' and the guild rewards' spells (%s) and the guild\n" % ids)
        f.write("-- reputation's factions (%s), as the server needs them.\n" % faction_ids)
        f.write("-- Generated by tools/build_patch.py from the same Spell.dbc and Faction.dbc rows the client\n")
        f.write("-- patch gets: run the tool again, don't edit this file.\n\n")
        f.write("DELETE FROM `spell_dbc` WHERE `ID` IN (%s);\n" % ids)
        f.write("INSERT INTO `spell_dbc` VALUES\n" + ",\n".join(lines) + ";\n\n")
        f.write("DELETE FROM `faction_dbc` WHERE `ID` IN (%s);\n" % faction_ids)
        f.write("INSERT INTO `faction_dbc` (`ID`, `ReputationIndex`, `ReputationRaceMask_1`, `ReputationFlags_1`, "
                "`ParentFactionID`, `ParentFactionCap_1`, `ParentFactionCap_2`, `Name_Lang_enUS`, `Name_Lang_Mask`, "
                "`Description_Lang_enUS`, `Description_Lang_Mask`) VALUES\n" + ",\n".join(faction_lines) + ";\n")
    print("wrote", path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--patch", required=True, help="your realm's live spell patch (has Spell.dbc and Item.dbc)")
    ap.add_argument("--client", help="a 3.3.5a client folder: its Faction.dbc, when --patch has none")
    ap.add_argument("--out", help="the new spell patch")
    ap.add_argument("--sql", help="write the spells' and factions' server rows here")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        files = unpack(args.patch, tmp)
        for name in ("Spell", "Item", "SpellIcon"):
            if "DBFilesClient\\%s.dbc" % name not in files:
                sys.exit("%s has no %s.dbc" % (args.patch, name))
        spell_dbc = files["DBFilesClient\\Spell.dbc"]
        made = add_spells(spell_dbc)
        made += add_reward_spells(spell_dbc, spell_icons(files["DBFilesClient\\SpellIcon.dbc"]))
        add_items(files["DBFilesClient\\Item.dbc"])

        faction_name = "DBFilesClient\\Faction.dbc"
        if faction_name not in files:
            if not args.client:
                sys.exit("%s has no Faction.dbc: pass --client to start from the client's" % args.patch)
            files[faction_name] = client_faction_dbc(args.client, os.path.join(tmp, "Faction.dbc"))
        factions = add_factions(files[faction_name])

        if args.out:
            pack(args.out, [(local, name) for name, local in sorted(files.items())])
            print("%s: %d files, %.1f MB" % (args.out, len(files), os.path.getsize(args.out) / 1e6))
        if args.sql:
            write_sql(args.sql, made, factions)


if __name__ == "__main__":
    main()
