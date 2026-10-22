import sys

sums = {}
counts = {}
query = None
for line in sys.stdin.read().strip().splitlines():
    if line.startswith("?"):
        _, qx, qy = line.split()
        query = (float(qx), float(qy))
    else:
        x, y, label = line.split()
        sx, sy = sums.get(label, (0.0, 0.0))
        sums[label] = (sx + float(x), sy + float(y))
        counts[label] = counts.get(label, 0) + 1
best = None
best_d = None
for label, (sx, sy) in sums.items():
    cx, cy = sx / counts[label], sy / counts[label]
    d = (cx - query[0]) ** 2 + (cy - query[1]) ** 2
    if best_d is None or d < best_d:
        best_d, best = d, label
print(best)
