#pragma once

#include <QtCore/QObject>
#include <QtQml/QJSValue>
#include <QtCore/QString>

namespace Main {
class Session;
}

namespace Plugins {

class JSPluginManager;

class JSPluginBridge : public QObject {
	Q_OBJECT
public:
	explicit JSPluginBridge(
		not_null<Main::Session*> session,
		not_null<JSPluginManager*> manager);

public slots:
	void log(const QString &text);

	void registerPlugin(const QJSValue &pluginObj);

	void addMenuItem(
		const QString &id,
		const QString &text,
		const QString &icon);
	void removeMenuItem(const QString &id);

	void sendMessage(
		const QString &chatId,
		const QString &text,
		int replyToId = 0);

	void sendRequest(
		const QString &type,
		const QJSValue &params,
		QJSValue callback);

private:
	void callCallback(QJSValue callback, const QJSValueList &args);

	const not_null<Main::Session*> _session;
	const not_null<JSPluginManager*> _manager;
};

}
