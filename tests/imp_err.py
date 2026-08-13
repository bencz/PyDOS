"""Negative test: bare 'import x' must be rejected at compile time.

Only 'from x import y' links source modules; 'import sys' is the single
runtime-resolved exception.  Before the explicit diagnostic, this program
compiled silently and produced an executable with a missing module body.
run_mac.sh lists this test in EXPECT_FAIL and asserts the message suggests
the working 'from mod_hlp import ...' form.
"""

import mod_hlp

print(mod_hlp.helper_value())
