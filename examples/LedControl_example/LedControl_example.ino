/*
 * LedControl — управление светодиодом через VK
 *
 * Что показывает:
 *   - Управление пином через команды
 *   - Клавиатура с кнопками ВКЛ / ВЫКЛ / Статус
 *   - Приветствие с меню по команде "start"
 *
 * Схема:
 *   Светодиод → GPIO 2 (встроенный на большинстве ESP32)
 */

#include <VKassistent.h>

// Настройки — впиши свои значения
String SSID     = "__________";
String PASSWORD = "__________";
String Token    = "__________";
String GroupID  = "__________";

// Пин встроенного светодиода
#define LED_PIN 2

VKassistent bot(Token, GroupID);

// Состояние светодиода
bool ledState = false;

// Клавиатура управления
String menu = createKeyboard(
  Button("ВКЛ", positive),
  Button("ВЫКЛ", negative),
  Line(),
  Button("Статус", primary),
  Button("Меню", secondary)
);

void setup() {
  Serial.begin(115200);

  // Настраиваем пин светодиода
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  bot.connectWIFI(SSID, PASSWORD);
  bot.begin();

  // "start" — приветствие с меню
  bot.onMessage("start", [](VKMessage& msg) {
    String text = "Привет! Я управляю светодиодом. 💡\n\n";
    text += "Нажимай кнопки ниже.";
    bot.sendWithKeyboard(msg.peerId, text, menu);
  });

  // "Меню" — показываем клавиатуру
  bot.onMessage("Меню", [](VKMessage& msg) {
    bot.sendWithKeyboard(msg.peerId, "Управление светодиодом:", menu);
  });

  // "ВКЛ" — включаем светодиод
  bot.onMessage("ВКЛ", [](VKMessage& msg) {
    digitalWrite(LED_PIN, HIGH);
    ledState = true;
    Serial.println("Светодиод включён");
    bot.sendWithKeyboard(msg.peerId, "🔛 Светодиод включён", menu);
  });

  // "ВЫКЛ" — выключаем светодиод
  bot.onMessage("ВЫКЛ", [](VKMessage& msg) {
    digitalWrite(LED_PIN, LOW);
    ledState = false;
    Serial.println("Светодиод выключен");
    bot.sendWithKeyboard(msg.peerId, "🔴 Светодиод выключен", menu);
  });

  // "Статус" — показываем состояние
  bot.onMessage("Статус", [](VKMessage& msg) {
    String text = ledState ? "💡 Светодиод ВКЛЮЧЁН" : "🌑 Светодиод ВЫКЛЮЧЕН";
    bot.sendWithKeyboard(msg.peerId, text, menu);
  });

  Serial.println("Готов. Напиши боту 'start'.");
}

void loop() {
  bot.loop();
}