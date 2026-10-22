import sys

name = sys.stdin.read().rstrip("\n")
print("SELECT * FROM users WHERE name = ?")
print(name)
