import sys

for line in sys.stdin.read().splitlines():
    if line.startswith("$ "):
        print(line[2:])
