# SPDX-License-Identifier: MIT
"""Which engine classes, functions and fields our modules resolve that ME1 has, and whether ME2 has each one.

  python tools/le2-name-audit.py                       # ME1's VRIK and combat modules (the default list below)
  python tools/le2-name-audit.py game/Comfort game/MainMenuRequest ...   # any modules, as paths under src/ without extension

It reads string literals that look like engine paths ("Pkg.Class", "Pkg.Class.Function", and FieldRef{"Pkg.Class",
"Field"}) out of the modules' .cpp/.hpp, and looks each up in LExSDKv2's generated LE1 and LE2 headers, used here only as
name lists (nothing is compiled against them). For each name ME2 lacks it prints ME2's functions or classes of the same
short name. Names behind constants defined elsewhere are seen only if that file is in the list (engine/GameAdapter is).
It doesn't see native RVAs, bone names or behaviour: those need Ghidra and the game. Results for the VRIK modules on
2026-11-14: research/le2-vrik-audit.md.
"""
import os, re, sys, collections

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SDK = os.path.join(ROOT, 'external', 'LExSDKv2', 'Src', 'LESDK')
SRC = os.path.join(ROOT, 'src')
MODULES = sys.argv[1:] or ['game/HandAim', 'game/ArmIk', 'game/BodyFollow', 'game/BodyScale', 'game/CombatStance', 'game/Fingers',
           'game/GunKick', 'game/Holsters', 'game/Player', 'game/Sights', 'game/SupportGrip', 'game/ViewLevel',
           'game/Weapon', 'game/ScopeParts', 'game/scope_parts', 'game/BodyCalibration', 'engine/BonePose', 'engine/bone_pose',
           'engine/GameAdapter', 'engine/call_function', 'engine/BodyBounds', 'game/Throwables', 'game/Vent', 'game/Posture']


def names(game):
    funcs, classes, fields = set(), set(), collections.defaultdict(set)
    d = os.path.join(SDK, game)
    for f in os.listdir(d):
        p = os.path.join(d, f)
        text = open(p, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'^// Function ([\w]+)\.([\w]+)\.([\w]+)', text, re.M):
            funcs.add(f'{m.group(1)}.{m.group(2)}.{m.group(3)}')
            classes.add(f'{m.group(1)}.{m.group(2)}')
        for m in re.finditer(r'^// Class ([\w]+)\.([\w]+)', text, re.M):
            classes.add(f'{m.group(1)}.{m.group(2)}')
        if f.endswith('_classes.hpp'):
            cur = None
            for line in text.splitlines():
                m = re.match(r'^// Class ([\w]+)\.([\w]+)', line)
                if m:
                    cur = m.group(2)
                    continue
                m = re.match(r'^\s+[\w:<>\*\s,]+?\s\**(\w+)(\[\w*\])?\s*;\s*//\s*0x', line)
                if cur and m:
                    fields[cur].add(m.group(1))
    return funcs, classes, fields


def used():
    out = collections.OrderedDict()
    for mod in MODULES:
        for ext in ('.cpp', '.hpp'):
            p = os.path.join(SRC, mod.replace('/', os.sep) + ext)
            if not os.path.isfile(p):
                continue
            text = open(p, encoding='utf-8', errors='replace').read()
            for m in re.finditer(r'"([A-Z][A-Za-z0-9_]*\.[A-Z][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)?)"', text):
                out.setdefault(m.group(1), set()).add(mod)
            # fields named on a class given by its literal path: FieldRef{"Pkg.Class", "Field"} / {cls, "Field"}
            for m in re.finditer(r'FieldRef\s*\w*\s*\{\s*"([\w]+\.[\w]+)"\s*,\s*"(\w+)"', text):
                out.setdefault(f'{m.group(1)}:{m.group(2)}', set()).add(mod)
    return out


f1, c1, fl1 = names('LE1')
f2, c2, fl2 = names('LE2')
rows = []
for name, mods in used().items():
    if ':' in name:
        cls, fld = name.split(':')
        short = cls.split('.')[1]
        in1, in2 = fld in fl1.get(short, ()), fld in fl2.get(short, ())
        kind = 'field'
    elif name.count('.') == 2:
        in1, in2, kind = name in f1, name in f2, 'function'
    else:
        in1, in2, kind = name in c1, name in c2, 'class'
    if not in1 and not in2:
        continue  # not an engine name (a log tag, a file name...)
    rows.append((in2, kind, name, sorted(mods)))

missing = [r for r in rows if not r[0]]
print(f'{len(rows)} engine names used by these modules that LE1 has; ME2 has {len(rows) - len(missing)}, lacks {len(missing)}\n')
print('ME2 LACKS:')
for _, kind, name, mods in sorted(missing, key=lambda r: (r[3], r[2])):
    short = name.split('.')[-1].split(':')[-1]
    alt = sorted(x for x in (f2 if kind == 'function' else c2) if x.endswith('.' + short))[:4] if kind != 'field' else []
    print(f'  {kind:8} {name:60} {",".join(m.split("/")[-1] for m in mods):30} ME2 has: {", ".join(alt) or "-"}')
print('\nME2 HAS (by module):')
by = collections.defaultdict(list)
for has, kind, name, mods in rows:
    if has:
        for m in mods:
            by[m].append(name)
for m, ns in by.items():
    print(f'  {m}: {len(ns)}')
