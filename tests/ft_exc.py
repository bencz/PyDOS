# ft_exc.py - Custom exception hierarchy test

class AppError:
    def __init__(self, msg: str, code: int) -> None:
        self.msg = msg
        self.code = code

    def __str__(self) -> str:
        return "E" + str(self.code) + ": " + self.msg

class ValidationError(AppError):
    def __init__(self, field: str, msg: str) -> None:
        super().__init__(msg, 400)
        self.field = field

    def __str__(self) -> str:
        return "Validation[" + self.field + "]: " + self.msg

class NotFoundError(AppError):
    def __init__(self, what: str) -> None:
        super().__init__(what + " not found", 404)
        self.what = what

class PermissionError(AppError):
    def __init__(self) -> None:
        super().__init__("access denied", 403)

e1: AppError = AppError("generic", 500)
e2: ValidationError = ValidationError("email", "invalid format")
e3: NotFoundError = NotFoundError("user")
e4: PermissionError = PermissionError()
print(e1)
print(e2)
print(e3)
print(e4)
print(e1.code)
print(e2.field)
print(e3.what)
