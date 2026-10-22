import sys

ok = True
for line in sys.stdin.read().strip().splitlines():
    w, h = (int(v) for v in line.split())
    if w < 48 or h < 48:
        ok = False
print("OK" if ok else "TOO SMALL")
