import sys


events = []


def monitored_target(left, right):
    return left + right


def on_start(code, instruction_offset):
    events.append(("start", code.co_name, instruction_offset))


def on_return(code, instruction_offset, return_value):
    events.append(("return", code.co_name, return_value))


monitoring = sys.monitoring
tool_id = 5
monitoring.use_tool_id(tool_id, "pydos-monitoring-test")

try:
    monitoring.register_callback(
        tool_id, monitoring.events.PY_START, on_start
    )
    monitoring.register_callback(
        tool_id, monitoring.events.PY_RETURN, on_return
    )
    monitoring.set_local_events(
        tool_id,
        monitored_target.__code__,
        monitoring.events.PY_START | monitoring.events.PY_RETURN,
    )

    print(monitored_target(7, 8))
    print(events)
finally:
    monitoring.set_local_events(
        tool_id, monitored_target.__code__, monitoring.events.NO_EVENTS
    )
    monitoring.register_callback(
        tool_id, monitoring.events.PY_START, None
    )
    monitoring.register_callback(
        tool_id, monitoring.events.PY_RETURN, None
    )
    monitoring.free_tool_id(tool_id)
