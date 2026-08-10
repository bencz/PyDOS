from pydos.io import TextFile, TextIOWrapper
from pydos.io import open_file, read_text, write_text


written = write_text("IO312.TMP", "first\nsecond\n")
print(written)
print(read_text("IO312.TMP"))

file_value = open_file("IO312.TMP", "r")
print(isinstance(file_value, TextFile))
print(TextIOWrapper is TextFile)
print(file_value.closed)
print(file_value.read(5))
file_value.close()
print(file_value.closed)

try:
    open_file("NOFILE.XXX", "r")
    print("missing accepted")
except FileNotFoundError:
    print("missing rejected")
