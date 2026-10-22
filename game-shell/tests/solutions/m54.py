import sys

lines = sys.stdin.read().strip().splitlines()
a = [int(v) for v in lines[0].split()]
b = [int(v) for v in lines[1].split()]
mixed = [max(-100, min(100, x + y)) for x, y in zip(a, b)]
print(" ".join(str(v) for v in mixed))
