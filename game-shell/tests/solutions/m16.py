import sys
from collections import Counter

lines = [line.split() for line in sys.stdin if line.strip()]
counts = Counter(part[1] for part in lines)
print(len(lines))
for status in sorted(counts):
    print(status, counts[status])
