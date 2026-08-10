from collections.abc import Buffer


class Storage:
    def __init__(self, data):
        self.data = bytearray(data)
        self.views = 0

    def __buffer__(self, flags, /):
        self.views += 1
        return memoryview(self.data)

    def __release_buffer__(self, view, /):
        self.views -= 1


storage = Storage(b"PyDOS")
print(isinstance(storage, Buffer))
view = memoryview(storage)
print(storage.views, view.tobytes(), view[0])
view.release()
print(storage.views)
try:
    view.tobytes()
except ValueError:
    print("released")
