import sys

lines = sys.stdin.read().strip().splitlines()
ups = sum(1 for line in lines if line.split(":")[1].strip() == "up")
if ups == len(lines):
    print("HEALTHY")
elif ups == 0:
    print("DOWN")
else:
    print("DEGRADED")
