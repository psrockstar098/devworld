import sys
from collections import Counter

paths = [line.strip() for line in sys.stdin if line.strip()]
counts = Counter(paths)
print(len(counts))
for p in sorted(k for k, v in counts.items() if v > 1):
    print(p)
