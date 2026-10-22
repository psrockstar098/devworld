import sys

lines = sys.stdin.read().strip().splitlines()
screen = int(lines[0])
widths = [int(v) for v in lines[1].split()]
total = 0
count = 0
for w in widths:
    if total + w <= screen:
        total += w
        count += 1
    else:
        break
print(count)
