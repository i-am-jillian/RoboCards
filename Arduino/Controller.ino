#include <SPI.h>
#include <MFRC522.h>
#include <WiFiS3.h>

// ---------- RC522 Pins ----------
#define SS_PIN 7
#define RST_PIN 6
MFRC522 rfid(SS_PIN, RST_PIN);

// ---------- LEDs ----------
#define LED_RED 8
#define LED_GREEN 9

// ---------- Wi-Fi ----------
const char* ssid = "URVISH";
const char* password = "12345678";
WiFiServer server(80);

// ---------- STATIC IP (Laptop Hotspot default usually 192.168.137.1) ----------
IPAddress localIP(192, 168, 137, 50);   // UNO #1 fixed IP
IPAddress gateway(192, 168, 137, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 137, 1);

// ---------- ROBOT (UNO #2) IP ----------
IPAddress robotIP(192, 168, 137, 51);   // UNO #2 fixed IP

// ---------- Program storage ----------
const int MAX_STEPS = 50;
String steps[MAX_STEPS];
int stepCount = 0;

bool isPlaying = false;
bool stopFlag = false;

// ---------- Robot status (for webpage) ----------
bool robotOnline = false;
String robotLastCmd = "";
String robotLastResult = "No data";
unsigned long robotLastExecMs = 0;
unsigned long robotLastSeenMs = 0;
unsigned long lastRobotPoll = 0;

// ---------- NFC UIDs (YOUR LIST) ----------
const String UID_START            = "04711B432F0289";

const String UID_FORWARD          = "04B144572F0289";
const String UID_BACKWARD         = "04F14DDE300289";

const String UID_TURN_90          = "0431D5B42B0289";
const String UID_TURN_180         = "04E17FB42B0289";
const String UID_TURN_270         = "0451A74E2F0289";
const String UID_TURN_360         = "0461FBEC300289";

const String UID_TURN_CW          = "04E1C2DE300289";
const String UID_TURN_CCW         = "048186D5300289";

// Repeat loop numbers (1..10)
const String UID_LOOP_1           = "04F1DAD9300289";
const String UID_LOOP_2           = "04D14126310289";
const String UID_LOOP_3           = "0481D8512F0289";
const String UID_LOOP_4           = "04D1A1B42B0289";
const String UID_LOOP_5           = "04F16B372F0289";
const String UID_LOOP_6           = "04F18BBC2B0289";
const String UID_LOOP_7           = "04F11A4B2F0289";
const String UID_LOOP_8           = "0421883A2F0289";
const String UID_LOOP_9           = "04614ABD2B0289";
const String UID_LOOP_10          = "04315E12310289";

const String UID_END_LOOP         = "0411BC3F2F0289";
const String UID_END              = "04017104310289";

// Buttons
const String UID_RUN              = "0471A5B32B0289";
const String UID_STOP             = "04C1F63B2F0289";

// ---------- Helpers: tiny JSON string extract (no ArduinoJson needed) ----------
String extractJsonString(const String& body, const String& key) {
  String pat = "\"" + key + "\":\"";
  int i = body.indexOf(pat);
  if (i < 0) return "";
  i += pat.length();
  int j = body.indexOf("\"", i);
  if (j < 0) return "";
  return body.substring(i, j);
}

long extractJsonNumber(const String& body, const String& key) {
  String pat = "\"" + key + "\":";
  int i = body.indexOf(pat);
  if (i < 0) return -1;
  i += pat.length();
  int j = i;
  while (j < (int)body.length() && (isDigit(body[j]) || body[j] == '-')) j++;
  return body.substring(i, j).toInt();
}

