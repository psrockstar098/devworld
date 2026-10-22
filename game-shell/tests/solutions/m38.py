import sys

state = 0
run = 0
presses = 0
for line in sys.stdin.read().strip().splitlines():
    v = int(line.strip())
    if v == state:
        run = 0
    else:
        run += 1
        if run >= 3:
            state = v
            run = 0
            if state == 1:
                presses += 1
print(presses)
