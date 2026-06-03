/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "plugins/plugins_bridge.h"

#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_common.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "history/history.h"
#include "history/history_item.h"
#include "settings.h"
#include "logs.h"
#include "storage/localimageloader.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>

namespace Plugins {
namespace {

constexpr auto kReadLimit = 256 * 1024;

const auto kDisabledSuffix = u".off"_q;
const auto kPluginSuffix = u".plg"_q;
const auto kExteraSuffix = u".plugin"_q;

[[nodiscard]] bool IsPluginName(const QString &name) {
	return name.endsWith(kPluginSuffix, Qt::CaseInsensitive)
		|| name.endsWith(kExteraSuffix, Qt::CaseInsensitive);
}

[[nodiscard]] QString NodeExecutable() {
	return u"node"_q;
}

void CopyTree(const QString &from, const QString &to) {
	const auto source = QDir(from);
	if (!source.exists()) {
		return;
	}
	QDir().mkpath(to);
	const auto entries = source.entryInfoList(
		QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
	for (const auto &entry : entries) {
		const auto target = to + '/' + entry.fileName();
		if (entry.isDir()) {
			CopyTree(entry.absoluteFilePath(), target);
		} else if (!QFile::exists(target)) {
			QFile::copy(entry.absoluteFilePath(), target);
		}
	}
}

[[nodiscard]] QString PluginsRoot() {
	return cWorkingDir() + u"plugins"_q;
}

[[nodiscard]] QString CanonicalName(const QString &fileName) {
	return fileName.endsWith(kDisabledSuffix, Qt::CaseInsensitive)
		? fileName.left(fileName.size() - kDisabledSuffix.size())
		: fileName;
}

[[nodiscard]] QString CapturedValue(
		const QString &source,
		const QString &field) {
	const auto expression = QRegularExpression(
		u"^\\s*"_q + field + u"\\s*=\\s*[\"']([^\"']*)[\"']"_q,
		QRegularExpression::MultilineOption);
	const auto match = expression.match(source);
	return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

[[nodiscard]] QString MetaValue(const QString &source, const QString &field) {
	auto result = CapturedValue(source, u"__"_q + field + u"__"_q);
	return result.isEmpty() ? CapturedValue(source, field) : result;
}

[[nodiscard]] QString PluginFilePath(const QString &name) {
	return PluginsRoot() + u"/installed/"_q + QFileInfo(name).fileName();
}

} // namespace

Bridge::Bridge(not_null<Main::Session*> session) : _session(session) {
	start();
}

Bridge::~Bridge() {
	if (_process.state() == QProcess::NotRunning) {
		return;
	}
	sendEvent({ { u"event"_q, u"shutdown"_q } });
	_process.closeWriteChannel();
	if (!_process.waitForFinished(1000)) {
		_process.kill();
		_process.waitForFinished(1000);
	}
}

QString Bridge::directory() const {
	return PluginsRoot() + u"/installed"_q;
}

void Bridge::ensureDirectory() {
	QDir().mkpath(directory());
}

void Bridge::start() {
	_menuItems.clear();
	const auto root = PluginsRoot();
	QDir().mkpath(root);

	const auto bundled = cExeDir() + u"plugins"_q;
	if (bundled != root) {
		CopyTree(bundled, root);
	}

	if (!QFile::exists(root + u"/js_sidecar.js"_q)) {
		LOG(("Plugins: JS runtime missing, execution disabled (%1).").arg(root));
		return;
	}
	ensureDirectory();

	_process.setProgram(NodeExecutable());
	_process.setArguments({
		root + u"/js_sidecar.js"_q,
		directory(),
	});
	_process.setWorkingDirectory(root);

	QObject::connect(&_process, &QProcess::readyReadStandardOutput, [=] {
		readActions();
	});
	QObject::connect(&_process, &QProcess::readyReadStandardError, [=] {
		const auto errorOutput = QString::fromUtf8(_process.readAllStandardError());
		LOG(("Plugins Error: %1").arg(errorOutput));
		_logEvents.fire_copy(errorOutput);
	});
	QObject::connect(
		&_process,
		&QProcess::errorOccurred,
		[=](QProcess::ProcessError error) {
			LOG(("Plugins: process error %1.").arg(int(error)));
		});

	_process.start();

	_session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		handleIncoming(item);
	}, _lifetime);
}

bool Bridge::isInstalled(const QString &name) const {
	return QFile::exists(PluginFilePath(name));
}

void Bridge::installPlugin(const QString &name, const QByteArray &content) {
	ensureDirectory();
	auto file = QFile(PluginFilePath(name));
	if (file.open(QIODevice::WriteOnly)) {
		file.write(content);
		file.close();
		requestReload();
	}
}

void Bridge::removePlugin(const QString &name) {
	if (QFile::remove(PluginFilePath(name))) {
		requestReload();
	}
}

std::optional<Plugin> Bridge::ReadMetadata(const QString &path) {
	const auto info = QFileInfo(path);
	const auto canonical = CanonicalName(info.fileName());
	if (!IsPluginName(canonical)) {
		return std::nullopt;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return std::nullopt;
	}
	const auto source = QString::fromUtf8(file.read(kReadLimit));
	const auto looksLikePlugin = source.contains(u"Plugin"_q)
		|| source.contains(u"__name__"_q)
		|| source.contains(u"__id__"_q);
	if (!looksLikePlugin) {
		return std::nullopt;
	}
	auto result = Plugin();
	result.fileName = canonical;
	result.name = MetaValue(source, u"name"_q);
	if (result.name.isEmpty()) {
		const auto dot = canonical.lastIndexOf('.');
		result.name = (dot > 0) ? canonical.left(dot) : canonical;
	}
	result.version = MetaValue(source, u"version"_q);
	result.description = MetaValue(source, u"description"_q);
	result.author = MetaValue(source, u"author"_q);
	return result;
}

std::vector<Plugin> Bridge::list() const {
	auto result = std::vector<Plugin>();
	const auto dir = QDir(directory());
	if (!dir.exists()) {
		return result;
	}
	const auto entries = dir.entryList(
		{ u"*"_q + kPluginSuffix,
			u"*"_q + kPluginSuffix + kDisabledSuffix,
			u"*"_q + kExteraSuffix,
			u"*"_q + kExteraSuffix + kDisabledSuffix },
		QDir::Files,
		QDir::Name);
	for (const auto &entry : entries) {
		auto parsed = ReadMetadata(dir.filePath(entry));
		if (!parsed) {
			continue;
		}
		parsed->enabled = !entry.endsWith(
			kDisabledSuffix,
			Qt::CaseInsensitive);
		result.push_back(std::move(*parsed));
	}
	return result;
}

std::optional<Plugin> Bridge::find(const QString &fileName) const {
	const auto canonical = CanonicalName(fileName);
	for (auto &plugin : list()) {
		if (plugin.fileName == canonical) {
			return plugin;
		}
	}
	return std::nullopt;
}

QString Bridge::resolveOnDiskPath(const QString &fileName) const {
	const auto canonical = CanonicalName(fileName);
	const auto dir = QDir(directory());
	const auto enabled = dir.filePath(canonical);
	if (QFile::exists(enabled)) {
		return enabled;
	}
	const auto disabled = enabled + kDisabledSuffix;
	if (QFile::exists(disabled)) {
		return disabled;
	}
	return QString();
}

bool Bridge::install(const QString &sourcePath, QString *errorText) {
	const auto metadata = ReadMetadata(sourcePath);
	if (!metadata) {
		if (errorText) {
			*errorText = u"This is not a valid opengram plugin."_q;
		}
		return false;
	}
	ensureDirectory();

	const auto existing = resolveOnDiskPath(metadata->fileName);
	if (!existing.isEmpty()) {
		QFile::remove(existing);
	}

	const auto target = QDir(directory()).filePath(metadata->fileName);
	if (!QFile::copy(sourcePath, target)) {
		if (errorText) {
			*errorText = u"Could not copy the plugin file."_q;
		}
		return false;
	}

	_changes.fire({});
	requestReload();
	return true;
}

void Bridge::uninstall(const QString &fileName) {
	const auto path = resolveOnDiskPath(fileName);
	if (path.isEmpty() || !QFile::remove(path)) {
		return;
	}
	_changes.fire({});
	requestReload();
}

void Bridge::setEnabled(const QString &fileName, bool enabled) {
	const auto path = resolveOnDiskPath(fileName);
	if (path.isEmpty()) {
		return;
	}
	const auto canonical = QDir(directory()).filePath(
		CanonicalName(fileName));
	const auto target = enabled ? canonical : (canonical + kDisabledSuffix);
	if (path == target) {
		return;
	}
	if (!QFile::rename(path, target)) {
		return;
	}
	_changes.fire({});
	requestReload();
}

void Bridge::requestReload() {
	if (_process.state() == QProcess::Running) {
		sendEvent({ { u"event"_q, u"reload"_q } });
	} else if (_process.state() == QProcess::NotRunning) {
		start();
	}
}

rpl::producer<> Bridge::changes() const {
	return _changes.events();
}

rpl::producer<QString> Bridge::logEvents() const {
	return _logEvents.events();
}

const std::vector<MenuItem> &Bridge::menuItems() const {
	return _menuItems;
}

void Bridge::triggerMenuItemClick(const QString &id, not_null<PeerData*> peer) {
	sendEvent({
		{ u"event"_q, u"menu_item_click"_q },
		{ u"id"_q, id },
		{ u"chat"_q, QString::number(peer->id.value) },
	});
}

void Bridge::handleIncoming(not_null<HistoryItem*> item) {
	if (item->out()) {
		return;
	}
	const auto text = item->originalText().text;
	if (text.isEmpty()) {
		return;
	}
	const auto peer = item->history()->peer;
	sendEvent({
		{ u"event"_q, u"message"_q },
		{ u"chat"_q, QString::number(peer->id.value) },
		{ u"sender"_q, QString::number(item->from()->id.value) },
		{ u"text"_q, text },
	});
}

void Bridge::handleOutgoing(
		not_null<PeerData*> peer,
		const QString &text,
		MsgId replyToId,
		const QString &replyPath) {
	sendEvent({
		{ u"event"_q, u"outgoing_message"_q },
		{ u"chat"_q, QString::number(peer->id.value) },
		{ u"text"_q, text },
		{ u"reply_to"_q, replyToId.bare },
		{ u"reply_path"_q, replyPath },
	});
}

void Bridge::sendEvent(const QJsonObject &event) {
	if (_process.state() != QProcess::Running) {
		return;
	}
	_process.write(QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n');
}

void Bridge::readActions() {
	while (_process.canReadLine()) {
		const auto line = _process.readLine();
		const auto parsed = QJsonDocument::fromJson(line);
		if (parsed.isObject()) {
			handleAction(parsed.object());
		}
	}
}

void Bridge::handleAction(const QJsonObject &action) {
	const auto type = action.value(u"action"_q).toString();
	if (type == u"send_message"_q) {
		const auto raw = action.value(u"chat"_q).toString().toULongLong();
		const auto text = action.value(u"text"_q).toString();
		const auto replyToId = action.value(u"reply_to"_q).toInt();
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
	} else if (type == u"send_file"_q) {
		const auto raw = action.value(u"chat"_q).toString().toULongLong();
		const auto path = action.value(u"path"_q).toString();
		const auto replyToId = action.value(u"reply_to"_q).toInt();
		if (!raw || path.isEmpty()) {
			return;
		}
		const auto history = _session->data().history(PeerId(BareId(raw)));
		auto file = QFile(path);
		if (file.open(QIODevice::ReadOnly)) {
			const auto content = file.readAll();
			auto action = Api::SendAction(history);
			if (replyToId) {
				action.replyTo.messageId = FullMsgId(
					history->peer->id,
					MsgId(replyToId));
			}
			const auto isPhoto = path.endsWith(u".png"_q, Qt::CaseInsensitive)
				|| path.endsWith(u".jpg"_q, Qt::CaseInsensitive)
				|| path.endsWith(u".jpeg"_q, Qt::CaseInsensitive);
			_session->api().sendFile(
				content,
				isPhoto ? SendMediaType::Photo : SendMediaType::File,
				action);
		}
	} else if (type == u"add_menu_item"_q) {
		const auto id = action.value(u"id"_q).toString();
		const auto text = action.value(u"text"_q).toString();
		const auto icon = action.value(u"icon"_q).toString();
		const auto menuType = action.value(u"menu_type"_q).toInt(1);
		if (!id.isEmpty() && !text.isEmpty()) {
			_menuItems.push_back({ id, text, icon, menuType });
			_changes.fire({});
		}
	} else if (type == u"remove_menu_item"_q) {
		const auto id = action.value(u"id"_q).toString();
		const auto removed = std::remove_if(
			_menuItems.begin(),
			_menuItems.end(),
			[&](const MenuItem &item) { return item.id == id; });
		if (removed != _menuItems.end()) {
			_menuItems.erase(removed, _menuItems.end());
			_changes.fire({});
		}
	} else if (type == u"send_request"_q) {
		const auto reqType = action.value(u"type"_q).toString();
		const auto params = action.value(u"params"_q).toObject();
		const auto callbackId = action.value(u"callback_id"_q).toInt();

		const auto chatVal = params.value(u"chat_id"_q);
		const auto raw = chatVal.isDouble()
			? static_cast<uint64>(chatVal.toDouble())
			: chatVal.toString().toULongLong();

		if (!raw) {
			sendEvent({
				{ u"event"_q, u"request_response"_q },
				{ u"callback_id"_q, callbackId },
				{ u"response"_q, QJsonValue::Null },
				{ u"error"_q, QJsonObject{ { u"text"_q, u"PEER_ID_INVALID"_q } } }
			});
			return;
		}

		const auto history = _session->data().history(PeerId(BareId(raw)));
		const auto peer = history->peer;

		if (reqType == u"channels_getParticipants"_q) {
			if (!peer->isChannel()) {
				sendEvent({
					{ u"event"_q, u"request_response"_q },
					{ u"callback_id"_q, callbackId },
					{ u"response"_q, QJsonValue::Null },
					{ u"error"_q, QJsonObject{ { u"text"_q, u"PEER_NOT_CHANNEL"_q } } }
				});
				return;
			}

			const auto channel = peer->asChannel();
			const auto offset = params.value(u"offset"_q).toInt();
			const auto limit = params.value(u"limit"_q).toInt();

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

				sendEvent({
					{ u"event"_q, u"request_response"_q },
					{ u"callback_id"_q, callbackId },
					{ u"response"_q, response },
					{ u"error"_q, QJsonValue::Null }
				});

			}).fail([=]() mutable {
				sendEvent({
					{ u"event"_q, u"request_response"_q },
					{ u"callback_id"_q, callbackId },
					{ u"response"_q, QJsonValue::Null },
					{ u"error"_q, QJsonObject{ { u"text"_q, u"API_ERROR"_q } } }
				});
			}).send();

		} else if (reqType == u"channels_editBanned"_q) {
			if (!peer->isChannel()) {
				return;
			}
			const auto channel = peer->asChannel();
			const auto userVal = params.value(u"user_id"_q);
			const auto userId = userVal.isDouble()
				? static_cast<uint64>(userVal.toDouble())
				: userVal.toString().toULongLong();

			const auto participant = _session->data().user(UserId(userId));
			const auto rights = ChannelData::KickedRestrictedRights(participant);

			_session->api().request(MTPchannels_EditBanned(
				channel->inputChannel(),
				participant->input(),
				RestrictionsToMTP(rights)
			)).done([=](const MTPUpdates &result) mutable {
				_session->api().applyUpdates(result);
				sendEvent({
					{ u"event"_q, u"request_response"_q },
					{ u"callback_id"_q, callbackId },
					{ u"response"_q, true },
					{ u"error"_q, QJsonValue::Null }
				});
			}).fail([=]() mutable {
				sendEvent({
					{ u"event"_q, u"request_response"_q },
					{ u"callback_id"_q, callbackId },
					{ u"response"_q, QJsonValue::Null },
					{ u"error"_q, QJsonObject{ { u"text"_q, u"API_ERROR"_q } } }
				});
			}).send();

		} else if (reqType == u"messages_deleteChatUser"_q) {
			if (!peer->isChat()) {
				return;
			}
			const auto chat = peer->asChat();
			const auto userVal = params.value(u"user_id"_q);
			const auto userId = userVal.isDouble()
				? static_cast<uint64>(userVal.toDouble())
				: userVal.toString().toULongLong();

			const auto participant = _session->data().user(UserId(userId));

			_session->api().request(MTPmessages_DeleteChatUser(
				MTP_flags(0),
				chat->inputChat(),
				participant->asUser()->inputUser()
			)).done([=](const MTPUpdates &result) mutable {
				_session->api().applyUpdates(result);
				sendEvent({
					{ u"event"_q, u"request_response"_q },
					{ u"callback_id"_q, callbackId },
					{ u"response"_q, true },
					{ u"error"_q, QJsonValue::Null }
				});
			}).fail([=]() mutable {
				sendEvent({
					{ u"event"_q, u"request_response"_q },
					{ u"callback_id"_q, callbackId },
					{ u"response"_q, QJsonValue::Null },
					{ u"error"_q, QJsonObject{ { u"text"_q, u"API_ERROR"_q } } }
				});
			}).send();
		}
	} else if (type == u"log"_q) {
		const auto text = action.value(u"text"_q).toString();
		LOG(("Plugins: %1").arg(text));
		_logEvents.fire_copy(text + '\n');
	}
}

bool Bridge::isRunning() const {
	return (_process.state() == QProcess::Running);
}

} // namespace Plugins
