#include "VKassistent.h"

static bool firstPrint = true;

// Forward-объявление: используется в sendGeo/sendWithKeyboard выше по тексту,
// тело — внизу файла.
static long _extractCmid(const String& response);

VKassistent::VKassistent(String Token, String GroupID) {
  _Token   = Token;
  _GroupID = GroupID;
}

// ─── Логирование по уровням ────────────────────────────────
void VKassistent::_logError(const String& msg) {
  if (_logLevel >= VK_LOG_ERROR) Serial.println(msg);
}
void VKassistent::_logInfo(const String& msg) {
  if (_logLevel >= VK_LOG_INFO) Serial.println(msg);
}
void VKassistent::_logDebug(const String& msg) {
  if (_logLevel >= VK_LOG_DEBUG) Serial.println(msg);
}

// ─── Утилиты логирования в рамке ───────────────────────────
void VKassistent::_boxStart(const String& title) {
  if (_logLevel < VK_LOG_INFO) return;
  Serial.println("================================");
  Serial.print("📡 VKassistent: ");
  Serial.println(title);
  Serial.println("--------------------------------");
}

void VKassistent::_boxEnd() {
  if (_logLevel < VK_LOG_INFO) return;
  Serial.println("================================");
}

// ─── Подключение к Wi-Fi ───────────────────────────────────
void VKassistent::connectWIFI(String SSID, String PASSWORD) {
  if (WiFi.status() == WL_CONNECTED) {
    if (firstPrint) {
      _boxStart("Wi-Fi");
      Serial.println("✅ Уже подключен");
      Serial.println("🌐 IP: " + WiFi.localIP().toString());
      _boxEnd();
      firstPrint = false;
    }
    return;
  }

  _boxStart("Wi-Fi подключение");
  Serial.print("📡 Подключаюсь к: ");
  Serial.println(SSID);
  Serial.print("⏳ Ждём: ");

  WiFi.begin(SSID.c_str(), PASSWORD.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ Wi-Fi подключен!");
    Serial.println("🌐 IP: " + WiFi.localIP().toString());
    firstPrint = false;
  } else {
    Serial.println("❌ Ошибка подключения!");
  }
  _boxEnd();
}

// ─── Инициализация ─────────────────────────────────────────
void VKassistent::begin() {
  randomSeed(esp_random());

  _boxStart("Инициализация");
  Serial.print("🔄 Reset reason: ");
  Serial.println((int)esp_reset_reason());

  if (!LittleFS.begin(true)) {
    Serial.println("⚠️ LittleFS не запустился");
  } else {
    Serial.println("✅ LittleFS OK");
    Serial.print("💾 total: "); Serial.println(LittleFS.totalBytes());
    Serial.print("💾 used:  "); Serial.println(LittleFS.usedBytes());
    Serial.print("💾 free:  "); Serial.println(LittleFS.totalBytes() - LittleFS.usedBytes());
  }
  _storage     = &LittleFS;
  _storageIsSD = false;
  _boxEnd();

  _getLongPollServer();
}

// ─── loop() ────────────────────────────────────────────────
void VKassistent::loop() {
  if (WiFi.status() != WL_CONNECTED) {
    _wifiFailCount++;
    _boxStart("Wi-Fi потерян");
    Serial.print("⚠️ Попытка: ");
    Serial.println(_wifiFailCount);

    if (_wifiFailCount % 5 == 0) {
      Serial.println("🔄 Полный сброс Wi-Fi...");
      WiFi.disconnect(true);
      delay(1000);
    } else {
      WiFi.disconnect();
      delay(100);
      WiFi.reconnect();
    }
    _boxEnd();

    delay(10000);
    return;
  }

  _wifiFailCount = 0;

  if (!_lpReady) {
    _getLongPollServer();
    delay(1000);
    return;
  }

  _pollLongPoll();
}

// ─── Хранилище: LittleFS ───────────────────────────────────
void VKassistent::useLittleFS() {
  if (!LittleFS.begin(true)) {
    _boxStart("LittleFS");
    Serial.println("❌ Не запустился");
    _boxEnd();
    return;
  }
  _storage     = &LittleFS;
  _storageIsSD = false;

  _boxStart("Хранилище");
  Serial.println("💾 Тип: LittleFS");
  _boxEnd();
}

