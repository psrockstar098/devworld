import sys

lines = sys.stdin.read().strip().splitlines()
threshold = int(lines[0])
readings = [int(v) for v in lines[1:]]
print("STOP" if any(r < threshold for r in readings) else "CLEAR")
