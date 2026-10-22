import sys

s = sys.stdin.read().strip()
nums = []
ops = []
i = 0
while i < len(s):
    c = s[i]
    if c.isspace():
        i += 1
    elif c.isdigit():
        j = i
        while j < len(s) and s[j].isdigit():
            j += 1
        nums.append(int(s[i:j]))
        i = j
    else:
        ops.append(c)
        i += 1
values = [nums[0]]
for op, n in zip(ops, nums[1:]):
    if op == "*":
        values[-1] *= n
    else:
        values.append(n)
print(sum(values))
