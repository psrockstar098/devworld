import sys

lines = sys.stdin.read().strip().splitlines()
x, y, d = lines[0].split()
x, y = int(x), int(y)
dirs = ["N", "E", "S", "W"]
di = dirs.index(d)
dx = {"N": 0, "E": 1, "S": 0, "W": -1}
dy = {"N": -1, "E": 0, "S": 1, "W": 0}
for cmd in lines[1:]:
    cmd = cmd.strip()
    if cmd == "F":
        facing = dirs[di]
        x += dx[facing]
        y += dy[facing]
    elif cmd == "L":
        di = (di - 1) % 4
    elif cmd == "R":
        di = (di + 1) % 4
print(f"{x} {y} {dirs[di]}")
