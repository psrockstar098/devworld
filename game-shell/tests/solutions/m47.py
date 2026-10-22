import sys

lines = sys.stdin.read().strip().splitlines()
n = int(lines[0])
yes = sum(1 for line in lines[1:] if line.split()[1] == "yes")
print("COMMIT" if yes * 2 > n else "ABORT")