// ---------- Simple HTTP GET to robot, returns body ----------
bool httpGetBody(IPAddress ip, const String& path, String& bodyOut, unsigned long timeoutMs = 1200) {
  WiFiClient c;
  bodyOut = "";

  if (!c.connect(ip, 80)) return false;

  c.print("GET ");
  c.print(path);
  c.println(" HTTP/1.1");
  c.print("Host: ");
  c.println(ip);
  c.println("Connection: close");
  c.println();

  unsigned long start = millis();

  // Wait for headers end: \r\n\r\n
  String header = "";
  while (millis() - start < timeoutMs) {
    while (c.available()) {
      char ch = c.read();
      header += ch;
      if (header.endsWith("\r\n\r\n")) goto HEADERS_DONE;
    }
  }
HEADERS_DONE:

  // Read body
  start = millis();
  while (millis() - start < timeoutMs) {
    while (c.available()) {
      char ch = c.read();
      bodyOut += ch;
      start = millis();
    }
    if (!c.connected()) break;
  }

  c.stop();
  return bodyOut.length() > 0;
}

// ---------- Poll robot /status every ~1.5s ----------
void pollRobotStatus() {
  String body;
  bool ok = httpGetBody(robotIP, "/status", body);

  if (!ok) {
    robotOnline = false;
    robotLastResult = "Robot offline / no response";
    return;
  }

  robotOnline = true;
  robotLastSeenMs = millis();

  String lc = extractJsonString(body, "lastCmd");
  String lr = extractJsonString(body, "lastResult");
  long execMs = extractJsonNumber(body, "lastExecMs");

  if (lc.length()) robotLastCmd = lc;
  if (lr.length()) robotLastResult = lr;
  if (execMs >= 0) robotLastExecMs = (unsigned long)execMs;
}

// ---------- Send command to robot and store ACK ----------
void sendCmdToRobotAndStoreAck(const String& cmd) {
  String path = "/run?cmd=" + cmd;
  String body;

  bool ok = httpGetBody(robotIP, path, body);

  if (!ok) {
    robotOnline = false;
    robotLastCmd = cmd;
    robotLastResult = "Send failed / no ACK";
    return;
  }

  robotOnline = true;
  robotLastSeenMs = millis();
  robotLastCmd = cmd;

  String result = extractJsonString(body, "result");
  long execMs = extractJsonNumber(body, "execMs");

  robotLastResult = result.length() ? result : "ACK received";
  if (execMs >= 0) robotLastExecMs = (unsigned long)execMs;
}

// ---------- UID reading ----------
String readUIDUpper() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

// ---------- Loop tokens ----------
bool isLoopStartToken(const String& s) { return s.startsWith("LOOP_START:"); }
bool isLoopEndToken(const String& s)   { return s == "LOOP_END"; }

int parseLoopRepeat(const String& s) {
  // "LOOP_START:3"
  int colon = s.indexOf(':');
  if (colon < 0) return 1;
  return s.substring(colon + 1).toInt();
}

int findLoopEndIndex(int startIdx) {
  for (int i = startIdx + 1; i < stepCount; i++) {
    if (steps[i] == "LOOP_END") return i;
  }
  return -1;
}

String displayStep(const String& token) {
  if (isLoopStartToken(token)) {
    return "LOOP x" + String(parseLoopRepeat(token)) + " (start)";
  }
  if (isLoopEndToken(token)) return "END LOOP";
  return token;
}

void addStep(const String& token) {
  if (stepCount >= MAX_STEPS) {
    Serial.println("Step memory full!");
    return;
  }
  steps[stepCount++] = token;
  Serial.println("Stored step: " + displayStep(token));
}

// ---------- Map UID -> token (command or loop token) ----------
String uidToToken(const String& uid) {
  if (uid == UID_START) return "START";
  if (uid == UID_FORWARD) return "FORWARD";
  if (uid == UID_BACKWARD) return "BACKWARD";

  if (uid == UID_TURN_90) return "TURN_90";
  if (uid == UID_TURN_180) return "TURN_180";
  if (uid == UID_TURN_270) return "TURN_270";
  if (uid == UID_TURN_360) return "TURN_360";

  if (uid == UID_TURN_CW) return "TURN_CW";
  if (uid == UID_TURN_CCW) return "TURN_CCW";

  // Loop count tags -> LOOP_START:N
  if (uid == UID_LOOP_1)  return "LOOP_START:1";
  if (uid == UID_LOOP_2)  return "LOOP_START:2";
  if (uid == UID_LOOP_3)  return "LOOP_START:3";
  if (uid == UID_LOOP_4)  return "LOOP_START:4";
  if (uid == UID_LOOP_5)  return "LOOP_START:5";
  if (uid == UID_LOOP_6)  return "LOOP_START:6";
  if (uid == UID_LOOP_7)  return "LOOP_START:7";
  if (uid == UID_LOOP_8)  return "LOOP_START:8";
  if (uid == UID_LOOP_9)  return "LOOP_START:9";
  if (uid == UID_LOOP_10) return "LOOP_START:10";

  if (uid == UID_END_LOOP) return "LOOP_END";
  if (uid == UID_END) return "END";

  return "";
}

