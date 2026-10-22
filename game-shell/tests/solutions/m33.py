import sys

lines = sys.stdin.read().strip().splitlines()
x, y = (int(v) for v in lines[0].split())
for cmd in lines[1:]:
    cmd = cmd.strip()
    if cmd == "U":
        y -= 1
    elif cmd == "D":
        y += 1
    elif cmd == "L":
        x -= 1
    elif cmd == "R":
        x += 1
print(f"{x} {y}")