// ─── Хранилище: SD ─────────────────────────────────────────
void VKassistent::useSD(int csPin) {
  useSD(csPin, 18, 19, 23);
}

void VKassistent::useSD(int cs, int sck, int miso, int mosi) {
  SPI.begin(sck, miso, mosi, cs);

  _boxStart("Хранилище: SD");

  if (!SD.begin(cs)) {
    Serial.println("❌ SD не найдена, остаёмся на LittleFS");
    _boxEnd();
    return;
  }

  if (SD.cardType() == CARD_NONE) {
    Serial.println("❌ Карта не вставлена");
    _boxEnd();
    return;
  }

  _storage     = &SD;
  _storageIsSD = true;

  Serial.println("💾 Тип: SD");
  Serial.print("📦 total: ");
  Serial.print(SD.totalBytes() / 1024 / 1024);
  Serial.println(" MB");
  _boxEnd();
}

// ─── _getStorage() ─────────────────────────────────────────
fs::FS& VKassistent::_getStorage() {
  if (_storage == nullptr) {
    _storage     = &LittleFS;
    _storageIsSD = false;
  }
  return *_storage;
}

// ─── _logStorage() ─────────────────────────────────────────
void VKassistent::_logStorage() {
  if (_storageIsSD) {
    Serial.print("💾 SD: used=");
    Serial.print(SD.usedBytes());
    Serial.print(" total=");
    Serial.println(SD.totalBytes());
  } else {
    Serial.print("💾 LittleFS: used=");
    Serial.print(LittleFS.usedBytes());
    Serial.print(" total=");
    Serial.println(LittleFS.totalBytes());
  }
}

// ─── _clearFS() ────────────────────────────────────────────
void VKassistent::_clearFS() {
  fs::FS& fs = _getStorage();

  _boxStart("Очистка FS");

  if (_storageIsSD) {
    Serial.print("💾 SD before: used=");
    Serial.print(SD.usedBytes());
    Serial.print(" free=");
    Serial.println(SD.totalBytes() - SD.usedBytes());
  } else {
    Serial.print("💾 LittleFS before: used=");
    Serial.print(LittleFS.usedBytes());
    Serial.print(" free=");
    Serial.println(LittleFS.totalBytes() - LittleFS.usedBytes());
  }

  Serial.println("🧹 Удаляю файлы...");

  File root = fs.open("/");
  if (!root) {
    Serial.println("⚠️ Не могу открыть корень FS");
    _boxEnd();
    return;
  }

  File f = root.openNextFile();
  int count = 0;
  while (f) {
    String name = f.name();
    bool isDir = f.isDirectory();
    f.close();
    if (isDir) {
      File sub = fs.open(name);
      if (sub) {
        File sf = sub.openNextFile();
        while (sf) {
          String sName = sf.name();
          sf.close();
          fs.remove(sName);
          count++;
          yield();
          sf = sub.openNextFile();
        }
        sub.close();
      }
      fs.rmdir(name);
    } else {
      if (fs.remove(name)) count++;
    }
    yield();
    f = root.openNextFile();
  }
  root.close();

  Serial.print("✅ Удалено файлов: ");
  Serial.println(count);

  if (_storageIsSD) {
    Serial.print("💾 SD after: free=");
    Serial.println(SD.totalBytes() - SD.usedBytes());
  } else {
    Serial.print("💾 LittleFS after: free=");
    Serial.println(LittleFS.totalBytes() - LittleFS.usedBytes());
  }

  _boxEnd();
}

// ─── _sanitizeName() ───────────────────────────────────────
String VKassistent::_sanitizeName(String name) {
  String out;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name.charAt(i);
    if (isalnum(c) || c == '.' || c == '_' || c == '-') out += c;
    else if (c == ' ') out += '_';
  }
  if (out.length() == 0) out = "file";
  if (out.length() > 32) out = out.substring(0, 32);
  return out;
}

// ─── Имена файлов ──────────────────────────────────────────
String VKassistent::_photoFileName(const VKMessage& msg) {
  String name = "photo_";
  name += String(msg.fromId);
  name += "_";
  name += String(millis());
  name += ".jpg";
  return name;
}

