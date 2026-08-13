"""Golden test for pydos.tui.keys: decoding packed ints and key specs."""

from pydos.tui.keys import Key, decode_key, key_from_spec

# Plain letter: scan 0x1E, ascii 'a'
plain = decode_key(7777)          # 0x1E61
print(plain)
print(plain == "a", plain == 97, plain == "ctrl+a")

# ctrl+s arrives as ascii 19 (ctrl-letter), scan 0x1F, ctrl flag set
ctrl_s = decode_key(19 | (31 << 8) | (1 << 17))
print(ctrl_s)
print(ctrl_s == "ctrl+s", ctrl_s == 19, ctrl_s == "s")

# shift+'A': ascii 65 with the shift flag; name normalizes to lowercase
shifted = decode_key(65 | (30 << 8) | (1 << 16))
print(shifted)
print(shifted == "shift+a", shifted == 65, shifted == "a")

# Up arrow: ascii 0, scan 72
arrow = decode_key(72 << 8)
print(arrow)
print(arrow == Key.UP, arrow == "up", arrow == Key.DOWN)

# alt+f: ascii 0, scan 33, alt flag
alt_f = decode_key((33 << 8) | (1 << 18))
print(alt_f)
print(alt_f == "alt+f", alt_f == "f", alt_f == "alt+g")

# F11 needs the enhanced keyboard path: ascii 0, scan 133
f11 = decode_key(133 << 8)
print(f11)
print(f11 == Key.F11, f11 == "f11")

# Enter and escape
print(decode_key(13 | (28 << 8)) == Key.ENTER)
print(decode_key(27 | (1 << 8)) == "escape")

# Spec parsing round-trips
spec = key_from_spec("ctrl+alt+delete")
print(spec)
combo = key_from_spec("ctrl+shift+z")
print(combo)
print(key_from_spec("esc") == Key.ESCAPE, key_from_spec("pgdn").name)

# Key-to-key equality
print(decode_key(7777) == decode_key(7777))
print(decode_key(7777) == decode_key(7777 | (1 << 17)))

# Printability drives text-field insertion
print(plain.is_printable(), ctrl_s.is_printable(), arrow.is_printable())
