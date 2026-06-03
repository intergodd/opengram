#include "plugins/cxx_plugin_manager.h"
#include "logs.h"
#include "settings.h"

#include <QtCore/QFileInfo>

namespace Plugins {

CxxPluginManager::CxxPluginManager() {
	loadAll();
}

CxxPluginManager::~CxxPluginManager() {
	unloadAll();
}

QString CxxPluginManager::directory() const {
	return cWorkingDir() + u"plugins/cxx"_q;
}

void CxxPluginManager::ensureDirectory() {
	QDir().mkpath(directory());
}

void CxxPluginManager::loadAll() {
	unloadAll();
	ensureDirectory();

	const auto dir = QDir(directory());
#if defined(Q_OS_WIN)
	const auto filter = QStringList(u"*.dll"_q);
#elif defined(Q_OS_MAC)
	const auto filter = QStringList(u"*.dylib"_q);
#else
	const auto filter = QStringList(u"*.so"_q);
#endif

	const auto entries = dir.entryInfoList(filter, QDir::Files, QDir::Name);
	for (const auto &entry : entries) {
		const auto path = entry.absoluteFilePath();
		auto lib = std::make_unique<QLibrary>(path);
		if (lib->load()) {
			typedef CxxPlugin* (*CreateFunc)();
			auto createFn = reinterpret_cast<CreateFunc>(lib->resolve("create_plugin"));
			if (createFn) {
				CxxPlugin* plugin = createFn();
				if (plugin) {
					plugin->onLoad();
					_plugins.push_back({ std::move(lib), plugin });
					LOG(("CxxPlugin: loaded %1 (%2)").arg(plugin->name()).arg(plugin->version()));
				} else {
					LOG(("CxxPlugin: failed to instantiate %1").arg(path));
					lib->unload();
				}
			} else {
				LOG(("CxxPlugin: create_plugin symbol not found in %1").arg(path));
				lib->unload();
			}
		} else {
			LOG(("CxxPlugin: failed to load %1: %2").arg(path).arg(lib->errorString()));
		}
	}
}

void CxxPluginManager::unloadAll() {
	for (auto &loaded : _plugins) {
		if (loaded.plugin) {
			loaded.plugin->onUnload();
			delete loaded.plugin;
		}
		if (loaded.library && loaded.library->isLoaded()) {
			loaded.library->unload();
		}
	}
	_plugins.clear();
}

bool CxxPluginManager::handleOutgoing(
		not_null<PeerData*> peer,
		QString &text,
		MsgId replyToId,
		const QString &replyPath) {
	for (const auto &loaded : _plugins) {
		if (loaded.plugin) {
			if (!loaded.plugin->onSendMessage(peer, text, replyToId, replyPath)) {
				return false;
			}
		}
	}
	return true;
}

std::vector<std::pair<CxxPlugin*, CxxMenuItem>> CxxPluginManager::menuItems() const {
	std::vector<std::pair<CxxPlugin*, CxxMenuItem>> result;
	for (const auto &loaded : _plugins) {
		if (loaded.plugin) {
			for (const auto &item : loaded.plugin->menuItems()) {
				result.push_back({ loaded.plugin, item });
			}
		}
	}
	return result;
}

void CxxPluginManager::triggerMenuItemClick(
		CxxPlugin* plugin,
		const QString &itemId,
		not_null<PeerData*> peer) {
	if (plugin) {
		plugin->onMenuItemClick(itemId, peer);
	}
}

}
