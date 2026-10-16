#!/usr/bin/env python3
"""The client side of mod-guild-levels' castable perks: their rows for your realm's spell patch,
and the same rows for the server.

    python3 build_patch.py --patch patch-z80-spells-and-items.MPQ --out patch-new.MPQ \\
        --sql ../data/sql/db-world/mod_guild_levels_spells.sql

--patch is the patch that carries your realm's Spell.dbc. A patch MPQ replaces whole files, so the
new rows go into its copy; every other file in it is copied unchanged. Spell.dbc gains:

- 90452 Mobile Banking: summons the guild chest (gameobject 9503400) in front of you for 5 min.
  Starts as a copy of MOLL-E (54710), the engineers' mailbox. 1 hour cooldown.
- 90453 Have Group, Will Travel: 6 s cast, outdoors and out of combat, 2 hour cooldown. A dummy
  on yourself; the module's script sends the summons.
- 90454 Mass Resurrection: 10 s cast, out of combat. A dummy on yourself; the script offers the
  resurrections.

--sql writes the three spell_dbc rows from the same Spell.dbc rows, so client and server can't
disagree about them. Running it again on its own output adds nothing. Needs Python 3 and
StormLib (`STORMLIB`, default /usr/local/lib/libstorm.dylib).
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


# --- SQL ---------------------------------------------------------------------------------------

def sql_str(s):
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


def write_sql(path, made):
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
        values[F_SUBTEXT] = sql_str("Guild Perk")
        values[F_DESCRIPTION] = sql_str(s["description"])
        lines.append("(" + ", ".join(values) + ")")

    ids = ", ".join(str(s["id"]) for s, _ in made)
    with open(path, "w", encoding="utf-8") as f:
        f.write("-- mod-guild-levels: the castable perks (%s), as the server needs them.\n" % ids)
        f.write("-- Generated by tools/build_patch.py from the same Spell.dbc rows the client patch gets:\n")
        f.write("-- run the tool again, don't edit this file.\n\n")
        f.write("DELETE FROM `spell_dbc` WHERE `ID` IN (%s);\n" % ids)
        f.write("INSERT INTO `spell_dbc` VALUES\n" + ",\n".join(lines) + ";\n")
    print("wrote", path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--patch", required=True, help="your realm's live spell patch (has Spell.dbc)")
    ap.add_argument("--out", help="the new spell patch")
    ap.add_argument("--sql", help="write the spells' server rows here")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        files = unpack(args.patch, tmp)
        spell_dbc = "DBFilesClient\\Spell.dbc"
        if spell_dbc not in files:
            sys.exit("%s has no Spell.dbc" % args.patch)
        made = add_spells(files[spell_dbc])
        if args.out:
            pack(args.out, [(local, name) for name, local in sorted(files.items())])
            print("%s: %d files, %.1f MB" % (args.out, len(files), os.path.getsize(args.out) / 1e6))
        if args.sql:
            write_sql(args.sql, made)


if __name__ == "__main__":
    main()
