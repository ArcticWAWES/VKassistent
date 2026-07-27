#include "VKassistent.h"

static bool firstPrint = true;

VKassistent::VKassistent(String Token, String GroupID) {
  _Token = Token;
  _GroupID = GroupID;
}

void VKassistent::connectWIFI(String SSID, String PASSWORD) {
  if (WiFi.status() == WL_CONNECTED) {
    if (firstPrint) {
      Serial.println("✅ Wi-Fi подключен! ");
      Serial.println("🌐 IP: " + WiFi.localIP().toString());
      firstPrint = false;
    }
    return;
  }
  
  Serial.print("📡 Подключение к Wi-Fi");
  WiFi.begin(SSID.c_str(), PASSWORD.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Wi-Fi подключен!");
    Serial.println("🌐 IP: " + WiFi.localIP().toString());
    firstPrint = false;
  } else {
    Serial.println("\n❌ Ошибка подключения!");
  }
}

void VKassistent::begin() {
  _lastMsgId = 0;
}

void VKassistent::loop() {
  if (millis() - _lastCheck > 2000) {
    _checkMessages();
    _lastCheck = millis();
  }
}

void VKassistent::proccesMessage(String cmd, Callback callback) {
  _commands.push_back(cmd);
  _callbacks.push_back(callback);
}

void VKassistent::send(long UserID, String text) {
  sendWithKeyboard(UserID, text, "");
}

void VKassistent::sendWithKeyboard(long UserID, String text, String keyboard) {
  String url = "https://api.vk.com/method/messages.send";
  url += "?user_id=" + String(UserID);
  url += "&message=" + _urlencode(text);
  if (keyboard != "") {
    url += "&keyboard=" + _urlencode(keyboard);
  }
  url += "&v=5.131";
  url += "&random_id=" + String(random(1000000, 9999999));
  url += "&access_token=" + _Token;
  
  _sendRequest(url);
}

void VKassistent::addAdmin(long UserID) {
  _admins.push_back(UserID);
}

bool VKassistent::isAdmin(long UserID) {
  for (long id : _admins) {
    if (id == UserID) return true;
  }
  return false;
}

// ============================================================
//  _checkMessages() — С ПЕРЕСОЗДАНИЕМ КЛИЕНТА
// ============================================================
void VKassistent::_checkMessages() {
  // Пересоздаём клиент при каждом вызове
  WiFiClientSecure *client = new WiFiClientSecure();
  client->setInsecure();
  
  HTTPClient http;
  String url = "https://api.vk.com/method/messages.getConversations";
  url += "?access_token=" + _Token;
  url += "&v=5.131";
  url += "&count=20";
  
  http.begin(*client, url);
  int code = http.GET();
  
  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);
    
    JsonArray items = doc["response"]["items"];
    for (JsonObject item : items) {
      JsonObject msg = item["last_message"];
      int id = msg["id"];
      long from = msg["from_id"];
      String text = msg["text"];
      
      if (id > _lastMsgId) {
        _lastMsgId = id;
        _handleCommand(text, from);
      }
    }
  } else {
    if (code != -1 && code != -11) {
      Serial.print("❌ _checkMessages() HTTP код: ");
      Serial.println(code);
    }
  }
  
  http.end();
  delete client;  // Очищаем память
}

void VKassistent::_handleCommand(String text, long UserID) {
  for (int i = 0; i < _commands.size(); i++) {
    if (text == _commands[i]) {
      _callbacks[i](UserID, text);
      return;
    }
  }
}

// ============================================================
//  _sendRequest() — С ПЕРЕСОЗДАНИЕМ КЛИЕНТА
// ============================================================
void VKassistent::_sendRequest(String url) {
  int attempts = 0;
  int code = -1;
  
  while (attempts < 3 && code != 200) {
    WiFiClientSecure *client = new WiFiClientSecure();
    client->setInsecure();
    
    HTTPClient http;
    http.begin(*client, url);
    
    code = http.GET();
    
    if (code == 200) {
      String response = http.getString();
      if (response.indexOf("error") != -1) {
        Serial.println("❌ ОШИБКА VK: " + response);
      }
    } else {
      if (code != -1 && code != -11) {
        Serial.print("❌ Попытка ");
        Serial.print(attempts + 1);
        Serial.print(": HTTP код ");
        Serial.println(code);
      }
      delay(500);
    }
    
    http.end();
    delete client;  // Очищаем память
    attempts++;
  }
  
  if (code != 200) {
    Serial.println("❌ Ошибка отправки после 3 попыток!");
  }
}

String VKassistent::_urlencode(String str) {
  String encoded = "";
  for (int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == ' ') {
      encoded += '+';
    } else if (isalnum(c)) {
      encoded += c;
    } else {
      char hex[4];
      sprintf(hex, "%%%02X", (unsigned char)c);
      encoded += hex;
    }
  }
  return encoded;
}