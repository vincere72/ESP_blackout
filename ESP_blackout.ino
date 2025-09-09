// rev.16 (12..15 di debug)
// WiFiManager con configurazione WiFi via web, ping a Blynk/MacroDroid,
// LED heartbeat, log su web/terminal con timestamp UTC, log live con AJAX + autoscroll
// + pulsante per pulire il log + reset WiFi via D3 in qualsiasi momento
// Ottimizzato per stabilità a lungo termine (no crash heap)

#define BLYNK_TEMPLATE_ID "TMPL4pOA_QOLl"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiManager.h>
#include <ESP8266WebServer.h>
#include <time.h>

unsigned int counter = 0;

// ===================== CONFIG =====================
char auth[] = "YwA6GjPJDiNOpxSPZI83hnV34yD3HdI-"; // Auth Token Blynk
const char* webhook_url = "https://trigger.macrodroid.com/e069f131-c220-474c-a554-f04c090461d6/triggerBlackout";

const unsigned long pingInterval = 30000; // 30s ping
unsigned long lastPingTime = 0;

const int resetPin = D3;
const int ledPin = LED_BUILTIN; // LOW = acceso

// ===================== WEB SERVER =====================
ESP8266WebServer server(80);

// ===================== LOG BUFFER =====================
#define LOG_LINES 50
#define LOG_LINE_LEN 128
char logBuffer[LOG_LINES][LOG_LINE_LEN];
int logIndex = 0;

// Terminal Blynk V10
WidgetTerminal terminal(V10);

// ===================== TIMESTAMP =====================
void getTimestamp(char* buf, size_t len) {
  time_t now = time(nullptr);
  struct tm* t = gmtime(&now); // UTC
  strftime(buf, len, "%Y-%m-%d %H:%M:%S UTC", t);
}

// ===================== LOG FUNCTION =====================
void logMessage(const char* msg) {
  char timestamp[32];
  getTimestamp(timestamp, sizeof(timestamp));

  char fullMsg[LOG_LINE_LEN];
  snprintf(fullMsg, LOG_LINE_LEN, "[%s] %s", timestamp, msg);

  Serial.println(fullMsg);
  terminal.println(fullMsg);
  terminal.flush();

  strncpy(logBuffer[logIndex], fullMsg, LOG_LINE_LEN - 1);
  logBuffer[logIndex][LOG_LINE_LEN - 1] = 0;
  logIndex = (logIndex + 1) % LOG_LINES;
}

void clearLog() {
  for (int i = 0; i < LOG_LINES; i++) logBuffer[i][0] = 0;
  logIndex = 0;
  logMessage("Log cancellato");
}

// ===================== PING FUNCTION =====================
void sendPing() {
  logMessage("Eseguo ping...");

  // Ping Blynk
  Blynk.virtualWrite(V1, 1);
  logMessage("Ping inviato a Blynk");

  // Ping MacroDroid
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, webhook_url);
    int code = http.GET();
    char buf[64];
    snprintf(buf, sizeof(buf), "%u Ping inviato a MacroDroid, HTTP code: %d", counter, code);
    logMessage(buf);
    counter++;
    http.end();
  } else {
    logMessage("WiFi non connesso: impossibile inviare ping a MacroDroid");
  }
}

