import hashlib
import sys

pw = sys.stdin.read().rstrip("\n")
print(hashlib.sha256(pw.encode()).hexdigest())
