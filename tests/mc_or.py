# Test OR patterns in match/case
def check_or(x) -> None:
    match x:
        case 1 | 2 | 3:
            print("low")
        case 4 | 5:
            print("mid")
        case _:
            print("other")

check_or(1)
check_or(2)
check_or(3)
check_or(4)
check_or(5)
check_or(99)

# OR with string literals
def check_lang(s: str) -> None:
    match s:
        case "py" | "python":
            print("Python")
        case "js" | "javascript":
            print("JavaScript")
        case _:
            print("unknown")

check_lang("py")
check_lang("python")
check_lang("js")
check_lang("rust")