String VKassistent::_docFileName(const VKMessage& msg) {
  String title = msg.getDocTitle();
  if (title.length() == 0) {
    title = "doc_" + String(msg.fromId) + "_" + String(millis()) + ".bin";
  }
  return _sanitizeName(title);
}

// ─── savePhoto / saveDoc ───────────────────────────────────
String VKassistent::savePhoto(const VKMessage& msg) {
  if (!msg.hasPhoto()) return "";
  if (!_storageIsSD) _clearFS();
  _logStorage();

  String path = "/";
  if (_storageIsSD) {
    if (!SD.exists("/photos")) SD.mkdir("/photos");
    path += "photos/";
  }
  path += _photoFileName(msg);

  if (_downloadToFS(msg.getPhotoUrl(), path)) return path;
  return "";
}

String VKassistent::saveDoc(const VKMessage& msg) {
  if (!msg.hasDoc()) return "";
  if (!_storageIsSD) _clearFS();
  _logStorage();

  String path = "/";
  if (_storageIsSD) {
    if (!SD.exists("/docs")) SD.mkdir("/docs");
    path += "docs/";
  }
  path += _docFileName(msg);

  if (_downloadToFS(msg.getDocUrl(), path)) return path;
  return "";
}

String VKassistent::saveAttachment(const VKMessage& msg, const String& path) {
  if (msg.attachments.empty()) return "";
  String url = msg.attachments[0].url;
  if (url == "") return "";
  if (!_storageIsSD) _clearFS();
  if (_downloadToFS(url, path)) return path;
  return "";
}

String VKassistent::savePhotoAndReply(const VKMessage& msg) {
  String path = savePhoto(msg);
  if (path != "") {
    File f = _getStorage().open(path);
    size_t sz = f ? f.size() : 0;
    if (f) f.close();
    send(msg.peerId, "✅ Сохранено: " + path + " (" + String(sz) + " байт)");
  } else {
    send(msg.peerId, "❌ Не удалось сохранить фото");
  }
  return path;
}

String VKassistent::saveDocAndReply(const VKMessage& msg) {
  String path = saveDoc(msg);
  if (path != "") {
    File f = _getStorage().open(path);
    size_t sz = f ? f.size() : 0;
    if (f) f.close();
    send(msg.peerId, "✅ Сохранено: " + path + " (" + String(sz) + " байт)");
  } else {
    send(msg.peerId, "❌ Не удалось сохранить документ");
  }
  return path;
}

// ─── sendGeo ───────────────────────────────────────────────
long VKassistent::sendGeo(long peerId, float lat, float lon) {
  return sendGeo(peerId, "", lat, lon);
}

long VKassistent::sendGeo(long peerId, const String& text, float lat, float lon) {
  String url = "https://api.vk.com/method/messages.send";
  url += "?peer_id=" + String(peerId);
  url += "&message=" + _urlencode(text);
  url += "&lat=" + String(lat, 6);
  url += "&long=" + String(lon, 6);
  url += "&v=5.131";
  url += "&random_id=" + String(random(1000000, 9999999));
  url += "&access_token=" + _Token;
  return _extractCmid(_sendRequest(url));
}

// ─── sendPhotoFromFS ───────────────────────────────────────
bool VKassistent::sendPhotoFromFS(long peerId, const String& path) {
  return sendPhotoFromFS(peerId, "", path);
}

