import sys
import os
import json
import shutil
import time
import uuid
from types import ModuleType
from pathlib import Path

class Mock:
    def __init__(self, name="Mock"):
        self.__name__ = name
    def __getattr__(self, name):
        if name.startswith('__'):
            raise AttributeError(name)
        return Mock(f"{self.__name__}.{name}")
    def __call__(self, *args, **kwargs):
        return Mock(f"{self.__name__}()")
    def __iter__(self):
        return iter([])
    def __getitem__(self, key):
        return Mock(f"{self.__name__}[{key!r}]")
    def __setitem__(self, key, value):
        pass
    def __delitem__(self, key):
        pass
    def __repr__(self):
        return f"<Mock {self.__name__}>"
    def __str__(self):
        return self.__name__
    def __bool__(self):
        return True
    def __eq__(self, other):
        return True

def register_mock_module(name, attrs=None):
    mod = ModuleType(name)
    if attrs:
        for k, v in attrs.items():
            setattr(mod, k, v)
    sys.modules[name] = mod
    return mod

# TLRPC / TL_messageMediaPhoto mock classes
class TL_messageMediaPhoto:
    pass

class TLRPCMeta(type):
    def __getattr__(cls, name):
        class DummyClass:
            pass
        DummyClass.__name__ = name
        return DummyClass

class TLRPC(metaclass=TLRPCMeta):
    TL_messageMediaPhoto = TL_messageMediaPhoto
    TL_photoSize = Mock("TL_photoSize")

# opengram Plugin import
from opengram_plugins.plugin import Plugin

class HookStrategy:
    DEFAULT = 0
    CANCEL = 1
    REPLACE = 2
    CONTINUE = 3
    MODIFY_FINAL = 4

class HookResult:
    def __init__(self, strategy=0, value=None, **kwargs):
        self.strategy = strategy
        self.value = value
        for k, v in kwargs.items():
            setattr(self, k, v)

class BasePlugin(Plugin):
    def __init__(self):
        super().__init__()
        self._settings = {}
        self._settings_path = Path("installed") / "plugin_settings.json"
        self._load_settings()

    def _load_settings(self):
        try:
            if self._settings_path.exists():
                with open(self._settings_path, "r", encoding="utf-8") as f:
                    all_settings = json.load(f)
                    self._settings = all_settings.get(self.__class__.__name__, {})
        except Exception:
            pass

    def _save_settings(self):
        try:
            all_settings = {}
            if self._settings_path.exists():
                with open(self._settings_path, "r", encoding="utf-8") as f:
                    all_settings = json.load(f)
            all_settings[self.__class__.__name__] = self._settings
            with open(self._settings_path, "w", encoding="utf-8") as f:
                json.dump(all_settings, f, indent=4)
        except Exception:
            pass

    def get_setting(self, key, default=None):
        val = self._settings.get(key, default)
        if val == "true": return True
        if val == "false": return False
        return val

    def set_setting(self, key, value):
        self._settings[key] = str(value).lower() if isinstance(value, bool) else str(value)
        self._save_settings()
        self.on_settings_changed(key, value)

    def on_settings_changed(self, key, value):
        pass

    def log(self, text):
        if self.host:
            self.host.log(f"[{self.__class__.__name__}] {text}")
        else:
            print(f"[{self.__class__.__name__}] {text}")

    def add_on_send_message_hook(self):
        self.log("Registered send_message hook")

    def add_hook(self, name, match_substring=False, priority=100):
        self.log(f"Registered hook: {name}")

    def hook_method(self, method, hook):
        self.log(f"Hooked method: {method}")

    def unhook_method(self, method, hook):
        self.log(f"Unhooked method: {method}")

class MethodHook:
    def __init__(self, *args, **kwargs): pass

class MethodReplacement:
    def __init__(self, *args, **kwargs): pass

class XposedHook:
    def __init__(self, *args, **kwargs): pass

class MenuItemData:
    def __init__(self, *args, **kwargs): pass

class MenuItemType:
    pass

# Register base_plugin
register_mock_module("base_plugin", {
    "BasePlugin": BasePlugin,
    "HookResult": HookResult,
    "HookStrategy": HookStrategy,
    "MethodHook": MethodHook,
    "MethodReplacement": MethodReplacement,
    "XposedHook": XposedHook,
    "MenuItemData": MenuItemData,
    "MenuItemType": MenuItemType,
})

