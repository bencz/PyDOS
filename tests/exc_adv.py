# exc_adv.py - except-as binding and bare raise (re-raise)

# Test 1: except ValueError as e
def test_binding() -> None:
    try:
        raise ValueError("something went wrong")
    except ValueError as e:
        print(e)

test_binding()

# Test 2: bare raise (re-raise)
def inner_raise() -> None:
    try:
        raise ValueError("inner error")
    except ValueError:
        print("caught inside")
        raise

def test_reraise() -> None:
    try:
        inner_raise()
    except ValueError as e:
        print(e)

test_reraise()

# Test 3: binding with different messages
def fail_with(msg: str) -> None:
    raise ValueError(msg)

def test_messages() -> None:
    try:
        fail_with("error one")
    except ValueError as e:
        print(e)

    try:
        fail_with("error two")
    except ValueError as e:
        print(e)

test_messages()

# Test 4: code continues after handling
def test_continue() -> None:
    x: int = 0
    try:
        raise ValueError("oops")
    except ValueError as e:
        print(e)
        x = 42
    print(x)

test_continue()

print("done")
