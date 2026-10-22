import sys

score = 0
combo = 0
for line in sys.stdin.read().strip().splitlines():
    if line.strip() == "hit":
        combo += 1
        score += 10 * combo
    elif line.strip() == "miss":
        combo = 0
print(score)