# Register hook_utils
register_mock_module("hook_utils", {
    "find_class": lambda name: Mock(name),
    "get_private_field": lambda obj, name: Mock(name),
    "set_private_field": lambda obj, name, val: None,
})

# Settings
class SettingItem:
    def __init__(self, *args, **kwargs):
        pass

register_mock_module("ui")
register_mock_module("ui.settings", {
    "Input": SettingItem,
    "Header": SettingItem,
    "Divider": SettingItem,
    "Switch": SettingItem,
    "Selector": SettingItem,
    "Text": SettingItem,
    "EditText": SettingItem,
    "Custom": SettingItem,
})

class BulletinHelper:
    @staticmethod
    def show_success(text, *args, **kwargs):
        print(f"[Bulletin Success] {text}")
    @staticmethod
    def show_error(text, *args, **kwargs):
        print(f"[Bulletin Error] {text}")
    @staticmethod
    def show_info(text, *args, **kwargs):
        print(f"[Bulletin Info] {text}")

register_mock_module("ui.bulletin", {
    "BulletinHelper": BulletinHelper,
})

class AlertDialogBuilder:
    BUTTON_POSITIVE = -1
    BUTTON_NEGATIVE = -2
    BUTTON_NEUTRAL = -3

    def __init__(self, *args, **kwargs):
        pass
    def setMessage(self, msg):
        return self
    def setTitle(self, title):
        return self
    def setPositiveButton(self, text, listener):
        return self
    def setNegativeButton(self, text, listener):
        return self
    def show(self):
        return Mock("AlertDialog")

register_mock_module("ui.alert", {
    "AlertDialogBuilder": AlertDialogBuilder,
})

# Android utils
register_mock_module("android_utils", {
    "run_on_ui_thread": lambda func: func() if callable(func) else None,
    "log": lambda text: print(f"[Android Log] {text}"),
    "copy_to_clipboard": lambda text: print(f"[Clipboard] Copied: {text}"),
    "OnClickListener": Mock("OnClickListener"),
    "R": Mock("R"),
})

# Client utils / Send message hook output to stdout via file descriptor 1
class MockAccountInstance:
    def __init__(self):
        self.selectedAccount = 0

class MockSendMessagesHelper:
    def generatePhotoSizes(self, path, *args):
        return Mock("GeneratedPhoto")

    @staticmethod
    def prepareSendingDocument(account, gif_path, *args, **kwargs):
        dialog_id = 0
        reply_to_id = 0
        if len(args) >= 5:
            dialog_id = args[4]
        if len(args) >= 6:
            replyToMsg = args[5]
            if replyToMsg and hasattr(replyToMsg, 'id'):
                reply_to_id = replyToMsg.id

        event = {
            "action": "send_file",
            "chat": str(dialog_id),
            "path": gif_path,
            "reply_to": reply_to_id
        }
        try:
            os.write(1, bytes(json.dumps(event, ensure_ascii=False) + "\n", "utf-8"))
        except Exception as e:
            print(f"Error writing send_file to stdout: {e}", file=sys.stderr)

_outgoing_handlers = []

def register_outgoing(handler):
    _outgoing_handlers.append(handler)

def send_message(params):
    chat_id = 0
    path = ""
    text = ""
    reply_to_id = 0
    if isinstance(params, dict):
        if "peer" in params and hasattr(params["peer"], 'id'):
            chat_id = params["peer"].id
        if "path" in params:
            path = params["path"]
        if "message" in params:
            text = params["message"]
        elif "text" in params:
            text = params["text"]
        if "replyToMsg" in params and params["replyToMsg"] and hasattr(params["replyToMsg"], 'id'):
            reply_to_id = params["replyToMsg"].id
    else:
        if hasattr(params, "peer") and hasattr(params.peer, 'id'):
            chat_id = params.peer.id
        if hasattr(params, "path"):
            path = params.path
        if hasattr(params, "message"):
            text = params.message
        elif hasattr(params, "text"):
            text = params.text
        if hasattr(params, "replyToMsg") and params.replyToMsg and hasattr(params.replyToMsg, 'id'):
            reply_to_id = params.replyToMsg.id

    if path:
        event = {
            "action": "send_file",
            "chat": str(chat_id),
            "path": path,
            "reply_to": reply_to_id
        }
    else:
        event = {
            "action": "send_message",
            "chat": str(chat_id),
            "text": text,
            "reply_to": reply_to_id
        }
    try:
        os.write(1, bytes(json.dumps(event, ensure_ascii=False) + "\n", "utf-8"))
    except Exception as e:
        print(f"Error writing send_message to stdout: {e}", file=sys.stderr)

