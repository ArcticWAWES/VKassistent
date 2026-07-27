#include <VKassistent.h>

String SSID = "__________";            // Имя Wi-Fi                   |       Wi-Fi name  
String PASSWORD = "__________";        // Пароль Wi-Fi                |       Wi-Fi password
String Token = "__________";           // токен VK                    |       VK token
String GroupID = "__________";         // ID группы                   |       Group ID

long MyID = __________;                // Администраторский VK ID     |       Administrator VK ID   
long FriendID = __________;            // Администраторский VK ID     |       Administrator VK ID  

VKassistent VKassistent(Token, GroupID);
const int LED_PIN = 2;                 // Встроенный светодиод        |       Built-in LED

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  VKassistent.connectWIFI(SSID, PASSWORD);     // Подключаем Wi-Fi            |       Connecting to Wi-Fi    
  VKassistent.begin();                         // Запускаем бота              |       launching the bot
  VKassistent.addAdmin(MyID);                  // Добавляем админа            |       Adding an administrator
  VKassistent.addAdmin(FriendID);              // Добавляем админа            |       Adding an administrator
  
  // Создаём клавиатуру | Create a keyboard
  String menu = createKeyboard(
    Button("💡🟢", positive),
    Button("💡🔴", negative)
  );
  
  // Команда "💡 ВКЛ" | First command
  VKassistent.proccesMessage("💡🟢", [](long UserID, String text) {
    digitalWrite(LED_PIN, HIGH);
    VKassistent.send(UserID, "💡🟢\n\nКомманда выполнена!\nCommand executed!");
  });
  
  // Команда "💡 ВЫКЛ"  | Second command
  VKassistent.proccesMessage("💡🔴", [](long UserID, String text) {
    digitalWrite(LED_PIN, LOW);
    VKassistent.send(UserID, "💡🔴\n\nКомманда выполнена!\nCommand executed!");
  });
  
  // Приветствие | Greeting
  VKassistent.sendWithKeyboard(MyID, "by Arctic_WAWES\nRU: Бот запущен!\nНажми кнопку:\nEN: Bot has been launched!\nPress the button:", menu);
}

void loop() {
  VKassistent.loop();  // ← не забывай! Запускает цикл  |   Don't forget! Starts the loop
}
