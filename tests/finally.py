# finally.py - comprehensive try/except/finally tests

# Test 1: basic finally, no exception
def test_basic_no_error() -> None:
    try:
        print("t1 try")
    except ValueError:
        print("t1 caught")
    finally:
        print("t1 finally")

# Test 2: finally runs after caught exception
def test_basic_with_error() -> None:
    try:
        x: int = 1 // 0
        print("t2 unreachable")
    except ZeroDivisionError:
        print("t2 caught")
    finally:
        print("t2 finally")

# Test 3: nested try/except/finally - inner catches
def test_nested_inner_catch() -> None:
    try:
        print("t3 outer try")
        try:
            raise ValueError("inner err")
        except ValueError:
            print("t3 inner caught")
        finally:
            print("t3 inner finally")
        print("t3 after inner")
    except ValueError:
        print("t3 outer caught")
    finally:
        print("t3 outer finally")

# Test 4: nested - inner does NOT catch, outer catches
def test_nested_outer_catch() -> None:
    try:
        print("t4 outer try")
        try:
            raise RuntimeError("propagate")
        except ValueError:
            print("t4 inner caught")
        finally:
            print("t4 inner finally")
        print("t4 unreachable")
    except RuntimeError:
        print("t4 outer caught")
    finally:
        print("t4 outer finally")

# Test 5: re-raise in handler, finally still runs at each level
def test_reraise_with_finally() -> None:
    try:
        try:
            raise ValueError("reraise me")
        except ValueError:
            print("t5 inner caught")
            raise
        finally:
            print("t5 inner finally")
    except ValueError:
        print("t5 outer caught")
    finally:
        print("t5 outer finally")

# Test 6: three levels deep
def test_triple_nested() -> None:
    try:
        try:
            try:
                raise KeyError("deep")
            except ValueError:
                print("t6 L3 wrong")
            finally:
                print("t6 L3 finally")
        except KeyError:
            print("t6 L2 caught")
        finally:
            print("t6 L2 finally")
    finally:
        print("t6 L1 finally")

# Test 7: exception in except handler caught by outer
def test_exc_in_handler() -> None:
    try:
        try:
            raise ValueError("first")
        except ValueError:
            print("t7 handler start")
            raise RuntimeError("second")
        finally:
            print("t7 inner finally")
    except RuntimeError:
        print("t7 outer caught second")
    finally:
        print("t7 outer finally")

# Test 8: finally with state mutation
def test_finally_state() -> None:
    x: int = 0
    try:
        x = 1
        raise ValueError("mutate")
    except ValueError:
        x = x + 10
    finally:
        x = x + 100
    print(x)

# Test 9: multiple except with finally
def test_multi_except_finally() -> None:
    try:
        raise KeyError("k")
    except ValueError:
        print("t9 ValueError")
    except KeyError:
        print("t9 KeyError")
    except RuntimeError:
        print("t9 RuntimeError")
    finally:
        print("t9 finally")

# Test 10: bare except with finally
def test_bare_except_finally() -> None:
    try:
        raise RuntimeError("bare")
    except:
        print("t10 bare caught")
    finally:
        print("t10 finally")

def main() -> None:
    test_basic_no_error()
    test_basic_with_error()
    test_nested_inner_catch()
    test_nested_outer_catch()
    test_reraise_with_finally()
    test_triple_nested()
    test_exc_in_handler()
    test_finally_state()
    test_multi_except_finally()
    test_bare_except_finally()
    print("all finally tests done")

main()