register_mock_module("client_utils", {
    "send_message": send_message,
    "get_send_messages_helper": lambda: MockSendMessagesHelper(),
    "get_account_instance": lambda: MockAccountInstance(),
    "get_messages_controller": lambda: Mock("MessagesController"),
    "get_file_loader": lambda: Mock("FileLoader"),
    "get_last_fragment": lambda: Mock("LastFragment"),
    "send_request": lambda *args, **kwargs: Mock("Request"),
    "get_notification_center": lambda *args: Mock("NotificationCenter"),
    "NotificationCenterDelegate": Mock("NotificationCenterDelegate"),
    "run_on_queue": lambda func, *args: func() if callable(func) else None,
    "get_media_data_controller": lambda *args: Mock("MediaDataController"),
    "EXTERNAL_NETWORK_QUEUE": Mock("EXTERNAL_NETWORK_QUEUE"),
    "get_user_config": lambda: Mock("UserConfig"),
})

# java.*
class JavaFile:
    def __init__(self, *args):
        if len(args) == 2:
            self._path = os.path.join(str(args[0]), str(args[1]))
        elif len(args) == 1:
            self._path = str(args[0])
        else:
            self._path = "."
    def exists(self):
        return os.path.exists(self._path)
    def mkdirs(self):
        os.makedirs(self._path, exist_ok=True)
        return True
    def getAbsolutePath(self):
        return os.path.abspath(self._path)
    def isFile(self):
        return os.path.isfile(self._path)
    def lastModified(self):
        return int(os.path.getmtime(self._path) * 1000) if self.exists() else 0
    def delete(self):
        try:
            if os.path.isdir(self._path):
                shutil.rmtree(self._path)
            else:
                os.remove(self._path)
            return True
        except Exception:
            return False
    def listFiles(self):
        if not self.exists():
            return []
        return [JavaFile(os.path.join(self._path, f)) for f in os.listdir(self._path)]
    def __str__(self):
        return self._path

class FileOutputStream:
    def __init__(self, path):
        self._file = open(str(path), "wb")
    def write(self, b):
        self._file.write(b)
    def close(self):
        self._file.close()

class ByteArrayOutputStream:
    def __init__(self):
        self._buffer = bytearray()
    def write(self, b):
        self._buffer.extend(b)
    def toByteArray(self):
        return bytes(self._buffer)
    def close(self):
        pass

def dynamic_proxy(*args, **kwargs):
    class MockProxy:
        def __init__(self, *a, **kw): pass
    return MockProxy

register_mock_module("java", {
    "cast": lambda t, v: v,
    "dynamic_proxy": dynamic_proxy,
    "jint": int,
})

register_mock_module("java.lang", {
    "Boolean": bool,
    "Runnable": lambda x: x,
    "String": str,
    "Integer": int,
})

register_mock_module("java.io", {
    "File": JavaFile,
    "FileOutputStream": FileOutputStream,
    "ByteArrayOutputStream": ByteArrayOutputStream,
})

register_mock_module("java.util", {
    "ArrayList": list,
    "Locale": Mock("Locale"),
})

register_mock_module("java.util.concurrent", {
    "ConcurrentHashMap": dict,
})

# android.*
register_mock_module("android")
register_mock_module("android.os", {
    "Bundle": dict,
})

register_mock_module("android.text", {
    "InputType": Mock("InputType"),
    "SpannableStringBuilder": str,
    "Spanned": Mock("Spanned"),
    "TextWatcher": Mock("TextWatcher"),
    "TextUtils": Mock("TextUtils"),
})

register_mock_module("android.text.style", {
    "ForegroundColorSpan": Mock("ForegroundColorSpan"),
    "BackgroundColorSpan": Mock("BackgroundColorSpan"),
})

register_mock_module("android.util", {
    "TypedValue": Mock("TypedValue"),
})

register_mock_module("android.view", {
    "View": Mock("View"),
    "Gravity": Mock("Gravity"),
    "ViewGroup": Mock("ViewGroup"),
    "MotionEvent": Mock("MotionEvent"),
    "ViewTreeObserver": Mock("ViewTreeObserver"),
})