// ===================== WEB HANDLERS =====================
void handleRoot() {
  String html = "<html><head><meta charset='utf-8'></head><body>";
  html += "<h2>ESP8266 Ping Monitor</h2>";
  html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
  html += "<p><b>Ping inviati:</b> " + String(counter) + "</p>";
  html += "<p><b>Stato WiFi:</b> " + String(WiFi.status() == WL_CONNECTED ? "Connesso" : "Disconnesso") + "</p>";
  html += "<form action='/reset'><input type='submit' value='Reset Counter'></form>";
  html += "<form action='/wifi'><input type='submit' value='Configura WiFi'></form>";
  html += "<p><a href='/log'>Visualizza log live</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleReset() {
  logMessage("Reset contatore richiesto dal webserver");
  counter = 0;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleWiFiConfig() {
  logMessage("Richiesta configurazione WiFi dal webserver");
  server.send(200, "text/html", "<html><body><h2>Riavvio in modalità configurazione WiFi...</h2></body></html>");
  delay(500);
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

void handleLog() {
  String html = R"rawliteral(
<html>
<head>
<meta charset='utf-8'>
<title>ESP8266 Log</title>
<script>
function updateLog() {
  fetch('/logdata')
    .then(response => response.text())
    .then(text => {
      let box = document.getElementById('logbox');
      box.textContent = text;
      box.scrollTop = box.scrollHeight;
    });
}
setInterval(updateLog,2000);
window.onload=updateLog;
</script>
</head>
<body>
<h2>Log ESP8266 (live)</h2>
<pre id="logbox" style="background:#111;color:#0f0;padding:10px;border-radius:8px;height:400px;overflow:auto;"></pre>
<form action='/clearlog'><input type='submit' value='Pulisci log'></form>
<p><a href='/'>Torna alla Home</a></p>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleLogData() {
  String logText = "";
  for (int i = 0; i < LOG_LINES; i++) {
    int idx = (logIndex + i) % LOG_LINES;
    if (logBuffer[idx][0] != 0) logText += String(logBuffer[idx]) + "\n";
  }
  server.send(200, "text/plain", logText);
}

void handleClearLog() {
  clearLog();
  server.sendHeader("Location", "/log");
  server.send(303);
}

// ===================== SETUP =====================
void setup() {
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);
  pinMode(resetPin, INPUT_PULLUP);

  Serial.begin(115200);

  // NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  logMessage("Sincronizzazione orario NTP...");
  while (!time(nullptr)) delay(500);
  logMessage("Orario NTP sincronizzato");

  // WiFiManager
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  if (!wm.autoConnect("ESP8266_Config")) {
    logMessage("Connessione WiFi fallita, riavvio...");
    ESP.restart();
  }

  char buf[64];
  snprintf(buf, sizeof(buf), "WiFi connesso! IP: %s", WiFi.localIP().toString().c_str());
  logMessage(buf);

  // Blynk
  Blynk.begin(auth, WiFi.SSID().c_str(), WiFi.psk().c_str());

  // Webserver
  server.on("/", handleRoot);
  server.on("/reset", handleReset);
  server.on("/wifi", handleWiFiConfig);
  server.on("/log", handleLog);
  server.on("/logdata", handleLogData);
  server.on("/clearlog", handleClearLog);
  server.begin();
  snprintf(buf, sizeof(buf), "Web server avviato su http://%s", WiFi.localIP().toString().c_str());
  logMessage(buf);
}

// ===================== LOOP =====================
unsigned long lastLedToggle = 0;
unsigned long ledInterval = 1000;
bool ledState = false;

// gestione pulsante D3
unsigned long buttonPressTime = 0;
bool buttonHeld = false;

void loop() {
  Blynk.run();
  server.handleClient();

  // LED heartbeat
  if (WiFi.status() == WL_CONNECTED) ledInterval = 1000;
  else ledInterval = 200;

  unsigned long now = millis();
  if (now - lastPingTime >= pingInterval) {
    lastPingTime = now;
    sendPing();
  }

  if (now - lastLedToggle >= ledInterval) {
    lastLedToggle = now;
    ledState = !ledState;
    digitalWrite(ledPin, ledState ? LOW : HIGH);
  }

  // === Controllo pulsante D3 ===
  if (digitalRead(resetPin) == LOW) {
    if (!buttonHeld) {
      buttonHeld = true;
      buttonPressTime = millis();
    } else if (millis() - buttonPressTime > 3000) {
      logMessage("Pulsante D3 tenuto premuto: reset configurazione WiFi");
      WiFiManager wm;
      wm.resetSettings();
      ESP.restart();
    }
  } else {
    buttonHeld = false;
  }
}
// rev.11
// WiFiManager con configurazione WiFi via web, ping a Blynk/MacroDroid,
// LED heartbeat, log su web/terminal con timestamp UTC, log live con AJAX + autoscroll
// + pulsante per pulire il log + reset WiFi via D3 in qualsiasi momento

#define BLYNK_TEMPLATE_ID "TMPL4pOA_QOLl"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <WiFiManager.h>
#include <ESP8266WebServer.h>
#include <time.h>

unsigned int counter = 0;

// ===================== CONFIG =====================
char auth[] = "YwA6GjPJDiNOpxSPZI83hnV34yD3HdI-"; // Auth Token Blynk
const char* webhook_url = "https://trigger.macrodroid.com/e069f131-c220-474c-a554-f04c090461d6/triggerBlackout";

const unsigned long pingInterval = 30000; // 30s ping
unsigned long lastPingTime = 0;

const int resetPin = D3;
const int ledPin = LED_BUILTIN; // LOW = acceso

// ===================== WEB SERVER =====================
ESP8266WebServer server(80);

// ===================== LOG BUFFER =====================
#define LOG_LINES 50
String logBuffer[LOG_LINES];
int logIndex = 0;

// Terminal Blynk V10
WidgetTerminal terminal(V10);

// ===================== TIMESTAMP =====================
String getTimestamp() {
  time_t now = time(nullptr);
  struct tm* t = gmtime(&now); // UTC
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", t);
  return String(buf);
}

// ===================== LOG FUNCTION =====================
void logMessage(String msg, bool newline=true) {
  String fullMsg = "[" + getTimestamp() + "] " + msg;

  if (newline) {
    Serial.println(fullMsg);
    terminal.println(fullMsg);
  } else {
    Serial.print(fullMsg);
    terminal.print(fullMsg);
  }
  terminal.flush();

  logBuffer[logIndex] = fullMsg;
  logIndex = (logIndex + 1) % LOG_LINES;
}

void clearLog() {
  for (int i=0; i<LOG_LINES; i++) logBuffer[i] = "";
  logIndex = 0;
  logMessage("Log cancellato");
}

// ===================== PING FUNCTION =====================
void sendPing() {
  logMessage("Eseguo ping...");

  // Ping Blynk
  Blynk.virtualWrite(V1, 1);
  logMessage("Ping inviato a Blynk");

  // Ping MacroDroid
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, webhook_url);
    int code = http.GET();
    logMessage(String(counter) + " Ping inviato a MacroDroid, HTTP code: " + String(code));
    counter++;
    http.end();
  } else {
    logMessage("WiFi non connesso: impossibile inviare ping a MacroDroid");
  }
}

