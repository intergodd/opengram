#pragma once

#include "plugins/cxx_plugin.h"
#include <QtCore/QLibrary>
#include <QtCore/QDir>
#include <QtCore/QString>
#include <vector>
#include <memory>

namespace Plugins {

class CxxPluginManager final {
public:
	explicit CxxPluginManager();
	~CxxPluginManager();

	void loadAll();
	void unloadAll();

	bool handleOutgoing(
		not_null<PeerData*> peer,
		QString &text,
		MsgId replyToId,
		const QString &replyPath);

	std::vector<std::pair<CxxPlugin*, CxxMenuItem>> menuItems() const;

	void triggerMenuItemClick(
		CxxPlugin* plugin,
		const QString &itemId,
		not_null<PeerData*> peer);

private:
	struct LoadedPlugin {
		std::unique_ptr<QLibrary> library;
		CxxPlugin* plugin = nullptr;
	};

	QString directory() const;
	void ensureDirectory();

	std::vector<LoadedPlugin> _plugins;
};

}
