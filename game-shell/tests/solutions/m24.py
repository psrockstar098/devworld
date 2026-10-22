import re
import sys

name = sys.stdin.read().strip()
if re.fullmatch(r"[A-Za-z0-9_]{3,16}", name):
    print("OK")
else:
    print("REJECT")
