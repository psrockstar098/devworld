import sys

s = sys.stdin.read().strip()
out = []
i = 0
while i < len(s):
    c = s[i]
    if c.isspace():
        i += 1
    elif c.isdigit():
        j = i
        while j < len(s) and s[j].isdigit():
            j += 1
        out.append(f"INT:{s[i:j]}")
        i = j
    else:
        out.append(f"OP:{c}")
        i += 1
print("\n".join(out))
