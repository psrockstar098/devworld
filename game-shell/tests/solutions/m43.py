import sys

s = sys.stdin.read().strip()
depth = 0
ok = True
for c in s:
    if c == "(":
        depth += 1
    elif c == ")":
        depth -= 1
        if depth < 0:
            ok = False
            break
print("OK" if ok and depth == 0 else "MISMATCH")
