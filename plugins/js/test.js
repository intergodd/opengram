opengram.registerPlugin({
    name: "Тестовый плагин JS",
    version: "1.0.0",
    description: "Простой визуальный плагин для тестирования меню",
    author: "Antigravity",

    onLoad: function() {
        opengram.log("Тестовый плагин JS успешно загружен!");
        opengram.addMenuItem("js_test_menu_item", "Тест", "msg_bots_solar");
    },

    onUnload: function() {
        opengram.log("Тестовый плагин JS выгружен.");
    },

    onMenuItemClick: function(itemId, chatId) {
        if (itemId === "js_test_menu_item") {
            opengram.log("Нажат пункт меню Тест в чате " + chatId);
            opengram.sendMessage(chatId, "Кнопка 'Тест' успешно сработала в чате! 🎉");
        }
    }
});