bool VKassistent::sendPhotoFromFS(long peerId, const String& text, const String& path) {
  fs::FS& fs = _getStorage();

  _boxStart("Отправка фото");

  File f = fs.open(path, FILE_READ);
  if (!f) {
    Serial.println("❌ Не могу открыть " + path);
    _boxEnd();
    return false;
  }

  size_t len = f.size();
  if (len == 0) {
    Serial.println("❌ Файл пуст");
    f.close();
    _boxEnd();
    return false;
  }

  Serial.print("📸 Файл: ");
  Serial.print(len);
  Serial.println(" байт");

  uint8_t* buf = (uint8_t*)malloc(len);
  if (!buf) {
    Serial.println("❌ Нет heap для буфера");
    f.close();
    _boxEnd();
    return false;
  }

  size_t read = f.read(buf, len);
  f.close();

  if (read != len) {
    Serial.println("❌ Прочитано не всё");
    free(buf);
    _boxEnd();
    return false;
  }

  if (buf[0] != 0xFF || buf[1] != 0xD8) {
    Serial.println("⚠️ Не JPEG");
  }

  String uploadUrl = _getPhotoUploadServer();
  if (uploadUrl == "") {
    Serial.println("❌ Не получил upload_url");
    free(buf);
    _boxEnd();
    return false;
  }
  Serial.println("📤 upload_url получен");

  String respJson = _uploadPhotoToServer(uploadUrl, buf, len);
  free(buf);

  if (respJson == "") {
    Serial.println("❌ Upload не удался");
    _boxEnd();
    return false;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, respJson);
  if (err) {
    Serial.println("❌ JSON upload error");
    _boxEnd();
    return false;
  }

  if (doc.containsKey("error")) {
    Serial.println("❌ VK upload error");
    _boxEnd();
    return false;
  }

  String server = doc["server"].as<String>();
  String photo  = doc["photo"].as<String>();
  String hash   = doc["hash"].as<String>();

  if (server == "" || photo == "" || hash == "") {
    Serial.println("❌ Пустые server/photo/hash");
    _boxEnd();
    return false;
  }
  Serial.println("📥 Фото загружено, сохраняю...");

  int photoId = 0;
  int ownerId = 0;
  if (!_saveMessagesPhoto(server, photo, hash, photoId, ownerId)) {
    Serial.println("❌ saveMessagesPhoto не удался");
    _boxEnd();
    return false;
  }
  Serial.print("✅ Сохранено: photo");
  Serial.print(ownerId);
  Serial.print("_");
  Serial.println(photoId);

  bool result = _sendPhotoAttachment(peerId, text, photoId, ownerId);
  _boxEnd();
  return result;
}

