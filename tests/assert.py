def test_assert_pass() -> None:
    assert True
    print("assert True passed")

    x: int = 5
    assert x > 0
    print("assert x > 0 passed")

    assert 1 == 1
    print("assert 1 == 1 passed")

def test_assert_fail() -> None:
    try:
        assert False, "this should fail"
    except AssertionError as e:
        print("caught: AssertionError")

    try:
        assert 0
    except AssertionError:
        print("caught: assert 0")

def main() -> None:
    test_assert_pass()
    test_assert_fail()
    print("all assert tests done")

main()
