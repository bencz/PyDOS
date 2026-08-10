class Plugin:
    registry = {}
    plugin_key = ""

    def __init_subclass__(klass, *, key, **kwargs):
        super().__init_subclass__(**kwargs)
        klass.plugin_key = key
        Plugin.registry[key] = klass


class JsonPlugin(Plugin, key="json"):
    pass


class DerivedJson(JsonPlugin, key="derived"):
    pass


print(Plugin.registry["json"] is JsonPlugin)
print(Plugin.registry["derived"] is DerivedJson)
print(JsonPlugin.plugin_key, DerivedJson.plugin_key)
