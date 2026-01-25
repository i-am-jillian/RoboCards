#include <WiFiS3.h>

// ---------- Wi-Fi ----------
const char* ssid = "URVISH";
const char* password = "12345678";
WiFiServer server(80);

// ---------- STATIC IP ----------
IPAddress localIP(192, 168, 137, 51);
IPAddress gateway(192, 168, 137, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 137, 1);

// ---------- L298 Pins (CHANGE IF NEEDED) ----------
#define IN1  2
#define IN2  3
#define ENA  5  // PWM
#define IN3  4
#define IN4  7
#define ENB  6  // PWM

// ---------- Motion tuning (edit these) ----------
const int DRIVE_PWM = 200;
const unsigned long DRIVE_MS = 900;

// Turning timing is robot-specific; adjust until correct.
const unsigned long TURN_90_MS  = 350;
const unsigned long TURN_180_MS = 700;
const unsigned long TURN_270_MS = 1050;
const unsigned long TURN_360_MS = 1400;

String lastCmd = "";
String lastResult = "Idle";
unsigned long lastExecMs = 0;

void stopMotors() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  analogWrite(ENA, 0); analogWrite(ENB, 0);
}

void driveForward(unsigned long ms) {
  // both forward
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
  analogWrite(ENA, DRIVE_PWM); analogWrite(ENB, DRIVE_PWM);
  delay(ms);
  stopMotors();
}

void driveBackward(unsigned long ms) {
  // both backward
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
  analogWrite(ENA, DRIVE_PWM); analogWrite(ENB, DRIVE_PWM);
  delay(ms);
  stopMotors();
}

// spin in place
void turnCW(unsigned long ms) {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);   // left forward
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);  // right backward
  analogWrite(ENA, DRIVE_PWM); analogWrite(ENB, DRIVE_PWM);
  delay(ms);
  stopMotors();
}

void turnCCW(unsigned long ms) {
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);  // left backward
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);   // right forward
  analogWrite(ENA, DRIVE_PWM); analogWrite(ENB, DRIVE_PWM);
  delay(ms);
  stopMotors();
}

String getQueryParam(const String& path, const String& key) {
  int q = path.indexOf('?');
  if (q < 0) return "";
  String qs = path.substring(q + 1);
  int start = qs.indexOf(key + "=");
  if (start < 0) return "";
  start += key.length() + 1;
  int end = qs.indexOf('&', start);
  if (end < 0) end = qs.length();
  return qs.substring(start, end);
}

void sendJson(WiFiClient& client, const String& json) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Access-Control-Allow-Origin: *");
  client.println("Connection: close");
  client.println();
  client.println(json);
}

String executeCommand(const String& cmd) {
  unsigned long t0 = millis();

  if (cmd == "START") {
    // optional: do nothing or blink
    delay(50);
  }
  else if (cmd == "FORWARD") {
    driveForward(DRIVE_MS);
  }
  else if (cmd == "BACKWARD") {
    driveBackward(DRIVE_MS);
  }
  else if (cmd == "TURN_CW") {
    turnCW(TURN_90_MS);
  }
  else if (cmd == "TURN_CCW") {
    turnCCW(TURN_90_MS);
  }
  else if (cmd == "TURN_90") {
    turnCW(TURN_90_MS);
  }
  else if (cmd == "TURN_180") {
    turnCW(TURN_180_MS);
  }
  else if (cmd == "TURN_270") {
    turnCW(TURN_270_MS);
  }
  else if (cmd == "TURN_360") {
    turnCW(TURN_360_MS);
  }
  else if (cmd == "END") {
    delay(50);
  }
  else if (cmd == "STOP") {
    stopMotors();
  }
  else {
    lastExecMs = millis() - t0;
    return "Unknown cmd";
  }

  lastExecMs = millis() - t0;
  return "OK";
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);
  stopMotors();

  WiFi.config(localIP, dns, gateway, subnet);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nRobot Wi-Fi connected!");
    Serial.print("Robot IP: ");
    Serial.println(WiFi.localIP());
    server.begin();
  } else {
    Serial.println("\nRobot Wi-Fi failed!");
  }
}

void loop() {
  WiFiClient client = server.available();
  if (!client) return;

  String reqLine = client.readStringUntil('\n');
  reqLine.trim();

  while (client.connected() && client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) break;
  }

  int sp1 = reqLine.indexOf(' ');
  int sp2 = reqLine.indexOf(' ', sp1 + 1);
  String path = (sp1 > 0 && sp2 > sp1) ? reqLine.substring(sp1 + 1, sp2) : "/";

  if (path.startsWith("/status")) {
    String json = "{";
    json += "\"lastCmd\":\"" + lastCmd + "\",";
    json += "\"lastResult\":\"" + lastResult + "\",";
    json += "\"lastExecMs\":" + String(lastExecMs);
    json += "}";
    sendJson(client, json);
    client.stop();
    return;
  }

  if (path.startsWith("/run")) {
    String cmd = getQueryParam(path, "cmd");
    lastCmd = cmd;
    lastResult = executeCommand(cmd);

    String json = "{";
    json += "\"received\":\"" + cmd + "\",";
    json += "\"result\":\"" + lastResult + "\",";
    json += "\"execMs\":" + String(lastExecMs);
    json += "}";
    sendJson(client, json);
    client.stop();
    return;
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain");
  client.println("Connection: close");
  client.println();
  client.println("Robot OK. Use /run?cmd=FORWARD or /status");
  client.stop();
}
