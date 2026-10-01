#include "VKassistent.h"

static bool firstPrint = true;

VKassistent::VKassistent(String Token, String GroupID) {
  _Token   = Token;
  _GroupID = GroupID;
}

// ─── Подключение к Wi-Fi ───────────────────────────────────
void VKassistent::connectWIFI(String SSID, String PASSWORD) {
  if (WiFi.status() == WL_CONNECTED) {
    if (firstPrint) {
      Serial.println("✅ Wi-Fi подключен!");
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

// ─── Инициализация ─────────────────────────────────────────
void VKassistent::begin() {
  randomSeed(esp_random());

  Serial.print("Reset reason: ");
  Serial.println((int)esp_reset_reason());

  // ─── LittleFS ──────────────────────────────────────────
  if (!LittleFS.begin(true)) {
    Serial.println("❌ LittleFS.begin() FAILED");
    Serial.println("   Пробую basePath /spiffs...");
    if (!LittleFS.begin(false, "/spiffs")) {
      Serial.println("❌ LittleFS с /spiffs тоже FAILED");
      Serial.println("   Нужен кастомный partition table!");
    } else {
      Serial.println("✅ LittleFS OK (basePath /spiffs)");
    }
  } else {
    Serial.println("✅ LittleFS OK");
  }

  Serial.print("💾 total: "); Serial.println(LittleFS.totalBytes());
  Serial.print("💾 used:  "); Serial.println(LittleFS.usedBytes());
  Serial.print("💾 free:  "); Serial.println(LittleFS.totalBytes() - LittleFS.usedBytes());

  _storage     = &LittleFS;
  _storageIsSD = false;

  _getLongPollServer();
}

// ─── loop() ────────────────────────────────────────────────
void VKassistent::loop() {
  if (WiFi.status() != WL_CONNECTED) {
    _wifiFailCount++;
    Serial.print("⚠️ Wi-Fi потерян, попытка ");
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

// ─── Хранилище ─────────────────────────────────────────────
void VKassistent::useLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("❌ LittleFS не запустился");
    return;
  }
  _storage     = &LittleFS;
  _storageIsSD = false;
  Serial.println("💾 Хранилище: LittleFS");
}

void VKassistent::useSD(int csPin) {
  useSD(csPin, 18, 19, 23);
}

void VKassistent::useSD(int cs, int sck, int miso, int mosi) {
  SPI.begin(sck, miso, mosi, cs);

  if (!SD.begin(cs)) {
    Serial.println("❌ SD не найдена, остаёмся на LittleFS");
    return;
  }

  if (SD.cardType() == CARD_NONE) {
    Serial.println("❌ SD: карта не вставлена");
    return;
  }

  _storage     = &SD;
  _storageIsSD = true;

  Serial.println("💾 Хранилище: SD");
  Serial.print("   total: ");
  Serial.print(SD.totalBytes() / 1024 / 1024);
  Serial.println(" MB");
}

fs::FS& VKassistent::_getStorage() {
  if (_storage == nullptr) {
    _storage     = &LittleFS;
    _storageIsSD = false;
  }
  return *_storage;
}

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
//  Удаляем все файлы во всех директориях.
void VKassistent::_clearFS() {
  Serial.print("💾 LittleFS before: used=");
  Serial.print(LittleFS.usedBytes());
  Serial.print(" free=");
  Serial.println(LittleFS.totalBytes() - LittleFS.usedBytes());

  Serial.println("🧹 Удаляю файлы...");

  File root = LittleFS.open("/");
  if (!root) {
    Serial.println("⚠️ Не могу открыть LittleFS root");
    return;
  }

  File f = root.openNextFile();
  int count = 0;
  while (f) {
    String name = f.name();
    bool isDir = f.isDirectory();
    f.close();
    if (isDir) {
      // рекурсивное удаление каталога
      File sub = LittleFS.open(name);
      if (sub) {
        File sf = sub.openNextFile();
        while (sf) {
          String sName = sf.name();
          sf.close();
          LittleFS.remove(sName);
          count++;
          yield();
          sf = sub.openNextFile();
        }
        sub.close();
      }
      LittleFS.rmdir(name);
    } else {
      if (LittleFS.remove(name)) count++;
    }
    yield();
    f = root.openNextFile();
  }
  root.close();

  Serial.print("✅ Удалено: ");
  Serial.println(count);
  Serial.print("💾 LittleFS after: free=");
  Serial.println(LittleFS.totalBytes() - LittleFS.usedBytes());
}

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

// ─── Сохранение ────────────────────────────────────────────
String VKassistent::savePhoto(const VKMessage& msg) {
  if (!msg.hasPhoto()) {
    Serial.println("⚠️ savePhoto: в сообщении нет фото");
    return "";
  }

  fs::FS& fs = _getStorage();
  if (!_storageIsSD) _clearFS();

  _logStorage();

  String path = "/";
  if (_storageIsSD) {
    if (!SD.exists("/photos")) SD.mkdir("/photos");
    path += "photos/";
  }
  path += _photoFileName(msg);

  if (_downloadToFS(fs, msg.getPhotoUrl(), path)) return path;
  return "";
}

String VKassistent::saveDoc(const VKMessage& msg) {
  if (!msg.hasDoc()) {
    Serial.println("⚠️ saveDoc: в сообщении нет документа");
    return "";
  }

  fs::FS& fs = _getStorage();
  if (!_storageIsSD) _clearFS();

  _logStorage();

  String path = "/";
  if (_storageIsSD) {
    if (!SD.exists("/docs")) SD.mkdir("/docs");
    path += "docs/";
  }
  path += _docFileName(msg);

  if (_downloadToFS(fs, msg.getDocUrl(), path)) return path;
  return "";
}

String VKassistent::saveAttachment(const VKMessage& msg, const String& path) {
  if (msg.attachments.empty()) return "";
  String url = msg.attachments[0].url;
  if (url == "") return "";

  fs::FS& fs = _getStorage();
  if (!_storageIsSD) _clearFS();

  if (_downloadToFS(fs, url, path)) return path;
  return "";
}

String VKassistent::saveAttachmentToFS(const VKMessage& msg, fs::FS& fs, const String& path) {
  if (msg.attachments.empty()) return "";
  String url = msg.attachments[0].url;
  if (url == "") return "";

  if (_downloadToFS(fs, url, path)) return path;
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

// ─── sendGeo() ─────────────────────────────────────────────
void VKassistent::sendGeo(long peerId, float lat, float lon) {
  sendGeo(peerId, "", lat, lon);
}

void VKassistent::sendGeo(long peerId, const String& text, float lat, float lon) {
  String url = "https://api.vk.com/method/messages.send";
  url += "?peer_id=" + String(peerId);
  url += "&message=" + _urlencode(text);
  url += "&lat=" + String(lat, 6);
  url += "&long=" + String(lon, 6);
  url += "&v=5.131";
  url += "&random_id=" + String(random(1000000, 9999999));
  url += "&access_token=" + _Token;
  _sendRequest(url);
}

// ─── sendPhotoFromFS() ─────────────────────────────────────
bool VKassistent::sendPhotoFromFS(long peerId, fs::FS& fs, const String& path) {
  return sendPhotoFromFS(peerId, "", fs, path);
}

bool VKassistent::sendPhotoFromFS(long peerId, const String& text,
                                  fs::FS& fs, const String& path) {
  File f = fs.open(path, FILE_READ);
  if (!f) {
    Serial.print("❌ sendPhotoFromFS: не могу открыть ");
    Serial.println(path);
    return false;
  }

  size_t len = f.size();
  if (len == 0) {
    Serial.println("❌ sendPhotoFromFS: файл пуст");
    f.close();
    return false;
  }

  Serial.print("📸 sendPhotoFromFS: файл ");
  Serial.print(len);
  Serial.println(" байт");

  uint8_t* buf = (uint8_t*)malloc(len);
  if (!buf) {
    Serial.println("❌ sendPhotoFromFS: нет heap для буфера");
    f.close();
    return false;
  }

  size_t read = f.read(buf, len);
  f.close();

  if (read != len) {
    Serial.print("❌ sendPhotoFromFS: прочитано ");
    Serial.print(read);
    Serial.print(" из ");
    Serial.println(len);
    free(buf);
    return false;
  }

  if (buf[0] != 0xFF || buf[1] != 0xD8) {
    Serial.print("⚠️ sendPhotoFromFS: не JPEG. Первые байты: ");
    Serial.print(buf[0], HEX);
    Serial.print(" ");
    Serial.println(buf[1], HEX);
  }

  String uploadUrl = _getPhotoUploadServer();
  if (uploadUrl == "") {
    Serial.println("❌ sendPhotoFromFS: не получил upload_url");
    free(buf);
    return false;
  }
  Serial.println("📤 upload_url получен");

  String respJson = _uploadPhotoToServer(uploadUrl, buf, len);
  free(buf);

  if (respJson == "") {
    Serial.println("❌ sendPhotoFromFS: upload не удался");
    return false;
  }

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, respJson);
  if (err) {
    Serial.print("❌ sendPhotoFromFS: JSON upload error: ");
    Serial.println(err.c_str());
    return false;
  }

  if (doc.containsKey("error")) {
    Serial.print("❌ sendPhotoFromFS upload error: ");
    Serial.println(respJson);
    return false;
  }

  String server = doc["server"].as<String>();
  String photo  = doc["photo"].as<String>();
  String hash   = doc["hash"].as<String>();

  if (server == "" && doc.containsKey("file1")) {
    Serial.println("ℹ️ Разбираю новый формат (file1)");
    String file1Raw = doc["file1"].as<String>();
    DynamicJsonDocument inner(2048);
    DeserializationError e2 = deserializeJson(inner, file1Raw);
    if (e2) {
      Serial.print("❌ inner JSON error: ");
      Serial.println(e2.c_str());
      return false;
    }
    JsonObject item = inner[0];
    if (item.isNull()) {
      Serial.println("❌ file1: пустой массив");
      return false;
    }
    server = item["server"].as<String>();
    photo  = item["photo"].as<String>();
    hash   = item["hash"].as<String>();
  }

  if (server == "" || photo == "" || hash == "") {
    Serial.println("❌ sendPhotoFromFS: пустые server/photo/hash");
    return false;
  }
  Serial.println("📥 Фото загружено, сохраняю...");

  int photoId = 0;
  int ownerId = 0;
  if (!_saveMessagesPhoto(server, photo, hash, photoId, ownerId)) {
    Serial.println("❌ sendPhotoFromFS: saveMessagesPhoto не удался");
    return false;
  }
  Serial.print("✅ Фото сохранено: photo");
  Serial.print(ownerId);
  Serial.print("_");
  Serial.println(photoId);

  return _sendPhotoAttachment(peerId, text, photoId, ownerId);
}

// ─── _getPhotoUploadServer() ───────────────────────────────
String VKassistent::_getPhotoUploadServer() {
  _resetClient();

  HTTPClient http;
  String url = "https://api.vk.com/method/photos.getMessagesUploadServer";
  url += "?access_token=" + _Token;
  url += "&v=5.131";

  http.begin(_client, url);
  int code = http.GET();

  if (code != 200) {
    Serial.print("❌ _getPhotoUploadServer HTTP: ");
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
    Serial.print("❌ _getPhotoUploadServer VK error: ");
    Serial.println(payload);
    return "";
  }

  return doc["response"]["upload_url"].as<String>();
}

// ─── _uploadPhotoToServer() ────────────────────────────────
String VKassistent::_uploadPhotoToServer(const String& uploadUrl,
                                          const uint8_t* buf, size_t len) {
  Serial.println("🌐 upload URL: " + uploadUrl.substring(0, 120));

  int protoPos = uploadUrl.indexOf("://");
  if (protoPos < 0) return "";
  int slashPos = uploadUrl.indexOf("/", protoPos + 3);
  if (slashPos < 0) return "";

  String host = uploadUrl.substring(protoPos + 3, slashPos);
  String path = uploadUrl.substring(slashPos);

  Serial.println("🌐 host: " + host);

  String fieldName = "photo";
  if (uploadUrl.indexOf("bulk_upload") >= 0) {
    fieldName = "file1";
    Serial.println("ℹ️ bulk_upload — используем file1");
  } else {
    Serial.println("ℹ️ upload.php — используем photo");
  }

  _resetClient();

  if (!_client.connect(host.c_str(), 443)) {
    Serial.println("❌ _uploadPhotoToServer: connect fail");
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
      Serial.println("❌ _uploadPhotoToServer: write fail");
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
        int idx = rawResponse.indexOf("\r\n\r\n");
        if (idx >= 0) headersDone = true;
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

  if (rawResponse.length() == 0) {
    Serial.println("❌ _uploadPhotoToServer: пустой ответ");
    return "";
  }

  int headerEnd = rawResponse.indexOf("\r\n\r\n");
  if (headerEnd < 0) return "";
  String body = rawResponse.substring(headerEnd + 4);

  return body;
}

// ─── _saveMessagesPhoto() ──────────────────────────────────
bool VKassistent::_saveMessagesPhoto(const String& server, const String& photo,
                                      const String& hash,
                                      int& outId, int& outOwnerId) {
  _resetClient();

  HTTPClient http;
  String url = "https://api.vk.com/method/photos.saveMessagesPhoto";
  url += "?server=" + _urlencode(server);
  url += "&photo=" + _urlencode(photo);
  url += "&hash=" + _urlencode(hash);
  url += "&access_token=" + _Token;
  url += "&v=5.131";

  http.begin(_client, url);
  int code = http.GET();

  if (code != 200) {
    Serial.print("❌ _saveMessagesPhoto HTTP: ");
    Serial.println(code);
    if (code == -1) _client.stop();
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("❌ _saveMessagesPhoto JSON: ");
    Serial.println(err.c_str());
    return false;
  }

  if (doc.containsKey("error")) {
    Serial.print("❌ _saveMessagesPhoto VK error: ");
    Serial.println(payload);
    return false;
  }

  JsonArray resp = doc["response"];
  if (resp.size() == 0) {
    Serial.println("❌ _saveMessagesPhoto: пустой response");
    return false;
  }

  outId      = resp[0]["id"]       | 0;
  outOwnerId = resp[0]["owner_id"] | 0;

  return outId != 0;
}

// ─── _sendPhotoAttachment() ────────────────────────────────
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

// ─── _downloadToFS() ───────────────────────────────────────
bool VKassistent::_downloadToFS(fs::FS& fs, const String& url, const String& path) {
  Serial.println("⬇️ Скачиваю: " + url.substring(0, 80) + "...");

  _resetClient();

  HTTPClient http;
  http.begin(_client, url);
  http.setTimeout(20000);

  int code = http.GET();
  if (code != 200) {
    Serial.print("❌ _downloadToFS HTTP: ");
    Serial.println(code);
    if (code == -1) _client.stop();
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  Serial.print("📦 Content-Length: ");
  Serial.println(contentLength);
  Serial.print("💾 Free heap: ");
  Serial.println(ESP.getFreeHeap());

  File f = fs.open(path, FILE_WRITE);
  if (!f) {
    Serial.print("❌ Не могу открыть файл: ");
    Serial.println(path);
    http.end();
    return false;
  }

  Serial.println("✅ Файл открыт, начинаю запись...");

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
      if (w != (size_t)n) {
        Serial.print("❌ FS write fail: ");
        Serial.print(w);
        Serial.print(" из ");
        Serial.println(n);
        break;
      }
      written += w;
      lastData = millis();
    } else {
      if (millis() - lastData > 10000) {
        Serial.println("❌ _downloadToFS: таймаут 10 сек без данных");
        break;
      }
      yield();
      delay(10);
    }
  }

  f.close();
  http.end();

  Serial.print("✅ _downloadToFS: ");
  Serial.print(written);
  Serial.print(" байт → ");
  Serial.println(path);

  return written > 0;
}

// ─── _resetClient() ────────────────────────────────────────
void VKassistent::_resetClient() {
  _client.stop();
  _client = WiFiClientSecure();
  _client.setInsecure();
}

// ─── _getLongPollServer() ──────────────────────────────────
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
      Serial.print("⚠️ _getLongPollServer JSON error: ");
      Serial.println(err.c_str());
    } else if (doc.containsKey("error")) {
      int errCode = doc["error"]["error_code"] | -1;
      const char* errMsg = doc["error"]["error_msg"] | "unknown";
      Serial.print("❌ VK error: ");
      Serial.print(errCode);
      Serial.print(" — ");
      Serial.println(errMsg);
    } else {
      _lpServer = doc["response"]["server"].as<String>();
      _lpKey    = doc["response"]["key"].as<String>();
      _lpTs     = doc["response"]["ts"].as<String>();
      _lpReady  = true;
      Serial.println("✅ Long Poll сервер получен");
    }
  } else {
    Serial.print("⚠️ _getLongPollServer HTTP: ");
    Serial.println(code);
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

void VKassistent::onPhoto  (CallbackFull cb) { _photoCallbacks.push_back(cb);   }
void VKassistent::onDoc    (CallbackFull cb) { _docCallbacks.push_back(cb);     }
void VKassistent::onGeo    (CallbackFull cb) { _geoCallbacks.push_back(cb);     }
void VKassistent::onSticker(CallbackFull cb) { _stickerCallbacks.push_back(cb); }

// ─── Отправка ──────────────────────────────────────────────
void VKassistent::send(long UserID, String text) {
  sendWithKeyboard(UserID, text, "");
}

void VKassistent::sendWithKeyboard(long UserID, String text, String keyboard) {
  String url = "https://api.vk.com/method/messages.send";
  url += "?peer_id=" + String(UserID);
  url += "&message=" + _urlencode(text);
  if (keyboard != "") url += "&keyboard=" + _urlencode(keyboard);
  url += "&v=5.131";
  url += "&random_id=" + String(random(1000000, 9999999));
  url += "&access_token=" + _Token;
  _sendRequest(url);
}

void VKassistent::sendToLast(String text) {
  if (_lastPeerId == 0) {
    Serial.println("⚠️ sendToLast: вне колбэка");
    return;
  }
  send(_lastPeerId, text);
}

// ─── Админы ────────────────────────────────────────────────
void VKassistent::addAdmin(long UserID) { _admins.push_back(UserID); }

bool VKassistent::isAdmin(long UserID) {
  for (long id : _admins) if (id == UserID) return true;
  return false;
}

// ─── _pollLongPoll() ───────────────────────────────────────
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
      Serial.print("❌ _pollLongPoll HTTP: ");
      Serial.println(code);
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

// ─── _handleCommand() ──────────────────────────────────────
void VKassistent::_handleCommand(VKMessage& msg) {
  long savedPeer = _lastPeerId;
  _lastPeerId = msg.peerId;

  if (msg.hasPhoto()) {
    for (auto& cb : _photoCallbacks) cb(msg);
  }
  if (msg.hasDoc()) {
    for (auto& cb : _docCallbacks) cb(msg);
  }
  if (msg.hasGeo()) {
    for (auto& cb : _geoCallbacks) cb(msg);
  }
  if (msg.hasSticker()) {
    for (auto& cb : _stickerCallbacks) cb(msg);
  }

  if (msg.text.length() > 0) {
    for (auto& rc : _commands) {
      if (rc.cmd != msg.text) continue;
      if (rc.oldCb)    rc.oldCb(msg.peerId, msg.text);
      if (rc.simpleCb) rc.simpleCb();
      if (rc.fullCb)   rc.fullCb(msg);
    }
  }

  _lastPeerId = savedPeer;
}

// ─── _parseAttachments() ───────────────────────────────────
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

// ─── _sendRequest() ────────────────────────────────────────
void VKassistent::_sendRequest(String url) {
  int attempts = 0;
  int code = -1;

  while (attempts < 3 && code != 200) {
    _resetClient();

    HTTPClient http;
    http.begin(_client, url);
    code = http.GET();

    if (code == 200) {
      String response = http.getString();
      if (response.indexOf("\"error\"") != -1) {
        Serial.println("❌ VK error (send): " + response);
      }
    } else {
      if (code != -1 && code != -11) {
        Serial.print("❌ Попытка ");
        Serial.print(attempts + 1);
        Serial.print(": HTTP ");
        Serial.println(code);
      }
      if (code == -1) _client.stop();
      delay(500);
    }

    http.end();
    attempts++;
  }

  if (code != 200) Serial.println("❌ Не отправлено после 3 попыток");
}

// ─── _urlencode() ──────────────────────────────────────────
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