// ===================== WEB HANDLERS =====================
void handleRoot() {
  String html = "<html><head><meta charset='utf-8'></head><body>";
  html += "<h2>ESP8266 Ping Monitor</h2>";
  html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
  html += "<p><b>Ping inviati:</b> " + String(counter) + "</p>";
  html += "<p><b>Stato WiFi:</b> " + String(WiFi.status() == WL_CONNECTED ? "Connesso" : "Disconnesso") + "</p>";
  html += "<form action='/reset'><input type='submit' value='Reset Counter'></form>";
  html += "<form action='/wifi'><input type='submit' value='Configura WiFi'></form>";
  html += "<p><a href='/log'>Visualizza log live</a></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleReset() {
  logMessage("Reset contatore richiesto dal webserver");
  counter = 0;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleWiFiConfig() {
  logMessage("Richiesta configurazione WiFi dal webserver");
  server.send(200, "text/html", "<html><body><h2>Riavvio in modalità configurazione WiFi...</h2></body></html>");
  delay(500);
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

void handleLog() {
  String html = R"rawliteral(
  <html>
  <head>
    <meta charset='utf-8'>
    <title>ESP8266 Log</title>
    <script>
      function updateLog() {
        fetch('/logdata')
          .then(response => response.text())
          .then(text => {
            let box = document.getElementById('logbox');
            box.textContent = text;
            box.scrollTop = box.scrollHeight;
          });
      }
      setInterval(updateLog, 2000);
      window.onload = updateLog;
    </script>
  </head>
  <body>
    <h2>Log ESP8266 (live)</h2>
    <pre id="logbox" style="background:#111;color:#0f0;padding:10px;border-radius:8px;height:400px;overflow:auto;"></pre>
    <form action='/clearlog'><input type='submit' value='Pulisci log'></form>
    <p><a href='/'>Torna alla Home</a></p>
  </body>
  </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleLogData() {
  String logText = "";
  for (int i=0; i<LOG_LINES; i++) {
    int idx = (logIndex + i) % LOG_LINES;
    if (logBuffer[idx].length() > 0) logText += logBuffer[idx] + "\n";
  }
  server.send(200, "text/plain", logText);
}

void handleClearLog() {
  clearLog();
  server.sendHeader("Location", "/log");
  server.send(303);
}

// ===================== SETUP =====================
void setup() {
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);
  pinMode(resetPin, INPUT_PULLUP);

  Serial.begin(115200);

  // NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  logMessage("Sincronizzazione orario NTP...");
  while (!time(nullptr)) delay(500);
  logMessage("Orario NTP sincronizzato");

  // WiFiManager
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);

  if (!wm.autoConnect("ESP8266_Config")) {
    logMessage("Connessione WiFi fallita, riavvio...");
    ESP.restart();
  }

  logMessage("WiFi connesso! IP: " + WiFi.localIP().toString());

  // Blynk
  Blynk.begin(auth, WiFi.SSID().c_str(), WiFi.psk().c_str());

  // Webserver
  server.on("/", handleRoot);
  server.on("/reset", handleReset);
  server.on("/wifi", handleWiFiConfig);
  server.on("/log", handleLog);
  server.on("/logdata", handleLogData);
  server.on("/clearlog", handleClearLog);
  server.begin();
  logMessage("Web server avviato su http://" + WiFi.localIP().toString());
}

// ===================== LOOP =====================
unsigned long lastLedToggle = 0;
unsigned long ledInterval = 1000;
bool ledState = false;

// gestione pulsante D3
unsigned long buttonPressTime = 0;
bool buttonHeld = false;

void loop() {
  Blynk.run();
  server.handleClient();

  // LED heartbeat
  if (WiFi.status() == WL_CONNECTED) ledInterval = 1000;
  else ledInterval = 200;

  unsigned long now = millis();
  if (now - lastPingTime >= pingInterval) {
    lastPingTime = now;
    sendPing();
  }

  if (now - lastLedToggle >= ledInterval) {
    lastLedToggle = now;
    ledState = !ledState;
    digitalWrite(ledPin, ledState ? LOW : HIGH);
  }

  // === Controllo pulsante D3 ===
  if (digitalRead(resetPin) == LOW) {
    if (!buttonHeld) {
      buttonHeld = true;
      buttonPressTime = millis();
    } else if (millis() - buttonPressTime > 3000) {
      logMessage("Pulsante D3 tenuto premuto: reset configurazione WiFi");
      WiFiManager wm;
      wm.resetSettings();
      ESP.restart();
    }
  } else {
    buttonHeld = false;
  }
}