// ─── _getPhotoUploadServer ─────────────────────────────────
String VKassistent::_getPhotoUploadServer() {
  if (_UserToken.length() == 0) {
    _boxStart("Фото");
    Serial.println("❌ Не задан пользовательский токен (setUserToken)");
    _boxEnd();
    return "";
  }

  _resetClient();

  HTTPClient http;
  String url = "https://api.vk.com/method/photos.getMessagesUploadServer";
  url += "?access_token=" + _UserToken;
  url += "&v=5.131";

  http.begin(_client, url);
  int code = http.GET();

  if (code != 200) {
    Serial.print("❌ HTTP: ");
    Serial.println(code);
    if (code == -1) _client.stop();
    http.end();
    return "";
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(2048);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return "";

  if (doc.containsKey("error")) {
    Serial.println("❌ VK error: " + payload);
    return "";
  }

  return doc["response"]["upload_url"].as<String>();
}

// ─── _uploadPhotoToServer ──────────────────────────────────
String VKassistent::_uploadPhotoToServer(const String& uploadUrl,
                                          const uint8_t* buf, size_t len) {
  int protoPos = uploadUrl.indexOf("://");
  if (protoPos < 0) return "";
  int slashPos = uploadUrl.indexOf("/", protoPos + 3);
  if (slashPos < 0) return "";

  String host = uploadUrl.substring(protoPos + 3, slashPos);
  String path = uploadUrl.substring(slashPos);

  String fieldName = "photo";
  if (uploadUrl.indexOf("bulk_upload") >= 0) fieldName = "file1";

  _resetClient();

  if (!_client.connect(host.c_str(), 443)) {
    Serial.println("❌ connect fail");
    return "";
  }

  String boundary = "----VKBoundary" + String(millis());

  String head = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"" + fieldName + "\"; filename=\"photo.jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";

  size_t totalLen = head.length() + len + tail.length();

  _client.print("POST " + path + " HTTP/1.1\r\n");
  _client.print("Host: " + host + "\r\n");
  _client.print("Content-Type: multipart/form-data; boundary=" + boundary + "\r\n");
  _client.print("Content-Length: " + String(totalLen) + "\r\n");
  _client.print("Connection: close\r\n\r\n");

  _client.print(head);

  size_t sent = 0;
  while (sent < len) {
    size_t chunk = (len - sent) > 1024 ? 1024 : (len - sent);
    size_t w = _client.write(buf + sent, chunk);
    if (w == 0) {
      _client.stop();
      return "";
    }
    sent += w;
  }

  _client.print(tail);

  String rawResponse;
  bool headersDone = false;
  unsigned long timeout = millis() + 20000;

  while (millis() < timeout) {
    while (_client.available()) {
      char c = _client.read();
      rawResponse += c;
      if (!headersDone) {
        if (rawResponse.indexOf("\r\n\r\n") >= 0) headersDone = true;
      }
    }
    if (headersDone && !_client.connected()) break;
    if (headersDone && rawResponse.length() > 0) {
      delay(10);
      if (!_client.available()) break;
    }
    delay(1);
  }

  _client.stop();

  if (rawResponse.length() == 0) return "";

  int headerEnd = rawResponse.indexOf("\r\n\r\n");
  if (headerEnd < 0) return "";
  return rawResponse.substring(headerEnd + 4);
}

// ─── _saveMessagesPhoto ────────────────────────────────────
bool VKassistent::_saveMessagesPhoto(const String& server, const String& photo,
                                      const String& hash,
                                      int& outId, int& outOwnerId) {
  _resetClient();

  HTTPClient http;
  String url = "https://api.vk.com/method/photos.saveMessagesPhoto";
  url += "?server=" + _urlencode(server);
  url += "&photo=" + _urlencode(photo);
  url += "&hash=" + _urlencode(hash);
  url += "&access_token=" + _UserToken;
  url += "&v=5.131";

  http.begin(_client, url);
  int code = http.GET();

  if (code != 200) {
    if (code == -1) _client.stop();
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;

  if (doc.containsKey("error")) return false;

  JsonArray resp = doc["response"];
  if (resp.size() == 0) return false;

  outId      = resp[0]["id"]       | 0;
  outOwnerId = resp[0]["owner_id"] | 0;

  return outId != 0;
}

// ─── _sendPhotoAttachment ──────────────────────────────────
bool VKassistent::_sendPhotoAttachment(long peerId, const String& text,
                                        int photoId, int ownerId) {
  String attachment = "photo" + String(ownerId) + "_" + String(photoId);

  String url = "https://api.vk.com/method/messages.send";
  url += "?peer_id=" + String(peerId);
  url += "&message=" + _urlencode(text);
  url += "&attachment=" + _urlencode(attachment);
  url += "&v=5.131";
  url += "&random_id=" + String(random(1000000, 9999999));
  url += "&access_token=" + _Token;

  _sendRequest(url);
  return true;
}

// ─── _downloadToFS ─────────────────────────────────────────
bool VKassistent::_downloadToFS(const String& url, const String& path) {
  fs::FS& fs = _getStorage();

  _boxStart("Скачивание");

  Serial.print("⬇️ URL: ");
  Serial.println(url.substring(0, 80) + "...");

  _resetClient();

  HTTPClient http;
  http.begin(_client, url);
  http.setTimeout(20000);

  int code = http.GET();
  if (code != 200) {
    Serial.print("❌ HTTP: ");
    Serial.println(code);
    if (code == -1) _client.stop();
    http.end();
    _boxEnd();
    return false;
  }

  int contentLength = http.getSize();
  Serial.print("📦 Размер: ");
  Serial.print(contentLength);
  Serial.println(" байт");
  Serial.print("💾 Heap: ");
  Serial.println(ESP.getFreeHeap());

  File f = fs.open(path, FILE_WRITE);
  if (!f) {
    Serial.print("❌ Не могу открыть: ");
    Serial.println(path);
    http.end();
    _boxEnd();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t written = 0;
  uint8_t buf[1024];
  unsigned long lastData = millis();

  while (http.connected() && (contentLength < 0 || written < (size_t)contentLength)) {
    size_t avail = stream->available();
    if (avail) {
      size_t toRead = avail < sizeof(buf) ? avail : sizeof(buf);
      int n = stream->readBytes(buf, toRead);
      if (n <= 0) break;

      size_t w = f.write(buf, n);
      if (w != (size_t)n) break;
      written += w;
      lastData = millis();
    } else {
      if (millis() - lastData > 10000) {
        Serial.println("❌ Таймаут 10 сек");
        break;
      }
      yield();
      delay(10);
    }
  }

  f.close();
  http.end();

  Serial.print("✅ Записано: ");
  Serial.print(written);
  Serial.print(" → ");
  Serial.println(path);

  _boxEnd();
  return written > 0;
}

// ─── _resetClient ──────────────────────────────────────────
void VKassistent::_resetClient() {
  _client.stop();
  _client = WiFiClientSecure();
  _client.setInsecure();
}

// ─── _getLongPollServer ────────────────────────────────────
void VKassistent::_getLongPollServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  _resetClient();

  HTTPClient http;
  String url = "https://api.vk.com/method/groups.getLongPollServer";
  url += "?group_id=" + _GroupID;
  url += "&access_token=" + _Token;
  url += "&v=5.131";

  http.begin(_client, url);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, payload);

    if (err) {
      _boxStart("Long Poll");
      Serial.print("⚠️ JSON error: ");
      Serial.println(err.c_str());
      _boxEnd();
    } else if (doc.containsKey("error")) {
      _boxStart("Long Poll");
      Serial.println("❌ VK error: " + payload);
      _boxEnd();
    } else {
      _lpServer = doc["response"]["server"].as<String>();
      _lpKey    = doc["response"]["key"].as<String>();
      _lpTs     = doc["response"]["ts"].as<String>();
      _lpReady  = true;

      _boxStart("Long Poll");
      Serial.println("✅ Сервер получен");
      Serial.println("🔗 " + _lpServer);
      _boxEnd();
    }
  } else {
    if (code == -1) _client.stop();
  }

  http.end();
}

