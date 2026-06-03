opengram.registerPlugin({
    name: "JS Модератор",
    version: "1.1.0",
    description: "Нативный плагин модерации на JavaScript",
    author: "Antigravity",

    onLoad: function() {
        opengram.log("Инициализация плагина JS Модератор...");
        opengram.addMenuItem("js_cleanup_deleted", "Удалить удаленные аккаунты (JS)", "msg_user_remove");
        opengram.addMenuItem("js_cleanup_bots", "Удалить ботов (JS)", "msg_bots_solar");
    },

    onUnload: function() {
        opengram.log("Плагин JS Модератор выгружен.");
    },

    onSendMessage: function(chatId, text, replyToId, replyPath) {
        if (text === ".js_test") {
            opengram.log("Перехвачена команда .js_test в чате " + chatId);
            opengram.sendMessage(chatId, "Привет! Это сообщение отправлено из скриптового JavaScript-плагина! 🚀", replyToId);
            return false;
        }
        return true;
    },

    onMenuItemClick: function(itemId, chatId) {
        if (itemId === "js_cleanup_deleted") {
            opengram.log("JS Модератор: Запущена очистка удаленных аккаунтов в чате " + chatId);
            
            opengram.sendRequest("channels_getParticipants", { chat_id: chatId, offset: 0, limit: 100 }, function(response, error) {
                if (error) {
                    opengram.log("Ошибка API: " + error.text);
                    return;
                }

                var users = response.users;
                var deletedCount = 0;

                for (var i = 0; i < users.length; i++) {
                    var user = users[i];
                    if (user.deleted) {
                        deletedCount++;
                        opengram.log("Обнаружен удаленный аккаунт (ID: " + user.id + "), исключаем...");
                        
                        opengram.sendRequest("channels_editBanned", { chat_id: chatId, user_id: user.id }, function(res, err) {
                            if (err) {
                                opengram.log("Не удалось исключить " + user.id + ": " + err.text);
                            }
                        });
                    }
                }
                opengram.log("Очистка завершена. Исключено аккаунтов: " + deletedCount);
            });
        } 
        else if (itemId === "js_cleanup_bots") {
            opengram.log("JS Модератор: Запущена очистка ботов в чате " + chatId);

            opengram.sendRequest("channels_getParticipants", { chat_id: chatId, offset: 0, limit: 100 }, function(response, error) {
                if (error) {
                    opengram.log("Ошибка API: " + error.text);
                    return;
                }

                var users = response.users;
                var botsCount = 0;

                for (var i = 0; i < users.length; i++) {
                    var user = users[i];
                    if (user.bot) {
                        botsCount++;
                        opengram.log("Обнаружен бот: " + user.first_name + " (ID: " + user.id + "), исключаем...");
                        
                        opengram.sendRequest("channels_editBanned", { chat_id: chatId, user_id: user.id }, function(res, err) {
                            if (err) {
                                opengram.log("Не удалось исключить бота " + user.id + ": " + err.text);
                            }
                        });
                    }
                }
                opengram.log("Очистка завершена. Исключено ботов: " + botsCount);
            });
        }
    }
});
