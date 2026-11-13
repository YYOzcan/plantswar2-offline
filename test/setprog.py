"""setprog.py <save.json> <area> <stage>: mark every story stage up to area-stage cleared (3 stars) for all heroes."""
import json, sys
p, area, stage = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
d = json.load(open(p))
for u in d["users"].values():
    for h in u.get("heroes", {}).values():
        for a in range(1, area + 1):
            for s in range(1, (stage if a == area else 15) + 1):
                h["stages"][f"{a}-{s}"] = 3
        h["info"]["area"], h["info"]["stage"] = area, stage
json.dump(d, open(p, "w"), indent=1)
print("progress ->", area, stage)