// ─── Регистрация команд ────────────────────────────────────
void VKassistent::processMessage(String cmd, Callback callback) {
  RegisteredCommand rc;
  rc.cmd   = cmd;
  rc.oldCb = callback;
  _commands.push_back(rc);
}

void VKassistent::onMessage(String cmd, CallbackSimple callback) {
  RegisteredCommand rc;
  rc.cmd      = cmd;
  rc.simpleCb = callback;
  _commands.push_back(rc);
}

void VKassistent::onMessage(String cmd, CallbackFull callback) {
  RegisteredCommand rc;
  rc.cmd    = cmd;
  rc.fullCb = callback;
  _commands.push_back(rc);
}

void VKassistent::onAnyMessage(CallbackFull cb) {
  _anyCallbacks.push_back(cb);
}

void VKassistent::onPhoto  (CallbackFull cb) { _photoCallbacks.push_back(cb);   }
void VKassistent::onDoc    (CallbackFull cb) { _docCallbacks.push_back(cb);     }
void VKassistent::onGeo    (CallbackFull cb) { _geoCallbacks.push_back(cb);     }
void VKassistent::onSticker(CallbackFull cb) { _stickerCallbacks.push_back(cb); }

// ─── Отправка ──────────────────────────────────────────────
long VKassistent::send(long UserID, String text) {
  return sendWithKeyboard(UserID, text, "");
}

long VKassistent::sendWithKeyboard(long UserID, String text, String keyboard) {
  String url = "https://api.vk.com/method/messages.send";
  url += "?peer_id=" + String(UserID);
  url += "&message=" + _urlencode(text);
  if (keyboard != "") url += "&keyboard=" + _urlencode(keyboard);
  url += "&v=5.131";
  url += "&random_id=" + String(random(1000000, 9999999));
  url += "&access_token=" + _Token;

  String response = _sendRequest(url);
  long cmid = _extractCmid(response);

  if (cmid == 0 && response.length() > 0) {
    _logError("⚠️ Не удалось извлечь cmid из ответа");
  }
  return cmid;
}

long VKassistent::sendMenu(long peerId, const String& title, const String& keyboard) {
  return sendWithKeyboard(peerId, title, keyboard);
}

long VKassistent::sendToLast(String text) {
  if (_lastPeerId == 0) {
    _boxStart("sendToLast");
    Serial.println("⚠️ Вне колбэка");
    _boxEnd();
    return 0;
  }
  return send(_lastPeerId, text);
}

