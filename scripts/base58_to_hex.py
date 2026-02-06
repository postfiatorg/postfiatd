import sys

ALPHABET = 'rpshnaf39wBUDNEGHJKLM4PQRST7VWXYZ2bcdeCg65jkm8oFqi1tuvAxyz'

def decode(s):
    n = 0
    for c in s:
        n = n * 58 + ALPHABET.index(c)
    return n.to_bytes((n.bit_length() + 7) // 8, 'big')

key = sys.argv[1] if len(sys.argv) > 1 else 'nHBqN2VkMCc8hx53RNzyDkSYTgtehdBg5XTWpR13LLoUvmEhn4Sq'
raw = decode(key)
print(raw[1:-4].hex().upper())
