#pragma once

#include <QtCore/QObject>

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
};

}