// ─── Админы ────────────────────────────────────────────────
void VKassistent::addAdmin(long UserID) { _admins.push_back(UserID); }

bool VKassistent::isAdmin(long UserID) {
  for (long id : _admins) if (id == UserID) return true;
  return false;
}

// ─── _pollLongPoll ─────────────────────────────────────────
void VKassistent::_pollLongPoll() {
  _client.setInsecure();

  HTTPClient http;
  String url = _lpServer;
  url += "?act=a_check&key=" + _lpKey + "&ts=" + _lpTs + "&wait=25";

  http.setTimeout(30000);
  http.begin(_client, url);
  int code = http.GET();

  if (code != 200) {
    if (code != -1 && code != -11) {
      _boxStart("Long Poll");
      Serial.print("❌ HTTP: ");
      Serial.println(code);
      _boxEnd();
    }
    if (code == -1) _client.stop();
    http.end();
    delay(1000);
    return;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return;

  if (doc.containsKey("failed")) {
    int failed = doc["failed"];
    if (failed == 1) {
      _lpTs = doc["ts"].as<String>();
    } else if (failed == 2) {
      _lpReady = false;
    }
    return;
  }

  if (doc.containsKey("ts")) _lpTs = doc["ts"].as<String>();

  JsonArray updates = doc["updates"];
  for (JsonObject update : updates) {
    if (update["type"].as<String>() != "message_new") continue;

    JsonObject m = update["object"]["message"];

    VKMessage msg;
    msg.peerId    = m["peer_id"] | 0;
    msg.fromId    = m["from_id"] | 0;
    msg.messageId = m["id"]      | 0;
    msg.text      = m["text"].as<String>();

    if (m.containsKey("attachments")) {
      _parseAttachments(m["attachments"].as<JsonArray>(), msg.attachments);
    }

    _handleCommand(msg);
  }
}

// ─── _handleCommand ────────────────────────────────────────
void VKassistent::_handleCommand(VKMessage& msg) {
  long savedPeer = _lastPeerId;
  _lastPeerId = msg.peerId;

  if (_strictAdmins && !isAdmin(msg.fromId)) {
    if (_denyMessage.length() > 0) send(msg.peerId, _denyMessage);
    _lastPeerId = savedPeer;
    return;
  }

  if (msg.hasPhoto())   for (auto& cb : _photoCallbacks)   cb(msg);
  if (msg.hasDoc())     for (auto& cb : _docCallbacks)     cb(msg);
  if (msg.hasGeo())     for (auto& cb : _geoCallbacks)     cb(msg);
  if (msg.hasSticker()) for (auto& cb : _stickerCallbacks) cb(msg);

  if (msg.text.length() > 0) {
    bool matched = false;
    for (auto& rc : _commands) {
      if (rc.cmd != msg.text) continue;
      matched = true;
      if (rc.oldCb)    rc.oldCb(msg.peerId, msg.text);
      if (rc.simpleCb) rc.simpleCb();
      if (rc.fullCb)   rc.fullCb(msg);
    }

    if (!matched) {
      for (auto& cb : _anyCallbacks) cb(msg);
    }
  }

  _lastPeerId = savedPeer;
}

// ─── _parseAttachments ─────────────────────────────────────
void VKassistent::_parseAttachments(JsonArray atts, std::vector<VKAttachment>& out) {
  for (JsonObject a : atts) {
    VKAttachment att;
    att.type = a["type"].as<String>();

    if (att.type == "photo") {
      JsonObject p = a["photo"];
      att.id      = p["id"]       | 0;
      att.ownerId = p["owner_id"] | 0;

      JsonArray sizes = p["sizes"];
      String bestUrl = "";
      for (JsonObject s : sizes) {
        if (s["type"].as<String>() == "x") {
          bestUrl = s["url"].as<String>();
          break;
        }
      }
      if (bestUrl == "" && sizes.size() > 0) {
        bestUrl = sizes[sizes.size() - 1]["url"].as<String>();
      }
      att.url = bestUrl;
    }
    else if (att.type == "doc") {
      JsonObject d = a["doc"];
      att.id      = d["id"]       | 0;
      att.ownerId = d["owner_id"] | 0;
      att.url     = d["url"].as<String>();
      att.title   = d["title"].as<String>();
    }
    else if (att.type == "geo") {
      JsonObject g = a["geo"];
      JsonObject c = g["coordinates"];
      att.lat = c["latitude"]  | 0.0f;
      att.lon = c["longitude"] | 0.0f;
    }
    else if (att.type == "sticker") {
      att.id = a["sticker"]["sticker_id"] | 0;
    }

    out.push_back(att);
  }
}

// ─── _extractCmid ──────────────────────────────────────────
static long _extractCmid(const String& response) {
  if (response.length() == 0) return 0;
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, response)) return 0;
  if (doc.containsKey("error")) return 0;

  JsonVariant resp = doc["response"];
  if (resp.is<JsonArray>()) {
    JsonArray arr = resp.as<JsonArray>();
    if (arr.size() == 0) return 0;
    if (arr[0].is<long>()) return arr[0].as<long>();
    JsonObject o = arr[0].as<JsonObject>();
    if (o.containsKey("conversation_message_id"))
      return o["conversation_message_id"].as<long>();
    return o["message_id"] | 0;
  }
  if (resp.is<JsonObject>()) {
    JsonObject o = resp.as<JsonObject>();
    if (o.containsKey("conversation_message_id"))
      return o["conversation_message_id"].as<long>();
    return o["message_id"] | 0;
  }
  return resp | 0L;
}

