#ifndef VKassistent_h
#define VKassistent_h

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <functional>
#include <vector>

// Callback-типы
using Callback       = std::function<void(long userId, String text)>;
using CallbackSimple = std::function<void()>;
using CallbackFull   = std::function<void(struct VKMessage&)>;

// Цвета кнопок VK
static const char* primary   = "primary";
static const char* secondary = "secondary";
static const char* positive  = "positive";
static const char* negative  = "negative";

// Кнопка клавиатуры
struct Button {
  String label;
  String color;
  Button(String l, String c = "secondary") : label(l), color(c) {}
};

// Маркер разрыва ряда
struct LineBreak {};
inline LineBreak Line() { return LineBreak(); }

inline String _kb_part(const Button& b) {
  String s = "{\"action\":{\"type\":\"text\",\"label\":\"";
  s += b.label;
  s += "\"},\"color\":\"";
  s += b.color;
  s += "\"}";
  return s;
}

inline String _kb_part(const LineBreak&) {
  return "__LINE__";
}

template<typename... Args>
String createKeyboard(Args... args) {
  if (sizeof...(args) == 0) return "{\"one_time\":false,\"buttons\":[]}";

  String parts[] = { _kb_part(args)... };
  const int count = sizeof(parts) / sizeof(parts[0]);

  String json = "{\"one_time\":false,\"buttons\":[";
  bool rowOpen = false, firstRow = true;
  int inRow = 0;

  for (int i = 0; i < count; i++) {
    if (parts[i] == "__LINE__") {
      if (rowOpen) { json += "]"; rowOpen = false; inRow = 0; }
      continue;
    }
    if (inRow == 5) { json += "]"; rowOpen = false; inRow = 0; }

    if (!rowOpen) {
      if (!firstRow) json += ",";
      json += "["; rowOpen = true; firstRow = false;
    } else json += ",";

    json += parts[i];
    inRow++;
  }
  if (rowOpen) json += "]";
  json += "]}";
  return json;
}

// Вложение
struct VKAttachment {
  String type;
  String url;
  String title;
  int    id      = 0;
  int    ownerId = 0;
  float  lat     = 0.0f;
  float  lon     = 0.0f;
};

// Сообщение
struct VKMessage {
  long   peerId    = 0;
  long   fromId    = 0;
  int    messageId = 0;
  String text;
  std::vector<VKAttachment> attachments;

  bool hasAttachment(const String& t) const {
    for (const auto& a : attachments) if (a.type == t) return true;
    return false;
  }
  bool hasPhoto()   const { return hasAttachment("photo");   }
  bool hasDoc()     const { return hasAttachment("doc");     }
  bool hasGeo()     const { return hasAttachment("geo");     }
  bool hasSticker() const { return hasAttachment("sticker"); }

  const VKAttachment* getAttachment(const String& t) const {
    for (const auto& a : attachments) if (a.type == t) return &a;
    return nullptr;
  }
  String getPhotoUrl()   const { auto a = getAttachment("photo"); return a ? a->url   : ""; }
  String getDocUrl()     const { auto a = getAttachment("doc");   return a ? a->url   : ""; }
  String getDocTitle()   const { auto a = getAttachment("doc");   return a ? a->title : ""; }
  float  getLat()        const { auto a = getAttachment("geo");   return a ? a->lat   : 0.0f; }
  float  getLon()        const { auto a = getAttachment("geo");   return a ? a->lon   : 0.0f; }
};

// Зарегистрированная команда
struct RegisteredCommand {
  String         cmd;
  Callback       oldCb;
  CallbackSimple simpleCb;
  CallbackFull   fullCb;
};

// Уровни логирования
enum VKLogLevel {
  VK_LOG_NONE  = 0,   // тишина
  VK_LOG_ERROR = 1,   // только ошибки
  VK_LOG_INFO  = 2,   // как сейчас (по умолчанию)
  VK_LOG_DEBUG = 3    // всё + отладка
};

// Класс VKassistent
class VKassistent {
public:
  VKassistent(String Token, String GroupID);

  void connectWIFI(String SSID, String PASSWORD);
  void begin();
  void loop();

  // Хранилище
  void useLittleFS();
  void useSD(int csPin);
  void useSD(int cs, int sck, int miso, int mosi);

  // Логирование
  void setLogLevel(VKLogLevel lvl) { _logLevel = lvl; }
  VKLogLevel getLogLevel() const { return _logLevel; }

