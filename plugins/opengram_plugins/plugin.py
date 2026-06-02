"""Base plugin contract. Skeleton only — beta."""


class Plugin:
    name = "unnamed"
    version = "0.0.0"
    description = ""
    author = ""
    id = ""

    def __init__(self):
        self.host = None

    def on_load(self):
        pass

    def on_unload(self):
        pass

    def on_message(self, message):
        pass

    def getName(self):
        return self.name

    def getVersion(self):
        return self.version

    def getAuthor(self):
        return self.author

    def getDescription(self):
        return self.description

    def getId(self):
        return self.id or self.name

    def getEngine(self):
        return "python"

    def getError(self):
        return None

    def isEnabled(self):
        return True

    def getPack(self):
        return None

    def getIndex(self):
        return 0