register_mock_module("android.widget", {
    "LinearLayout": Mock("LinearLayout"),
    "FrameLayout": Mock("FrameLayout"),
    "TextView": Mock("TextView"),
    "ScrollView": Mock("ScrollView"),
})

class DialogInterfaceMeta(type):
    def __getattr__(cls, name):
        class DummyClass:
            pass
        DummyClass.__name__ = name
        return DummyClass

class DialogInterface(metaclass=DialogInterfaceMeta):
    BUTTON_POSITIVE = -1
    BUTTON_NEGATIVE = -2
    BUTTON_NEUTRAL = -3

register_mock_module("android.content", {
    "DialogInterface": DialogInterface,
    "Context": Mock("Context"),
    "ClipData": Mock("ClipData"),
    "ClipboardManager": Mock("ClipboardManager"),
    "Intent": Mock("Intent"),
})

register_mock_module("android.graphics", {
    "Bitmap": Mock("Bitmap"),
    "BitmapFactory": Mock("BitmapFactory"),
    "Color": Mock("Color"),
    "Typeface": Mock("Typeface"),
})

register_mock_module("androidx")
register_mock_module("androidx.core.content", {
    "FileProvider": Mock("FileProvider"),
})

# org.telegram.*
class MockApplicationLoader:
    @staticmethod
    def getFilesDirFixed():
        d = os.path.abspath("temp")
        os.makedirs(d, exist_ok=True)
        return d

class MockFileLoader:
    @staticmethod
    def getInstance(*args):
        return MockFileLoader()
    def getPathToMessage(self, messageOwner):
        if hasattr(messageOwner, '_filepath') and messageOwner._filepath:
            return JavaFile(messageOwner._filepath)
        return None
    def getPathToAttach(self, attach, *args):
        if hasattr(attach, '_filepath') and attach._filepath:
            return JavaFile(attach._filepath)
        if hasattr(attach, 'messageOwner') and hasattr(attach.messageOwner, '_filepath'):
            return JavaFile(attach.messageOwner._filepath)
        return None
    def loadFile(self, *args, **kwargs):
        pass

class MessageObject:
    def __init__(self, account, messageOwner, *args):
        self.messageOwner = messageOwner
    @staticmethod
    def getDocument(messageOwner):
        if hasattr(messageOwner, 'media') and messageOwner.media:
            return getattr(messageOwner.media, 'document', None)
        return None
    @staticmethod
    def isGifDocument(document):
        if document and hasattr(document, 'mime_type'):
            return "gif" in document.mime_type
        return False

class MockBuildVars:
    BUILD_VERSION_STRING = "13.0.0"
    BUILD_VERSION = 130000

register_mock_module("org.telegram.messenger", {
    "ApplicationLoader": MockApplicationLoader,
    "MessageObject": MessageObject,
    "FileLoader": MockFileLoader,
    "UserConfig": Mock("UserConfig"),
    "ImageLocation": Mock("ImageLocation"),
    "ImageLoader": Mock("ImageLoader"),
    "SendMessagesHelper": MockSendMessagesHelper,
    "Utilities": Mock("Utilities"),
    "AndroidUtilities": Mock("AndroidUtilities"),
    "LocaleController": Mock("LocaleController"),
    "BuildVars": MockBuildVars,
    "ChatObject": Mock("ChatObject"),
    "UserObject": Mock("UserObject"),
    "MessagesController": Mock("MessagesController"),
    "MediaController": Mock("MediaController"),
    "R": Mock("R"),
    "NotificationCenter": Mock("NotificationCenter"),
})

register_mock_module("org.telegram.ui", {
    "LaunchActivity": Mock("LaunchActivity"),
    "GroupCreateActivity": Mock("GroupCreateActivity"),
    "DialogsActivity": Mock("DialogsActivity"),
})

register_mock_module("org.telegram.ui.Components", {
    "RLottieDrawable": Mock("RLottieDrawable"),
    "ItemOptions": Mock("ItemOptions"),
    "EditTextBoldCursor": Mock("EditTextBoldCursor"),
    "LayoutHelper": Mock("LayoutHelper"),
    "BackupImageView": Mock("BackupImageView"),
    "UItem": Mock("UItem"),
})

register_mock_module("org.telegram.ui.Cells", {
    "CheckBoxCell": Mock("CheckBoxCell"),
    "RadioColorCell": Mock("RadioColorCell"),
})

