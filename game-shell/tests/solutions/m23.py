import sys

words = sys.stdin.read().split()
counts = {}
order = []
for w in words:
    if w not in counts:
        counts[w] = 0
        order.append(w)
    counts[w] += 1
for w in order:
    print(f"{w}: {counts[w]}")
