import sys


def to_int(addr):
    n = 0
    for octet in addr.split("."):
        n = n * 256 + int(octet)
    return n


lines = sys.stdin.read().strip().splitlines()
dest = to_int(lines[0])
best_len = -1
best_hop = None
for line in lines[1:]:
    prefix, hop = line.split()
    net, plen = prefix.split("/")
    plen = int(plen)
    mask = (0xFFFFFFFF << (32 - plen)) & 0xFFFFFFFF if plen > 0 else 0
    if dest & mask == to_int(net) & mask and plen > best_len:
        best_len, best_hop = plen, hop
print(best_hop if best_hop is not None else "unreachable")