// ─── _sendRequest ──────────────────────────────────────────
String VKassistent::_sendRequest(String url) {
  int attempts = 0;
  int code = -1;
  String response = "";

  while (attempts < 3 && code != 200) {
    _resetClient();

    HTTPClient http;
    http.begin(_client, url);
    code = http.GET();

    if (code == 200) {
      response = http.getString();
      if (response.indexOf("\"error\"") != -1) {
        if (_logLevel >= VK_LOG_ERROR) {
          _boxStart("VK ошибка");
          Serial.println("❌ " + response);
          _boxEnd();
        }
        response = "";
      }
    } else {
      if (code != -1 && code != -11) {
        if (_logLevel >= VK_LOG_ERROR) {
          _boxStart("VK отправка");
          Serial.print("❌ Попытка ");
          Serial.print(attempts + 1);
          Serial.print(": HTTP ");
          Serial.println(code);
          _boxEnd();
        }
      }
      if (code == -1) _client.stop();
      delay(500);
    }

    http.end();
    attempts++;
  }

  if (code != 200) {
    if (_logLevel >= VK_LOG_ERROR) {
      _boxStart("VK отправка");
      Serial.println("❌ Не отправлено после 3 попыток");
      _boxEnd();
    }
    return "";
  }

  return response;
}

// ─── editMessage ───────────────────────────────────────────
long VKassistent::editMessage(long peerId, long cmid, const String& newText) {
  return editMessage(peerId, cmid, newText, "");
}

long VKassistent::editMessage(long peerId, long cmid, const String& newText,
                              const String& keyboard) {
  if (cmid <= 0) {
    _boxStart("editMessage");
    Serial.println("⚠️ cmid = 0, нечего редактировать");
    _boxEnd();
    return 0;
  }

  String url = "https://api.vk.com/method/messages.edit";
  url += "?peer_id=" + String(peerId);
  url += "&conversation_message_id=" + String(cmid);
  url += "&message=" + _urlencode(newText);
  if (keyboard != "") url += "&keyboard=" + _urlencode(keyboard);
  url += "&v=5.131";
  url += "&access_token=" + _Token;

  String response = _sendRequest(url);
  if (response.length() == 0) return 0;

  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, response)) return 0;
  return doc["response"] | 0;
}

// ─── _urlencode ────────────────────────────────────────────
String VKassistent::_urlencode(String str) {
  String encoded = "";
  for (size_t i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == ' ') encoded += '+';
    else if (isalnum(c)) encoded += c;
    else {
      char hex[4];
      sprintf(hex, "%%%02X", (unsigned char)c);
      encoded += hex;
    }
  }
  return encoded;
}
