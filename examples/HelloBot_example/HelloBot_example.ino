/*
 * HelloBot — самый простой пример
 *
 * Что показывает:
 *   - Подключение к Wi-Fi и запуск бота
 *   - Приём текстовых команд
 *   - Ответы через send() и sendToLast()
 *   - Клавиатура с кнопками
 *   - Приветственное сообщение при команде "start"
 */

#include <VKassistent.h>

// Настройки — впиши свои значения
String SSID     = "__________";
String PASSWORD = "__________";
String Token    = "__________";
String GroupID  = "__________";

// Объект бота
VKassistent bot(Token, GroupID);

// Клавиатура. Line() переносит следующую кнопку на новую строку.
String menu = createKeyboard(
  Button("Привет", positive),
  Button("Помощь", primary),
  Line(),
  Button("Пока", negative)
);

void setup() {
  Serial.begin(115200);

  // Подключаемся к Wi-Fi и запускаем бота
  bot.connectWIFI(SSID, PASSWORD);
  bot.begin();

  // "start" — приветствие с меню
  bot.onMessage("start", [](VKMessage& msg) {
    String text = "Привет! Я бот на ESP32. 👋\n\n";
    text += "Выбери действие на клавиатуре ниже\n";
    text += "или напиши 'Помощь' для списка команд.";
    bot.sendWithKeyboard(msg.peerId, text, menu);
  });

  // "Привет" — отвечаем через sendToLast (в тот же чат)
  bot.onMessage("Привет", []() {
    bot.sendToLast("Привет-привет! 👋");
  });

  // "Помощь" — отвечаем через peerId
  bot.onMessage("Помощь", [](VKMessage& msg) {
    String text = "Доступные команды:\n";
    text += "start — меню\n";
    text += "Привет — приветствие\n";
    text += "Помощь — эта справка\n";
    text += "Пока — попрощаться";
    bot.send(msg.peerId, text);
  });

  // "Пока" — прощаемся с той же клавиатурой
  bot.onMessage("Пока", [](VKMessage& msg) {
    bot.sendWithKeyboard(msg.peerId, "До встречи! 👋", menu);
  });

  Serial.println("Готов. Напиши боту 'start'.");
}

void loop() {
  // bot.loop() вызывается постоянно — он ждёт сообщения от VK
  bot.loop();
}