// ---------- Execute one token (send to robot) ----------
void execToken(const String& token) {
  if (token.length() == 0) return;

  // Skip loop markers here (handled by loop engine)
  if (isLoopStartToken(token) || isLoopEndToken(token)) return;

  // Send to robot + store ACK so webpage shows "received"
  sendCmdToRobotAndStoreAck(token);

  // small gap
  delay(60);
}

// ---------- Sequence execution with loop expansion ----------
void executeSequence() {
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, HIGH);

  int i = 0;
  while (i < stepCount && !stopFlag) {
    String token = steps[i];

    if (isLoopStartToken(token)) {
      int repeatN = parseLoopRepeat(token);
      int endIdx = findLoopEndIndex(i);

      if (endIdx < 0) {
        Serial.println("ERROR: LOOP_START found but no LOOP_END!");
        robotLastResult = "ERROR: Missing END LOOP";
        break;
      }

      Serial.println("Executing LOOP x" + String(repeatN));

      for (int r = 0; r < repeatN && !stopFlag; r++) {
        for (int j = i + 1; j < endIdx && !stopFlag; j++) {
          execToken(steps[j]);
        }
      }

      i = endIdx + 1; // jump after LOOP_END
      continue;
    }

    if (isLoopEndToken(token)) {
      // stray end loop (ignore)
      i++;
      continue;
    }

    // Normal token
    Serial.println("Executing: " + token);
    execToken(token);

    // If END encountered, you may choose to stop execution early
    if (token == "END") break;

    i++;
  }

  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, HIGH);

  if (stopFlag) Serial.println("Sequence stopped!");
  else Serial.println("Sequence finished!");

  // Reset after run
  stepCount = 0;
  isPlaying = false;
  stopFlag = false;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  SPI.begin();
  rfid.PCD_Init();

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);

  Serial.println("Connecting to Wi-Fi (Static IP, Laptop Hotspot)...");
  WiFi.config(localIP, dns, gateway, subnet);

  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi connected!");
    Serial.print("UNO #1 IP: ");
    Serial.println(WiFi.localIP());
    server.begin();
    Serial.println("Web server started on port 80.");
  } else {
    Serial.println("\nWi-Fi connection failed!");
  }

  Serial.println("Scan tags to build program. Tap RUN to execute. STOP to stop.");
}

void loop() {
  // Web + robot poll
  handleWebClients();
  if (millis() - lastRobotPoll > 1500) {
    lastRobotPoll = millis();
    pollRobotStatus();
  }

  // NFC
  handleNFC();
}

