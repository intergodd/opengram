"""Load a single .plg file as a Plugin instance. Skeleton only — beta."""

from importlib.machinery import SourceFileLoader
from pathlib import Path

from .plugin import Plugin

PLG_SUFFIX = ".plg"


def load_plg(path):
    path = Path(path)
    module = SourceFileLoader(path.stem, str(path)).load_module()
    for value in vars(module).values():
        if isinstance(value, type) and issubclass(value, Plugin) and value is not Plugin:
            if value.__module__ == module.__name__:
                inst = value()
                for key in ['name', 'version', 'description', 'author', 'id']:
                    val = getattr(module, f"__{key}__", getattr(module, key, None))
                    if val is not None:
                        if getattr(inst, key, None) in (None, 'unnamed', '0.0.0', ''):
                            setattr(inst, key, val)
                return inst
    raise ValueError(f"no Plugin subclass found in {path}")
