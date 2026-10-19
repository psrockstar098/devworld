import sys

def clamp(n, lo, hi):
    if n < lo:
        return lo
    if n > hi:
        return hi
    return n

def initials(full_name):
    return "".join(word[0].upper() for word in full_name.split())

def is_palindrome(text):
    cleaned = [c.lower() for c in text if c.isalnum()]
    return cleaned == cleaned[::-1]

lines = sys.stdin.read().splitlines()
n, lo, hi = [int(x) for x in lines[0].split()]
print(clamp(n, lo, hi))
print(initials(lines[1]))
print("YES" if is_palindrome(lines[2]) else "NO")
