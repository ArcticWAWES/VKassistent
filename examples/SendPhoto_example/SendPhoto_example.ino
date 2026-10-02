/*
 * SendPhoto — отправка фото из памяти
 *
 * Что показывает:
 *   - Как бот принимает фото и сохраняет
 *   - Как бот отправляет фото обратно
 *   - Приветствие по команде "start"
 *
 * ВАЖНО:
 *   Для sendPhotoFromFS() нужен ПОЛЬЗОВАТЕЛЬСКИЙ токен VK
 *   (не токен сообщества!).
 *
 *   Как получить пользовательский токен:
 *   1. Зайти на https://vkhost.github.io/
 *   2. Выбрать "VK Admin" или "Kate Mobile"
 *   3. Разрешить
 *   4. Скопировать токен из URL (после access_token=)
 */

#include <VKassistent.h>

// Настройки — впиши свои значения
String SSID      = "__________";
String PASSWORD  = "__________";
String Token     = "__________";  // токен сообщества
String GroupID   = "__________";
String UserToken = "__________";  // пользовательский токен (для фото)

VKassistent bot(Token, GroupID);

void setup() {
  Serial.begin(115200);

  bot.connectWIFI(SSID, PASSWORD);
  bot.begin();

  // "start" — приветствие
  bot.onMessage("start", [](VKMessage& msg) {
    String text = "Привет! Я умею хранить и отправлять фото. 📸\n\n";
    text += "1. Пришли мне фото — я сохраню.\n";
    text += "2. Напиши 'Отправь' — верну фото обратно.\n";
    text += "3. Напиши 'Файлы' — покажу список.";
    bot.send(msg.peerId, text);
  });

  // Пришло фото — сохраняем
  bot.onPhoto([](VKMessage& msg) {
    Serial.println("Получено фото, сохраняю");
    bot.savePhotoAndReply(msg);
  });

  // "Отправь" — ищем фото в памяти и отправляем обратно
  bot.onMessage("Отправь", [](VKMessage& msg) {
    // Ищем первый .jpg
    File root = LittleFS.open("/");
    File f = root.openNextFile();
    String path = "";

    while (f) {
      String name = f.name();
      if (name.endsWith(".jpg")) {
        path = name;
        if (!path.startsWith("/")) path = "/" + path;
        f.close();
        break;
      }
      f.close();
      f = root.openNextFile();
    }
    root.close();

    if (path == "") {
      bot.send(msg.peerId, "❌ Нет сохранённых фото.\nСначала пришли фото боту.");
      return;
    }

    Serial.println("Отправляю: " + path);
    bot.sendPhotoFromFS(msg.peerId, "📸 Твоё фото:", LittleFS, path);
  });

  // "Файлы" — список файлов в памяти
  bot.onMessage("Файлы", [](VKMessage& msg) {
    String text = "📁 Файлы в памяти:\n";

    File root = LittleFS.open("/");
    File f = root.openNextFile();
    int count = 0;

    while (f) {
      text += "• " + String(f.name()) + " (" + String(f.size()) + " байт)\n";
      count++;
      f.close();
      f = root.openNextFile();
    }
    root.close();

    if (count == 0) text += "(пусто)";
    bot.send(msg.peerId, text);
  });

  Serial.println("Готов. Напиши боту 'start'.");
}

void loop() {
  bot.loop();
}