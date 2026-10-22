import sys

lines = sys.stdin.read().strip().splitlines()
correct = sum(1 for line in lines if line.split()[0] == line.split()[1])
print(f"{correct} correct out of {len(lines)}")
