import sys

parts = sys.stdin.read().split()
age = int(parts[0])
ticket = parts[1] == "yes"
if age >= 13 and ticket:
    print("ADMIT")
else:
    print("DENY")
