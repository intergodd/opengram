#include "plugins/js_plugin_manager.h"
#include "main/main_session.h"

namespace Plugins {

JSPluginManager::JSPluginManager(not_null<Main::Session*> session)
: _session(session) {
}

JSPluginManager::~JSPluginManager() {
}

QString JSPluginManager::directory() const {
	return cWorkingDir() + u"plugins/js"_q;
}

void JSPluginManager::ensureDirectory() {
	QDir().mkpath(directory());
}

void JSPluginManager::log(const QString &text) {
}

void JSPluginManager::loadAll() {
}

void JSPluginManager::unloadAll() {
}

bool JSPluginManager::handleOutgoing(
		not_null<PeerData*> peer,
		QString &text,
		MsgId replyToId,
		const QString &replyPath) {
	return true;
}

std::vector<std::pair<LoadedJSPlugin*, JSMenuItem>> JSPluginManager::menuItems() const {
	return {};
}

void JSPluginManager::triggerMenuItemClick(
		LoadedJSPlugin* plugin,
		const QString &itemId,
		not_null<PeerData*> peer) {
}

void JSPluginManager::addMenuItem(LoadedJSPlugin* plugin, const JSMenuItem &item) {
}

void JSPluginManager::removeMenuItem(LoadedJSPlugin* plugin, const QString &id) {
}

}
