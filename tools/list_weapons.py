#!/usr/bin/env python3
"""List every weapon in a TF2: Gold Rush item schema, per class and slot, with prefabs merged
the way the game does (each listed prefab in order, then the item's own keys; nested blocks such
as used_by_classes are merged, not replaced).

  python list_weapons.py <items_game.txt> [kv.py dir]

items_game.txt can be extracted from tf_goldrush_dir.vpk (scripts/items/items_game.txt).
"""
import os
import sys

sys.path.insert(0, sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(__file__)))
import kv  # noqa: E402  (Valve KeyValues reader)

CLASSES = ["scout", "sniper", "soldier", "demoman", "medic", "heavy", "pyro", "spy", "engineer"]
SLOTS = ["primary", "secondary", "melee", "pda", "pda2", "building"]


def todict(node):
    d = {}
    for k, v in node.items():
        k = k.lower()
        v = todict(v) if isinstance(v, kv.KV) else v
        d[k] = merge(d[k], v) if isinstance(d.get(k), dict) and isinstance(v, dict) else v
    return d


def merge(a, b):
    r = dict(a)
    for k, v in b.items():
        r[k] = merge(r[k], v) if isinstance(r.get(k), dict) and isinstance(v, dict) else v
    return r


def main():
    tree = kv.load(sys.argv[1])
    root = todict(tree[0] if isinstance(tree, tuple) else tree)
    game = root.get("items_game", root)
    prefabs, items = game.get("prefabs", {}), game.get("items", {})

    def resolve(d, depth=0):
        out = {}
        for p in d.get("prefab", "").split():
            if p in prefabs and depth < 8:
                out = merge(out, resolve(prefabs[p], depth + 1))
        return merge(out, {k: v for k, v in d.items() if k != "prefab"})

    rows = []
    for key, d in items.items():
        if not key.isdigit() or not isinstance(d, dict):
            continue
        r = resolve(d)
        slot = r.get("item_slot", "")
        if slot not in SLOTS:
            continue
        users = r.get("used_by_classes", {})
        users = [c for c in CLASSES if c in {u.lower() for u in users}] if isinstance(users, dict) else []
        rows.append((int(key), r.get("name", ""), r.get("item_class", ""), slot, r.get("baseitem", "0") == "1", users))
    rows.sort()
    for c in CLASSES:
        print("== %s" % c)
        for s in SLOTS:
            names = ["%s (%d)%s" % (n, i, " [stock]" if base else "") for i, n, _, sl, base, u in rows if sl == s and c in u]
            if names:
                print("  %-9s %s" % (s, "; ".join(names)))


if __name__ == "__main__":
    main()
