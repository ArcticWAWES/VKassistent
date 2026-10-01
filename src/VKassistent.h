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

// ─── Callback-типы ─────────────────────────────────────────
using Callback       = std::function<void(long userId, String text)>;
using CallbackSimple = std::function<void()>;
using CallbackFull   = std::function<void(struct VKMessage&)>;

// ─── Цвета кнопок VK ───────────────────────────────────────
static const char* primary   = "primary";
static const char* secondary = "secondary";
static const char* positive  = "positive";
static const char* negative  = "negative";

// ─── Кнопка клавиатуры ─────────────────────────────────────
struct Button {
  String label;
  String color;
  Button(String l, String c = "secondary") : label(l), color(c) {}
};

// ─── Маркер разрыва ряда ───────────────────────────────────
struct LineBreak {};
inline LineBreak Line() { return LineBreak(); }

// ─── Преобразование аргументов в токены JSON ───────────────
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

// ─── Сборка клавиатуры ─────────────────────────────────────
template<typename... Args>
String createKeyboard(Args... args) {
  if (sizeof...(args) == 0) {
    return "{\"one_time\":false,\"buttons\":[]}";
  }

  String parts[] = { _kb_part(args)... };
  const int count = sizeof(parts) / sizeof(parts[0]);

  String json = "{\"one_time\":false,\"buttons\":[";

  bool rowOpen  = false;
  bool firstRow = true;
  int  inRow    = 0;

  for (int i = 0; i < count; i++) {
    if (parts[i] == "__LINE__") {
      if (rowOpen) {
        json += "]";
        rowOpen = false;
        inRow = 0;
      }
      continue;
    }

    if (inRow == 5) {
      json += "]";
      rowOpen = false;
      inRow = 0;
    }

    if (!rowOpen) {
      if (!firstRow) json += ",";
      json += "[";
      rowOpen = true;
      firstRow = false;
    } else {
      json += ",";
    }

    json += parts[i];
    inRow++;
  }

  if (rowOpen) json += "]";
  json += "]}";
  return json;
}

// ─── Вложение ──────────────────────────────────────────────
struct VKAttachment {
  String type;
  String url;
  String title;
  int    id      = 0;
  int    ownerId = 0;
  float  lat     = 0.0f;
  float  lon     = 0.0f;
};

// ─── Сообщение ─────────────────────────────────────────────
struct VKMessage {
  long   peerId    = 0;
  long   fromId    = 0;
  int    messageId = 0;
  String text;
  std::vector<VKAttachment> attachments;

  bool hasAttachment(const String& type) const {
    for (const auto& a : attachments) if (a.type == type) return true;
    return false;
  }
  bool hasPhoto()   const { return hasAttachment("photo");   }
  bool hasDoc()     const { return hasAttachment("doc");     }
  bool hasGeo()     const { return hasAttachment("geo");     }
  bool hasSticker() const { return hasAttachment("sticker"); }

  const VKAttachment* getAttachment(const String& type) const {
    for (const auto& a : attachments) if (a.type == type) return &a;
    return nullptr;
  }
  String getPhotoUrl()   const { auto a = getAttachment("photo"); return a ? a->url   : ""; }
  String getDocUrl()     const { auto a = getAttachment("doc");   return a ? a->url   : ""; }
  String getDocTitle()   const { auto a = getAttachment("doc");   return a ? a->title : ""; }
  float  getLat()        const { auto a = getAttachment("geo");   return a ? a->lat   : 0.0f; }
  float  getLon()        const { auto a = getAttachment("geo");   return a ? a->lon   : 0.0f; }
};

// ─── Зарегистрированная команда ────────────────────────────
struct RegisteredCommand {
  String         cmd;
  Callback       oldCb;
  CallbackSimple simpleCb;
  CallbackFull   fullCb;
};

// ─── Класс VKassistent ─────────────────────────────────────
class VKassistent {
public:
  VKassistent(String Token, String GroupID);

  void connectWIFI(String SSID, String PASSWORD);
  void begin();
  void loop();

  // ─── Хранилище ───────────────────────────────────────
  void useLittleFS();
  void useSD(int csPin);
  void useSD(int cs, int sck, int miso, int mosi);

  // ─── Регистрация текстовых команд ────────────────────
  void processMessage(String cmd, Callback callback);
  void onMessage(String cmd, CallbackSimple callback);
  void onMessage(String cmd, CallbackFull   callback);

  void proccesMessage(String cmd, Callback callback) {
    processMessage(cmd, callback);
  }

  // ─── Регистрация по типу вложения ────────────────────
  void onPhoto  (CallbackFull cb);
  void onDoc    (CallbackFull cb);
  void onGeo    (CallbackFull cb);
  void onSticker(CallbackFull cb);

  // ─── Отправка ────────────────────────────────────────
  void send(long UserID, String text);
  void sendWithKeyboard(long UserID, String text, String keyboard);
  void sendToLast(String text);

  // ─── Отправка вложений ───────────────────────────────
  void sendGeo(long peerId, float lat, float lon);
  void sendGeo(long peerId, const String& text, float lat, float lon);

  // ─── Отправка фото из FS ─────────────────────────────
  bool sendPhotoFromFS(long peerId, fs::FS& fs, const String& path);
  bool sendPhotoFromFS(long peerId, const String& text, fs::FS& fs, const String& path);

  // ─── Сохранение вложений ─────────────────────────────
  String savePhoto(const VKMessage& msg);
  String saveDoc(const VKMessage& msg);
  String saveAttachment(const VKMessage& msg, const String& path);
  String saveAttachmentToFS(const VKMessage& msg, fs::FS& fs, const String& path);

  String savePhotoAndReply(const VKMessage& msg);
  String saveDocAndReply(const VKMessage& msg);

  // ─── Админы ─────────────────────────────────────────
  void addAdmin(long UserID);
  bool isAdmin(long UserID);

private:
  String _Token;
  String _GroupID;

  std::vector<RegisteredCommand> _commands;
  std::vector<CallbackFull>      _photoCallbacks;
  std::vector<CallbackFull>      _docCallbacks;
  std::vector<CallbackFull>      _geoCallbacks;
  std::vector<CallbackFull>      _stickerCallbacks;
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

  void   _resetClient();
  void   _getLongPollServer();
  void   _pollLongPoll();
  void   _handleCommand(VKMessage& msg);
  String _urlencode(String str);
  void   _sendRequest(String url);

  void   _parseAttachments(JsonArray atts, std::vector<VKAttachment>& out);

  fs::FS& _getStorage();
  bool    _downloadToFS(fs::FS& fs, const String& url, const String& path);
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