register_mock_module("org.telegram.ui.ActionBar", {
    "AlertDialog": Mock("AlertDialog"),
    "Theme": Mock("Theme"),
})

# com.*
register_mock_module("com")
register_mock_module("com.exteragram.messenger.utils", {
    "AppUtils": Mock("AppUtils"),
    "ChatUtils": Mock("ChatUtils"),
})
class MockPluginCellSettingFactory:
    @staticmethod
    def new_instance(*args, **kwargs):
        return Mock("PluginCellSettingFactoryInstance")

register_mock_module("com.exteragram.messenger.plugins", {
    "PluginsController": Mock("PluginsController"),
    "Plugin": Mock("Plugin"),
    "PluginsConstants": Mock("PluginsConstants"),
    "PythonPluginsEngine": Mock("PythonPluginsEngine"),
    "PluginCellSettingFactory": MockPluginCellSettingFactory,
})
register_mock_module("com.exteragram.messenger.utils.chats", {
    "ChatUtils": Mock("ChatUtils"),
})

# de.*
register_mock_module("de")
register_mock_module("de.robv.android.xposed", {
    "XC_MethodHook": Mock("XC_MethodHook"),
})

class MockVersion:
    def __init__(self, version_str):
        self.version_str = str(version_str)
        self.parts = []
        for part in self.version_str.split('.'):
            try:
                self.parts.append(int(part.strip()))
            except ValueError:
                self.parts.append(0)
    def __ge__(self, other):
        if not isinstance(other, MockVersion): return False
        return self.parts >= other.parts
    def __gt__(self, other):
        if not isinstance(other, MockVersion): return False
        return self.parts > other.parts
    def __le__(self, other):
        if not isinstance(other, MockVersion): return False
        return self.parts <= other.parts
    def __lt__(self, other):
        if not isinstance(other, MockVersion): return False
        return self.parts < other.parts
    def __eq__(self, other):
        if not isinstance(other, MockVersion): return False
        return self.parts == other.parts

# packaging / typing_extensions / local utilities
register_mock_module("packaging")
register_mock_module("packaging.version", {
    "Version": MockVersion,
})
def mock_deprecated(*args, **kwargs):
    return lambda func: func

register_mock_module("typing_extensions", {
    "get_origin": lambda x: None,
    "get_args": lambda x: (),
    "overload": lambda x: x,
    "deprecated": mock_deprecated,
})
register_mock_module("file_utils", {
    "get_file_extension": lambda path: os.path.splitext(path)[1],
    "get_plugins_dir": lambda: os.path.abspath("installed"),
})
register_mock_module("markdown_utils", {
    "to_html": lambda text: text,
})
register_mock_module("plugin_settings", {
    "get_settings": lambda *args: {},
    "save_settings": lambda *args: None,
})

# Mocking packages needed by ExteraGram plugins
class TLObject:
    pass

register_mock_module("org.telegram.tgnet", {
    "TLRPC": TLRPC,
    "TLObject": TLObject,
})

register_mock_module("org.telegram.tgnet.tl", {
    "TL_stars": Mock("TL_stars"),
})

register_mock_module("dalvik.system", {
    "InMemoryDexClassLoader": Mock("InMemoryDexClassLoader"),
})

register_mock_module("android.app", {
    "Activity": Mock("Activity"),
})

register_mock_module("java.nio", {
    "ByteBuffer": Mock("ByteBuffer"),
})

register_mock_module("java.nio.charset", {
    "Charset": Mock("Charset"),
})

register_mock_module("android.net", {
    "Uri": Mock("Uri"),
})

register_mock_module("extera_utils")
def mock_joverride(*args, **kwargs):
    if len(args) == 1 and callable(args[0]):
        return args[0]
    return lambda func: func

class ExteraBase:
    def __init__(self, *args, **kwargs): pass

def mock_java_subclass(*args, **kwargs):
    def decorator(cls):
        @classmethod
        def new_instance(c, *a, **kw):
            inst = c(*a, **kw)
            inst.java = Mock("JavaInstance")
            return inst
        cls.new_instance = new_instance
        return cls
    return decorator

register_mock_module("extera_utils.classes", {
    "Base": ExteraBase,
    "java_subclass": mock_java_subclass,
    "joverride": mock_joverride,
})
register_mock_module("zwylib_companion")
register_mock_module("com.exteragram.messenger.plugins.models", {
    "CustomSetting": Mock("CustomSetting"),
})
