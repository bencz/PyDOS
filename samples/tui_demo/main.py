from pydos.io.tui import Application


app = Application("PyDOS TUI widgets")
app.create_label(18, 5, "A Pythonic widget layer compiled for DOS")
app.create_button(
    27, 9, 24, "Run lambda handler",
    lambda: app.set_message("The lambda handler was called"),
)
app.create_button(
    27, 12, 24, "Exit",
    lambda: app.stop(),
)
app.run()
