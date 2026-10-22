import sys
from collections import deque

grid = sys.stdin.read().strip().splitlines()
start = goal = None
for r, row in enumerate(grid):
    for c, ch in enumerate(row):
        if ch == "S":
            start = (r, c)
        elif ch == "G":
            goal = (r, c)
seen = {start: 0}
queue = deque([start])
while queue:
    r, c = queue.popleft()
    if (r, c) == goal:
        print(seen[(r, c)])
        break
    for dr, dc in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        nr, nc = r + dr, c + dc
        if (0 <= nr < len(grid) and 0 <= nc < len(grid[nr])
                and grid[nr][nc] != "#" and (nr, nc) not in seen):
            seen[(nr, nc)] = seen[(r, c)] + 1
            queue.append((nr, nc))
else:
    print("NO PATH")
