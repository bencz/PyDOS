# exc_par.py - Exception parent-chain matching tests
# Tests that except clauses catch subtype exceptions via hierarchy

# KeyError caught by except LookupError
try:
    raise KeyError("k")
except LookupError as e:
    print("LookupError caught KeyError")

# IndexError caught by except LookupError
try:
    raise IndexError("i")
except LookupError as e:
    print("LookupError caught IndexError")

# ZeroDivisionError caught by except ArithmeticError
try:
    raise ZeroDivisionError("z")
except ArithmeticError as e:
    print("ArithmeticError caught ZeroDivisionError")

# NotImplementedError caught by except RuntimeError
try:
    raise NotImplementedError("n")
except RuntimeError as e:
    print("RuntimeError caught NotImplementedError")

# FileNotFoundError caught by except OSError
try:
    raise FileNotFoundError("f")
except OSError as e:
    print("OSError caught FileNotFoundError")

# ModuleNotFoundError caught by except ImportError
try:
    raise ModuleNotFoundError("m")
except ImportError as e:
    print("ImportError caught ModuleNotFoundError")

# RecursionError caught by except RuntimeError
try:
    raise RecursionError("r")
except RuntimeError as e:
    print("RuntimeError caught RecursionError")

# UnicodeDecodeError caught by except ValueError
try:
    raise UnicodeDecodeError("u")
except ValueError as e:
    print("ValueError caught UnicodeDecodeError")

# SystemExit NOT caught by except Exception
try:
    try:
        raise SystemExit("s")
    except Exception:
        print("ERROR: Exception caught SystemExit")
except BaseException:
    print("BaseException caught SystemExit")

# KeyboardInterrupt NOT caught by except Exception
try:
    try:
        raise KeyboardInterrupt("k")
    except Exception:
        print("ERROR: Exception caught KeyboardInterrupt")
except BaseException:
    print("BaseException caught KeyboardInterrupt")

print("done")
