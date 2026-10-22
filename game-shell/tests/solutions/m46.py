import sys

holder = None
out = []
for line in sys.stdin.read().strip().splitlines():
    proc, action = line.split()
    if action == "lock":
        if holder is None:
            holder = proc
            out.append("OK")
        else:
            out.append("CONFLICT")
    elif action == "unlock":
        if holder == proc:
            holder = None
            out.append("OK")
        else:
            out.append("CONFLICT")
print("\n".join(out))
