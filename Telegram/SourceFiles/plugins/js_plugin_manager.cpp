#include "plugins/js_plugin_manager.h"
#include "plugins/js_plugin_bridge.h"
#include "plugins/plugins_bridge.h"
#include "main/main_session.h"
#include "logs.h"
#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace Plugins {

JSPluginManager::JSPluginManager(not_null<Main::Session*> session)
: _session(session) {
	loadAll();
}

JSPluginManager::~JSPluginManager() {
	unloadAll();
}

QString JSPluginManager::directory() const {
	return cWorkingDir() + u"plugins/js"_q;
}

void JSPluginManager::ensureDirectory() {
	QDir().mkpath(directory());
}

void JSPluginManager::log(const QString &text) {
	LOG(("JSPlugin: %1").arg(text));
	_session->plugins().logEvents().fire_copy(text + '\n');
}

void JSPluginManager::loadAll() {
	unloadAll();
	ensureDirectory();

	_engine = std::make_unique<QJSEngine>();
	_bridgeObj = new JSPluginBridge(_session, this);
	auto bridgeVal = _engine->newQObject(_bridgeObj);
	_engine->globalObject().setProperty(u"opengram"_q, bridgeVal);
	_engine->globalObject().setProperty(u"api"_q, bridgeVal);

	const auto dir = QDir(directory());
	const auto filter = QStringList(u"*.js"_q);
	const auto entries = dir.entryInfoList(filter, QDir::Files, QDir::Name);

	for (const auto &entry : entries) {
		const auto path = entry.absoluteFilePath();
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			log(QString(u"Failed to open JS plugin file: %1").arg(path));
			continue;
		}

		const auto code = QString::fromUtf8(file.readAll());
		file.close();

		LoadedJSPlugin loaded;
		loaded.fileName = entry.fileName();
		
		_currentLoadingPlugin = &loaded;
		auto result = _engine->evaluate(code, entry.fileName());
		
		if (!loaded.pluginObj.isObject() && result.isObject() && result.hasProperty(u"name"_q)) {
			loaded.pluginObj = result;
		}

		if (loaded.pluginObj.isObject()) {
			loaded.name = loaded.pluginObj.property(u"name"_q).toString();
			if (loaded.name.isEmpty()) {
				loaded.name = entry.baseName();
			}
			loaded.version = loaded.pluginObj.property(u"version"_q).toString();
			loaded.description = loaded.pluginObj.property(u"description"_q).toString();
			loaded.author = loaded.pluginObj.property(u"author"_q).toString();

			auto onLoadVal = loaded.pluginObj.property(u"onLoad"_q);
			if (onLoadVal.isCallable()) {
				auto res = onLoadVal.callWithInstance(loaded.pluginObj);
				if (res.isError()) {
					log(QString(u"JS Error in onLoad for %1: %2 (Line %3)")
						.arg(loaded.name)
						.arg(res.toString())
						.arg(res.property(u"lineNumber"_q).toInt()));
				}
			}

			_plugins.push_back(std::move(loaded));
			log(QString(u"JS Plugin loaded successfully: %1 (%2) by %3")
				.arg(_plugins.back().name)
				.arg(_plugins.back().version)
				.arg(_plugins.back().author));
		} else if (result.isError()) {
			log(QString(u"Error evaluating JS plugin %1: Line %2: %3")
				.arg(entry.fileName())
				.arg(result.property(u"lineNumber"_q).toInt())
				.arg(result.toString()));
		} else {
			log(QString(u"No valid plugin registered or returned in %1").arg(entry.fileName()));
		}

		_currentLoadingPlugin = nullptr;
	}
}

void JSPluginManager::unloadAll() {
	_menuItems.clear();

	for (auto &loaded : _plugins) {
		if (loaded.pluginObj.isObject()) {
			auto onUnloadVal = loaded.pluginObj.property(u"onUnload"_q);
			if (onUnloadVal.isCallable()) {
				auto res = onUnloadVal.callWithInstance(loaded.pluginObj);
				if (res.isError()) {
					log(QString(u"JS Error in onUnload for %1: %2 (Line %3)")
						.arg(loaded.name)
						.arg(res.toString())
						.arg(res.property(u"lineNumber"_q).toInt()));
				}
			}
		}
	}
	_plugins.clear();
	_engine.reset();
	_bridgeObj = nullptr;
}

void JSPluginManager::registerPlugin(const QJSValue &pluginObj) {
	if (_currentLoadingPlugin) {
		_currentLoadingPlugin->pluginObj = pluginObj;
	}
}

void JSPluginManager::addMenuItem(LoadedJSPlugin* plugin, const JSMenuItem &item) {
	auto target = plugin ? plugin : _currentLoadingPlugin;
	if (target) {
		_menuItems.push_back({ target, item });
		_session->plugins().changes().fire({});
	}
}

void JSPluginManager::removeMenuItem(LoadedJSPlugin* plugin, const QString &id) {
	auto target = plugin ? plugin : _currentLoadingPlugin;
	const auto removed = std::remove_if(
		_menuItems.begin(),
		_menuItems.end(),
		[&](const std::pair<LoadedJSPlugin*, JSMenuItem> &pair) {
			return (target == nullptr || pair.first == target) && pair.second.id == id;
		});
	if (removed != _menuItems.end()) {
		_menuItems.erase(removed, _menuItems.end());
		_session->plugins().changes().fire({});
	}
}

bool JSPluginManager::handleOutgoing(
		not_null<PeerData*> peer,
		QString &text,
		MsgId replyToId,
		const QString &replyPath) {
	
	for (const auto &loaded : _plugins) {
		if (loaded.pluginObj.isObject()) {
			auto hook = loaded.pluginObj.property(u"onSendMessage"_q);
			if (hook.isCallable()) {
				QJSValueList args;
				args << _engine->toScriptValue(QString::number(peer->id.value));
				args << _engine->toScriptValue(text);
				args << _engine->toScriptValue(replyToId.bare);
				args << _engine->toScriptValue(replyPath);

				auto res = hook.callWithInstance(loaded.pluginObj, args);
				if (res.isError()) {
					log(QString(u"JS Error in onSendMessage for %1: %2 (Line %3)")
						.arg(loaded.name)
						.arg(res.toString())
						.arg(res.property(u"lineNumber"_q).toInt()));
				} else if (res.isBool() && !res.toBool()) {
					return false;
				}
			}
		}
	}
	return true;
}

std::vector<std::pair<LoadedJSPlugin*, JSMenuItem>> JSPluginManager::menuItems() const {
	return _menuItems;
}

void JSPluginManager::triggerMenuItemClick(
		LoadedJSPlugin* plugin,
		const QString &itemId,
		not_null<PeerData*> peer) {
	if (plugin && plugin->pluginObj.isObject()) {
		auto clickHandler = plugin->pluginObj.property(u"onMenuItemClick"_q);
		if (clickHandler.isCallable()) {
			QJSValueList args;
			args << _engine->toScriptValue(itemId);
			args << _engine->toScriptValue(QString::number(peer->id.value));
			auto res = clickHandler.callWithInstance(plugin->pluginObj, args);
			if (res.isError()) {
				log(QString(u"JS Error in onMenuItemClick for %1: %2 (Line %3)")
					.arg(plugin->name)
					.arg(res.toString())
					.arg(res.property(u"lineNumber"_q).toInt()));
			}
		}
	}
}

}
