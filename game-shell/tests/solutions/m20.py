import sys

total = 0
for token in sys.stdin.read().split():
    try:
        total += int(token)
    except ValueError:
        pass
print(total)
