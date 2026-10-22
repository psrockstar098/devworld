import sys

lines = sys.stdin.read().strip().splitlines()
query = lines[0].strip()
for line in lines[1:]:
    key, value = line.split("=", 1)
    if key == query:
        print(value)
        break
else:
    print("NOT FOUND")
