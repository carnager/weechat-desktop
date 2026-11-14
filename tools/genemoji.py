import json, sys
d = sys.argv[1]; out = sys.argv[2]
data = json.load(open(f"{d}/en/compact.json"))
sc = {}
for f in ("github", "emojibase", "cldr"):
    for k, v in json.load(open(f"{d}/en/shortcodes/{f}.json")).items():
        v = [v] if isinstance(v, str) else v
        lst = sc.setdefault(k, [])
        for s in v:
            if s not in lst: lst.append(s)
groups = [g["message"] for g in json.load(open(f"{d}/en/messages.json"))["groups"]]
res = []
for e in sorted((e for e in data if "group" in e and e.get("group") != 2), key=lambda e: e.get("order", 1e9)):
    codes = sc.get(e["hexcode"], [])
    if not codes: continue
    res.append([e["unicode"], e["label"], e["group"], codes, e.get("tags", [])])
json.dump({"groups": groups, "emoji": res}, open(out, "w"), ensure_ascii=False, separators=(",", ":"))
print(len(res), groups)
