import sys

lines = sys.stdin.read().strip().splitlines()
x, y = (int(v) for v in lines[0].split())
w, h = (int(v) for v in lines[1].split())
for cmd in lines[2:]:
    cmd = cmd.strip()
    nx, ny = x, y
    if cmd == "U":
        ny -= 1
    elif cmd == "D":
        ny += 1
    elif cmd == "L":
        nx -= 1
    elif cmd == "R":
        nx += 1
    if 0 <= nx < w and 0 <= ny < h:
        x, y = nx, ny
print(f"{x} {y}")
