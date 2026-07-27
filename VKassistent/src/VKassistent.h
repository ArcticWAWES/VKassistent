#ifndef VKassistent_h
#define VKassistent_h

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>

using Callback = std::function<void(long userId, String text)>;

static const char* positive = "positive";
static const char* negative = "negative";
static const char* primary = "primary";
static const char* secondary = "secondary";

struct Button {
  String label;
  String color;
  Button(String l, String c = "secondary") : label(l), color(c) {}
};

#define createKeyboard(...) \
  [](){ \
    Button items[] = {__VA_ARGS__}; \
    int count = sizeof(items) / sizeof(items[0]); \
    String json = "{\"one_time\":false,\"buttons\":["; \
    for (int i = 0; i < count; i++) { \
      if (i > 0) json += ","; \
      json += "[{\"action\":{\"type\":\"text\",\"label\":\"" + items[i].label + "\"},\"color\":\"" + items[i].color + "\"}]"; \
    } \
    json += "]}"; \
    return json; \
  }()

class VKassistent {
public:
  VKassistent(String Token, String GroupID);
  
  void connectWIFI(String SSID, String PASSWORD);
  void begin();
  void loop();
  void proccesMessage(String cmd, Callback callback);
  void send(long UserID, String text);
  void sendWithKeyboard(long UserID, String text, String keyboard);
  void addAdmin(long UserID);
  bool isAdmin(long UserID);
  
private:
  String _Token;
  String _GroupID;
  
  std::vector<String> _commands;
  std::vector<Callback> _callbacks;
  std::vector<long> _admins;
  
  int _lastMsgId = 0;
  unsigned long _lastCheck = 0;
  
  void _checkMessages();
  void _handleCommand(String text, long UserID);
  String _urlencode(String str);
  void _sendRequest(String url);
};

#endif