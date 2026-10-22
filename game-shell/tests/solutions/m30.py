import sys

train = []
query = None
for line in sys.stdin.read().strip().splitlines():
    if line.startswith("?"):
        _, qx, qy = line.split()
        query = (float(qx), float(qy))
    else:
        x, y, label = line.split()
        train.append((float(x), float(y), label))
best = None
best_d = None
for x, y, label in train:
    d = (x - query[0]) ** 2 + (y - query[1]) ** 2
    if best_d is None or d < best_d:
        best_d, best = d, label
print(best)
