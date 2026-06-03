#pragma once

#include <QtQml/QJSEngine>
#include <QtQml/QJSValue>
#include <QtCore/QString>
#include <QtCore/QDir>
#include <vector>
#include <memory>

class PeerData;
struct MsgId;

namespace Main {
class Session;
}

namespace Plugins {

struct JSMenuItem {
	QString id;
	QString text;
	QString icon;
};

struct LoadedJSPlugin {
	QString fileName;
	QString name;
	QString version;
	QString description;
	QString author;
	QJSValue pluginObj;
};

class JSPluginBridge;

class JSPluginManager final {
public:
	explicit JSPluginManager(not_null<Main::Session*> session);
	~JSPluginManager();

	void loadAll();
	void unloadAll();

	bool handleOutgoing(
		not_null<PeerData*> peer,
		QString &text,
		MsgId replyToId,
		const QString &replyPath);

	std::vector<std::pair<LoadedJSPlugin*, JSMenuItem>> menuItems() const;
	void triggerMenuItemClick(
		LoadedJSPlugin* plugin,
		const QString &itemId,
		not_null<PeerData*> peer);

	void log(const QString &text);

	void addMenuItem(LoadedJSPlugin* plugin, const JSMenuItem &item);
	void removeMenuItem(LoadedJSPlugin* plugin, const QString &id);
	void registerPlugin(const QJSValue &pluginObj);

	not_null<Main::Session*> session() const {
		return _session;
	}

private:
	QString directory() const;
	void ensureDirectory();

	const not_null<Main::Session*> _session;
	std::unique_ptr<QJSEngine> _engine;
	JSPluginBridge* _bridgeObj = nullptr;
	std::vector<LoadedJSPlugin> _plugins;

	std::vector<std::pair<LoadedJSPlugin*, JSMenuItem>> _menuItems;

	LoadedJSPlugin* _currentLoadingPlugin = nullptr;
};

}
