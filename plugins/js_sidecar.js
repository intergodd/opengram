const fs = require('fs');
const path = require('path');
const readline = require('readline');
const vm = require('vm');

const pluginsDir = process.argv[2] || path.join(__dirname, 'js');

function log(text) {
    sendAction({ action: "log", text: text });
}

function sendAction(obj) {
    process.stdout.write(JSON.stringify(obj) + "\n");
}

const plugins = [];

const opengram = {
    _currentLoadingPlugin: null,
    registerPlugin: function(obj) {
        if (this._currentLoadingPlugin) {
            this._currentLoadingPlugin.instance = obj;
        }
    },
    log: function(text) {
        log(text);
    },
    addMenuItem: function(id, text, icon) {
        const plugin = this._currentLoadingPlugin;
        if (plugin) {
            const item = { id, text, icon };
            plugin.menuItems.push(item);
            sendAction({ action: "add_menu_item", id: id, text: text, icon: icon });
        }
    },
    removeMenuItem: function(id) {
        sendAction({ action: "remove_menu_item", id: id });
    },
    sendMessage: function(chatId, text, replyToId) {
        sendAction({ action: "send_message", chat: String(chatId), text: text, reply_to: replyToId || 0 });
    },
    _callbacks: new Map(),
    _nextCallbackId: 1,
    sendRequest: function(type, params, callback) {
        const callbackId = this._nextCallbackId++;
        if (callback) {
            this._callbacks.set(callbackId, callback);
        }
        sendAction({ action: "send_request", type: type, params: params, callback_id: callbackId });
    }
};

function loadAll() {
    log("Запуск Node.js сайдкара плагинов...");
    if (!fs.existsSync(pluginsDir)) {
        fs.mkdirSync(pluginsDir, { recursive: true });
    }

    const dirsToScan = [pluginsDir, path.join(path.dirname(pluginsDir), 'js')];
    const loadedFiles = new Set();

    for (const dir of dirsToScan) {
        if (!fs.existsSync(dir)) continue;
        const files = fs.readdirSync(dir);
        for (const file of files) {
            if (file.endsWith('.js') && !loadedFiles.has(file)) {
                loadedFiles.add(file);
                const filePath = path.join(dir, file);
                loadPlugin(filePath);
            }
        }
    }
}

function loadPlugin(filePath) {
    const code = fs.readFileSync(filePath, 'utf8');
    const pluginMeta = {
        fileName: path.basename(filePath),
        instance: null,
        menuItems: []
    };

    opengram._currentLoadingPlugin = pluginMeta;

    const context = {
        opengram: opengram,
        console: {
            log: (msg) => log("[Console] " + msg),
            error: (msg) => log("[Console Error] " + msg)
        },
        setTimeout: setTimeout,
        clearTimeout: clearTimeout,
        setInterval: setInterval,
        clearInterval: clearInterval,
        Buffer: Buffer,
        require: require,
        process: process
    };

    vm.createContext(context);
    try {
        vm.runInContext(code, context, { filename: path.basename(filePath) });
        const inst = pluginMeta.instance;
        if (inst) {
            plugins.push(pluginMeta);
            log("JS Plugin loaded: " + (inst.name || pluginMeta.fileName) + " (" + (inst.version || "1.0.0") + ")");
            if (typeof inst.onLoad === 'function') {
                try {
                    inst.onLoad();
                } catch (e) {
                    log("Error in onLoad for " + inst.name + ": " + e.stack);
                }
            }
        } else {
            log("No plugin registered in " + path.basename(filePath));
        }
    } catch (e) {
        log("Error loading plugin " + path.basename(filePath) + ": " + e.stack);
    }

    opengram._currentLoadingPlugin = null;
}

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
    terminal: false
});

rl.on('line', (line) => {
    if (!line.trim()) return;
    try {
        const event = JSON.parse(line);
        handleEvent(event);
    } catch (e) {
        log("Error parsing incoming event: " + e.message);
    }
});

function handleEvent(event) {
    const kind = event.event;
    if (kind === "message") {
        for (const p of plugins) {
            if (p.instance && typeof p.instance.onMessage === 'function') {
                try {
                    p.instance.onMessage(event);
                } catch (e) {
                    log("Error in onMessage for " + p.instance.name + ": " + e.stack);
                }
            }
        }
    } else if (kind === "outgoing_message") {
        let text = event.text;
        let cancelled = false;

        for (const p of plugins) {
            if (p.instance && typeof p.instance.onSendMessage === 'function') {
                try {
                    const res = p.instance.onSendMessage(event.chat, text, event.reply_to, event.reply_path);
                    if (res === false) {
                        cancelled = true;
                    } else if (typeof res === 'string') {
                        text = res;
                    }
                } catch (e) {
                    log("Error in onSendMessage for " + p.instance.name + ": " + e.stack);
                }
            }
        }

        if (!cancelled) {
            sendAction({
                action: "send_message",
                chat: event.chat,
                text: text,
                reply_to: event.reply_to || 0
            });
        }
    } else if (kind === "menu_item_click") {
        for (const p of plugins) {
            if (p.instance && typeof p.instance.onMenuItemClick === 'function') {
                try {
                    const hasItem = p.menuItems.some(item => item.id === event.id);
                    if (hasItem) {
                        p.instance.onMenuItemClick(event.id, event.chat);
                    }
                } catch (e) {
                    log("Error in onMenuItemClick for " + p.instance.name + ": " + e.stack);
                }
            }
        }
    } else if (kind === "request_response") {
        const callback = opengram._callbacks.get(event.callback_id);
        if (callback) {
            opengram._callbacks.delete(event.callback_id);
            try {
                callback(event.response, event.error);
            } catch (e) {
                log("Error in sendRequest callback: " + e.stack);
            }
        }
    } else if (kind === "reload") {
        log("Перезагрузка плагинов...");
        for (const p of plugins) {
            if (p.instance && typeof p.instance.onUnload === 'function') {
                try {
                    p.instance.onUnload();
                } catch (e) {
                    log("Error in onUnload for " + p.instance.name + ": " + e.stack);
                }
            }
        }
        plugins.length = 0;
        loadAll();
    } else if (kind === "shutdown") {
        process.exit(0);
    }
}

loadAll();
