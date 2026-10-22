import sys

jobs = []
for idx, line in enumerate(sys.stdin.read().strip().splitlines()):
    name, burst = line.split()
    jobs.append((int(burst), idx, name))
for _, _, name in sorted(jobs):
    print(name)
