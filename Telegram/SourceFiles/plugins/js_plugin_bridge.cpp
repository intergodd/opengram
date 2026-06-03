#include "plugins/js_plugin_bridge.h"
#include "plugins/js_plugin_manager.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "history/history.h"
#include "logs.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>

namespace Plugins {

JSPluginBridge::JSPluginBridge(
	not_null<Main::Session*> session,
	not_null<JSPluginManager*> manager)
: _session(session)
, _manager(manager) {
}

void JSPluginBridge::log(const QString &text) {
	_manager->log(text);
}

void JSPluginBridge::registerPlugin(const QJSValue &pluginObj) {
	_manager->registerPlugin(pluginObj);
}

void JSPluginBridge::addMenuItem(
		const QString &id,
		const QString &text,
		const QString &icon) {
	_manager->addMenuItem(nullptr, { id, text, icon });
}

void JSPluginBridge::removeMenuItem(const QString &id) {
	_manager->removeMenuItem(nullptr, id);
}

void JSPluginBridge::sendMessage(
		const QString &chatId,
		const QString &text,
		int replyToId) {
	const auto raw = chatId.toULongLong();
	if (!raw || text.isEmpty()) {
		return;
	}
	const auto history = _session->data().history(PeerId(BareId(raw)));
	auto sendAction = Api::SendAction(history);
	if (replyToId) {
		sendAction.replyTo.messageId = FullMsgId(
			history->peer->id,
			MsgId(replyToId));
	}
	auto message = Api::MessageToSend(std::move(sendAction));
	message.textWithTags = { text, {} };
	message.fromPlugin = true;
	_session->api().sendMessage(std::move(message));
}

void JSPluginBridge::sendRequest(
		const QString &type,
		const QJSValue &params,
		QJSValue callback) {
	
	auto engine = callback.engine();
	if (!engine) {
		return;
	}

	const auto chatVal = params.property(u"chat_id"_q);
	const auto raw = chatVal.isNumber() 
		? static_cast<uint64>(chatVal.toNumber()) 
		: chatVal.toString().toULongLong();

	if (!raw) {
		QJSValue error = engine->newObject();
		error.setProperty(u"text"_q, u"PEER_ID_INVALID"_q);
		QJSValueList args;
		args << QJSValue(QJSValue::NullValue) << error;
		callCallback(callback, args);
		return;
	}

	const auto history = _session->data().history(PeerId(BareId(raw)));
	const auto peer = history->peer;

	if (type == u"channels_getParticipants"_q) {
		if (!peer->isChannel()) {
			QJSValue error = engine->newObject();
			error.setProperty(u"text"_q, u"PEER_NOT_CHANNEL"_q);
			QJSValueList args;
			args << QJSValue(QJSValue::NullValue) << error;
			callCallback(callback, args);
			return;
		}

		const auto channel = peer->asChannel();
		const auto offset = params.property(u"offset"_q).toInt();
		const auto limit = params.property(u"limit"_q).toInt();

		_session->api().request(MTPchannels_GetParticipants(
			channel->inputChannel(),
			MTP_channelParticipantsRecent(),
			MTP_int(offset),
			MTP_int(limit ? limit : 200),
			MTP_long(0)
		)).done([=](const MTPchannels_ChannelParticipants &result) mutable {
			QJsonObject response;
			QJsonArray usersArray;
			QJsonArray partsArray;

			result.match([&](const MTPDchannels_channelParticipants &data) {
				for (const auto &userVal : data.vusers().v) {
					userVal.match([&](const MTPDuser &u) {
						QJsonObject uObj;
						uObj[u"id"_q] = static_cast<double>(u.vid().v);
						uObj[u"first_name"_q] = qs(u.vfirst_name());
						uObj[u"last_name"_q] = qs(u.vlast_name().value_or_empty());
						uObj[u"username"_q] = qs(u.vusername().value_or_empty());
						uObj[u"bot"_q] = u.is_bot();
						uObj[u"deleted"_q] = u.is_deleted();

						if (u.vstatus()) {
							u.vstatus()->match([&](const MTPDuserStatusOffline &status) {
								QJsonObject sObj;
								sObj[u"was_online"_q] = status.vwas_online().v;
								uObj[u"status"_q] = sObj;
							}, [&](const MTPDuserStatusOnline &) {
								QJsonObject sObj;
								sObj[u"online"_q] = true;
								uObj[u"status"_q] = sObj;
							}, [&](const auto &) {
								uObj[u"status"_q] = QJsonObject();
							});
						}

						usersArray.append(uObj);
					});
				}

				for (const auto &partVal : data.vparticipants().v) {
					partVal.match([&](const MTPDchannelParticipantCreator &p) {
						QJsonObject pObj;
						pObj[u"user_id"_q] = static_cast<double>(p.vuser_id().v);
						pObj[u"creator"_q] = true;
						partsArray.append(pObj);
					}, [&](const MTPDchannelParticipantAdmin &p) {
						QJsonObject pObj;
						pObj[u"user_id"_q] = static_cast<double>(p.vuser_id().v);
						pObj[u"admin"_q] = true;
						partsArray.append(pObj);
					}, [&](const MTPDchannelParticipantBanned &p) {
						QJsonObject pObj;
						pObj[u"user_id"_q] = static_cast<double>(peerFromMTP(p.vpeer()).value);
						pObj[u"banned"_q] = true;
						pObj[u"date"_q] = p.vdate().v;
						partsArray.append(pObj);
					}, [&](const auto &p) {
						QJsonObject pObj;
						pObj[u"user_id"_q] = static_cast<double>(p.vuser_id().v);
						pObj[u"date"_q] = p.vdate().v;
						partsArray.append(pObj);
					});
				}
			}, [&](const auto &) {});

			response[u"users"_q] = usersArray;
			response[u"participants"_q] = partsArray;

			auto parseFn = engine->globalObject().property(u"JSON"_q).property(u"parse"_q);
			auto jsonDoc = QJsonDocument(response).toJson(QJsonDocument::Compact);
			auto jsVal = parseFn.call({ engine->toScriptValue(QString::fromUtf8(jsonDoc)) });

			QJSValueList args;
			args << jsVal << QJSValue(QJSValue::NullValue);
			callCallback(callback, args);

		}).fail([=]() mutable {
			QJSValue error = engine->newObject();
			error.setProperty(u"text"_q, u"API_ERROR"_q);
			QJSValueList args;
			args << QJSValue(QJSValue::NullValue) << error;
			callCallback(callback, args);
		}).send();

	} else if (type == u"channels_editBanned"_q) {
		if (!peer->isChannel()) {
			return;
		}
		const auto channel = peer->asChannel();
		const auto userVal = params.property(u"user_id"_q);
		const auto userId = userVal.isNumber() 
			? static_cast<uint64>(userVal.toNumber()) 
			: userVal.toString().toULongLong();

		const auto participant = _session->data().user(UserId(userId));
		
		const auto rights = ChannelData::KickedRestrictedRights(participant);
		
		_session->api().request(MTPchannels_EditBanned(
			channel->inputChannel(),
			participant->input(),
			RestrictionsToMTP(rights)
		)).done([=](const MTPUpdates &result) mutable {
			_session->api().applyUpdates(result);
			
			QJSValueList args;
			args << engine->toScriptValue(true) << QJSValue(QJSValue::NullValue);
			callCallback(callback, args);
		}).fail([=]() mutable {
			QJSValue error = engine->newObject();
			error.setProperty(u"text"_q, u"API_ERROR"_q);
			QJSValueList args;
			args << QJSValue(QJSValue::NullValue) << error;
			callCallback(callback, args);
		}).send();

	} else if (type == u"messages_deleteChatUser"_q) {
		if (!peer->isChat()) {
			return;
		}
		const auto chat = peer->asChat();
		const auto userVal = params.property(u"user_id"_q);
		const auto userId = userVal.isNumber() 
			? static_cast<uint64>(userVal.toNumber()) 
			: userVal.toString().toULongLong();

		const auto participant = _session->data().user(UserId(userId));

		_session->api().request(MTPmessages_DeleteChatUser(
			MTP_flags(0),
			chat->inputChat(),
			participant->asUser()->inputUser()
		)).done([=](const MTPUpdates &result) mutable {
			_session->api().applyUpdates(result);
			
			QJSValueList args;
			args << engine->toScriptValue(true) << QJSValue(QJSValue::NullValue);
			callCallback(callback, args);
		}).fail([=]() mutable {
			QJSValue error = engine->newObject();
			error.setProperty(u"text"_q, u"API_ERROR"_q);
			QJSValueList args;
			args << QJSValue(QJSValue::NullValue) << error;
			callCallback(callback, args);
		}).send();
	}
}

void JSPluginBridge::callCallback(QJSValue callback, const QJSValueList &args) {
	if (!callback.isCallable()) {
		return;
	}
	auto res = callback.call(args);
	if (res.isError()) {
		log(QString(u"JS Error in callback: %1 (Line %2)")
			.arg(res.toString())
			.arg(res.property(u"lineNumber"_q).toInt()));
	}
}
}
