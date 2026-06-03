#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>
#include <vector>

class PeerData;
struct MsgId;

namespace Plugins {

struct CxxMenuItem {
	QString id;
	QString text;
	QString icon;
};

class CxxPlugin {
public:
	virtual ~CxxPlugin() = default;

	virtual QString name() const = 0;
	virtual QString version() const = 0;
	virtual QString description() const = 0;
	virtual QString author() const = 0;

	virtual void onLoad() {}
	virtual void onUnload() {}

	virtual bool onSendMessage(
		not_null<PeerData*> peer,
		QString &text,
		MsgId replyToId,
		const QString &replyPath) {
		return true;
	}

	virtual std::vector<CxxMenuItem> menuItems() {
		return {};
	}

	virtual void onMenuItemClick(
		const QString &itemId,
		not_null<PeerData*> peer) {}
};

}

extern "C" {
#ifdef Q_OS_WIN
	__declspec(dllexport) Plugins::CxxPlugin* create_plugin();
#else
	Plugins::CxxPlugin* create_plugin();
#endif
}