// ---------- Web ----------
void handleWebClients() {
  WiFiClient client = server.available();
  if (!client) return;

  String reqLine = client.readStringUntil('\n');
  reqLine.trim();

  // Drain headers
  while (client.connected() && client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
  }

  if (reqLine.startsWith("GET /status")) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();

    client.print("{\"status\":\"");
    client.print(isPlaying ? "Playing" : "Idle");
    client.print("\",\"steps\":[");

    for (int i = 0; i < stepCount; i++) {
      String ds = displayStep(steps[i]);
      client.print("\"");
      client.print(ds);
      client.print("\"");
      if (i < stepCount - 1) client.print(",");
    }

    client.print("],\"robot\":{");
    client.print("\"online\":");
    client.print(robotOnline ? "true" : "false");
    client.print(",\"lastCmd\":\"");
    client.print(robotLastCmd);
    client.print("\",\"lastResult\":\"");
    client.print(robotLastResult);
    client.print("\",\"lastExecMs\":");
    client.print(robotLastExecMs);
    client.print(",\"lastSeenAgoMs\":");
    client.print(robotOnline ? (millis() - robotLastSeenMs) : 999999);
    client.println("}}");

    client.stop();
    return;
  }

  // Main page
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html; charset=utf-8");
  client.println("Connection: close");
  client.println();

  client.println("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>UNO #1 Controller</title>");
  client.println("<style>");
  client.println("body{font-family:Arial; padding:16px;}");
  client.println(".card{border:1px solid #ccc; border-radius:10px; padding:12px; margin:12px 0;}");
  client.println(".row{display:flex; gap:24px; flex-wrap:wrap;}");
  client.println(".k{color:#555; font-size:13px;} .v{font-size:16px; font-weight:600;}");
  client.println("</style>");
  client.println("<script>");
  client.println("function updateStatus(){");
  client.println("fetch('/status').then(r=>r.json()).then(d=>{");
  client.println("document.getElementById('status').textContent=d.status;");
  client.println("let ul=document.getElementById('steps'); ul.innerHTML='';");
  client.println("d.steps.forEach(s=>{let li=document.createElement('li'); li.textContent=s; ul.appendChild(li);});");
  client.println("document.getElementById('robotOnline').textContent = d.robot.online ? 'ONLINE' : 'OFFLINE';");
  client.println("document.getElementById('robotLastCmd').textContent = d.robot.lastCmd || '-';");
  client.println("document.getElementById('robotLastResult').textContent = d.robot.lastResult || '-';");
  client.println("document.getElementById('robotExec').textContent = (d.robot.lastExecMs ?? '-') + ' ms';");
  client.println("document.getElementById('robotSeen').textContent = (d.robot.lastSeenAgoMs ?? '-') + ' ms ago';");
  client.println("}).catch(()=>{}); }");
  client.println("setInterval(updateStatus, 1000); window.onload=updateStatus;");
  client.println("</script></head><body>");

  client.println("<h1>UNO #1 NFC Controller</h1>");

  client.println("<div class='card'>");
  client.println("<div class='k'>Controller Status</div>");
  client.println("<div class='v'>Status: <span id='status'>Idle</span></div>");
  client.println("<h3>Program Steps</h3><ul id='steps'></ul>");
  client.println("</div>");

  client.println("<div class='card'>");
  client.println("<div class='k'>Robot (UNO #2) - Received by Robot</div>");
  client.println("<div class='row'>");
  client.println("<div><div class='k'>Robot</div><div class='v' id='robotOnline'>-</div></div>");
  client.println("<div><div class='k'>Last Received Cmd</div><div class='v' id='robotLastCmd'>-</div></div>");
  client.println("<div><div class='k'>Robot Result</div><div class='v' id='robotLastResult'>-</div></div>");
  client.println("<div><div class='k'>Exec Time</div><div class='v' id='robotExec'>-</div></div>");
  client.println("<div><div class='k'>Last Seen</div><div class='v' id='robotSeen'>-</div></div>");
  client.println("</div>");
  client.println("</div>");

  client.println("</body></html>");
  client.stop();
}

// ---------- NFC ----------
void handleNFC() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uid = readUIDUpper();
  Serial.println("Card UID: " + uid);

  // Buttons
  if (uid == UID_RUN) {
    if (stepCount > 0) {
      isPlaying = true;
      stopFlag = false;
      executeSequence();
    } else {
      Serial.println("No steps stored yet.");
    }
  }
  else if (uid == UID_STOP) {
    stopFlag = true;
    isPlaying = false;
    Serial.println("STOP activated");
    sendCmdToRobotAndStoreAck("STOP");
  }
  else {
    // Only record when not playing
    if (!isPlaying) {
      String token = uidToToken(uid);
      if (token.length() > 0) addStep(token);
      else Serial.println("Unknown tag (not mapped).");
    }
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  delay(250);
}