  // Второй токен — пользовательский, нужен для загрузки фото
  // (photos.getMessagesUploadServer, photos.saveMessagesPhoto).
  // Без него sendPhotoFromFS вернёт error_code: 15.
  void setUserToken(String token) { _UserToken = token; }
  bool hasUserToken() const { return _UserToken.length() > 0; }

  // Автобан не-админов
  void setStrictAdmins(bool strict) { _strictAdmins = strict; }
  void setDenyMessage(const String& msg) { _denyMessage = msg; }

  // Регистрация команд
  void processMessage(String cmd, Callback callback);
  void onMessage(String cmd, CallbackSimple callback);
  void onMessage(String cmd, CallbackFull   callback);
  void onAnyMessage(CallbackFull cb);

  void proccesMessage(String cmd, Callback callback) {
    processMessage(cmd, callback);
  }

  // Вложения
  void onPhoto  (CallbackFull cb);
  void onDoc    (CallbackFull cb);
  void onGeo    (CallbackFull cb);
  void onSticker(CallbackFull cb);

  // Отправка (возвращают conversation_message_id или 0 при ошибке)
  long send(long UserID, String text);
  long sendWithKeyboard(long UserID, String text, String keyboard);
  long sendMenu(long peerId, const String& title, const String& keyboard);
  long sendToLast(String text);
  long sendGeo(long peerId, float lat, float lon);
  long sendGeo(long peerId, const String& text, float lat, float lon);

  // Редактирование ранее отправленного сообщения
  long editMessage(long peerId, long cmid, const String& newText);
  long editMessage(long peerId, long cmid, const String& newText, const String& keyboard);

  // Отправка фото из FS
  bool sendPhotoFromFS(long peerId, const String& path);
  bool sendPhotoFromFS(long peerId, const String& text, const String& path);

  // Сохранение
  String savePhoto(const VKMessage& msg);
  String saveDoc(const VKMessage& msg);
  String saveAttachment(const VKMessage& msg, const String& path);
  String savePhotoAndReply(const VKMessage& msg);
  String saveDocAndReply(const VKMessage& msg);

  // Админы
  void addAdmin(long UserID);
  bool isAdmin(long UserID);

private:
  String _Token;
  String _GroupID;
  String _UserToken;

  std::vector<RegisteredCommand> _commands;
  std::vector<CallbackFull>      _photoCallbacks;
  std::vector<CallbackFull>      _docCallbacks;
  std::vector<CallbackFull>      _geoCallbacks;
  std::vector<CallbackFull>      _stickerCallbacks;
  std::vector<CallbackFull>      _anyCallbacks;
  std::vector<long>              _admins;

  WiFiClientSecure _client;

  String _lpServer;
  String _lpKey;
  String _lpTs;
  bool   _lpReady = false;

  long _lastPeerId = 0;
  int  _wifiFailCount = 0;

  fs::FS* _storage     = nullptr;
  bool    _storageIsSD = false;

  // Логирование
  VKLogLevel _logLevel = VK_LOG_INFO;
  void   _logError(const String& msg);
  void   _logInfo (const String& msg);
  void   _logDebug(const String& msg);

  // Автобан не-админов
  bool   _strictAdmins = false;
  String _denyMessage  = "⛔ Нет прав";

  // Утилиты логирования в рамке
  void   _boxStart(const String& title);
  void   _boxEnd();

  void   _resetClient();
  void   _getLongPollServer();
  void   _pollLongPoll();
  void   _handleCommand(VKMessage& msg);
  String _urlencode(String str);
  String _sendRequest(String url);   // возвращает тело ответа, "" при ошибке

  void   _parseAttachments(JsonArray atts, std::vector<VKAttachment>& out);

  fs::FS& _getStorage();
  bool    _downloadToFS(const String& url, const String& path);
  void    _clearFS();
  void    _logStorage();
  String  _sanitizeName(String name);

  String  _photoFileName(const VKMessage& msg);
  String  _docFileName(const VKMessage& msg);

  String  _getPhotoUploadServer();
  String  _uploadPhotoToServer(const String& uploadUrl,
                               const uint8_t* buf, size_t len);
  bool    _saveMessagesPhoto(const String& server, const String& photo,
                             const String& hash, int& outId, int& outOwnerId);
  bool    _sendPhotoAttachment(long peerId, const String& text,
                               int photoId, int ownerId);
};

#endif
