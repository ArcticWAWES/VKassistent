/*
 * ReceiveAttachments — приём вложений
 *
 * Что показывает:
 *   - Приём фото и сохранение в LittleFS
 *   - Приём документов
 *   - Приём гео-метки
 *   - Приём стикера
 *   - Приветствие по команде "start"
 *
 * ВАЖНО:
 *   При сохранении в LittleFS все старые файлы удаляются.
 *   Внутренняя память ESP32 ~1.4 МБ — хватит на 1-2 фото.
 */

#include <VKassistent.h>

// Настройки — впиши свои значения
String SSID     = "__________";
String PASSWORD = "__________";
String Token    = "__________";
String GroupID  = "__________";

VKassistent bot(Token, GroupID);

void setup() {
  Serial.begin(115200);

  bot.connectWIFI(SSID, PASSWORD);
  bot.begin();

  // "start" — приветствие
  bot.onMessage("start", [](VKMessage& msg) {
    String text = "Привет! Я принимаю вложения. 📎\n\n";
    text += "Что можно прислать:\n";
    text += "• фото — сохраню в память\n";
    text += "• документ — сохраню\n";
    text += "• гео-метку — покажу координаты\n";
    text += "• стикер — покажу ID\n\n";
    text += "Напиши 'Файлы' — покажу список.";
    bot.send(msg.peerId, text);
  });

  // Пришло фото — сохраняем и отвечаем
  bot.onPhoto([](VKMessage& msg) {
    Serial.println("Получено фото");
    bot.savePhotoAndReply(msg);
  });

  // Пришёл документ — сохраняем и отвечаем
  bot.onDoc([](VKMessage& msg) {
    Serial.println("Получен документ: " + msg.getDocTitle());
    bot.saveDocAndReply(msg);
  });

  // Пришла гео-метка — отправляем координаты обратно
  bot.onGeo([](VKMessage& msg) {
    String text = "📍 Твои координаты:\n";
    text += "Широта:  " + String(msg.getLat(), 6) + "\n";
    text += "Долгота: " + String(msg.getLon(), 6);
    bot.send(msg.peerId, text);
  });

  // Пришёл стикер — показываем его ID
  bot.onSticker([](VKMessage& msg) {
    int id = msg.attachments[0].id;
    bot.send(msg.peerId, "🎨 Стикер id: " + String(id));
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

    if (count == 0) text += "(пусто)\n";

    text += "\n💾 Свободно: ";
    text += String((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024);
    text += " КБ";

    bot.send(msg.peerId, text);
  });

  Serial.println("Готов. Напиши боту 'start'.");
}

void loop() {
  bot.loop();
}