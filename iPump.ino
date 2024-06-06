#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>

const char* ssid_ap = "iPump-Access-Point";
const char* password_ap = NULL;

AsyncWebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences preferences;

#define RELAY_PUMP_PIN  2
#define RELAY_VALVE_PIN  4
#define BUTTON_AUTO_PIN 12
#define BUTTON_MANUAL_PIN 14
#define BUTTON_MANUAL_PUMP_PIN 33
#define BUTTON_MANUAL_VALVE_PIN 32
#define LED_MODE_AUTO_PIN 16
#define LED_MODE_MANUAL_PIN 17
#define FLOAT_SWITCH_C1_CRITICAL_PIN 18
#define FLOAT_SWITCH_C1_FULL_PIN 19
#define FLOAT_SWITCH_C2_CRITICAL_PIN 21
#define FLOAT_SWITCH_C2_FULL_PIN 22
#define LED_C1_CRITICAL_PIN 23
#define LED_C1_FULL_PIN 25
#define LED_C2_CRITICAL_PIN 26
#define LED_C2_FULL_PIN 27

bool autoMode = true;
bool manualMode = false;
bool pumpManualState = false;
bool valveManualState = false;
bool mqttEnabled = false;

char mqttBroker[100] = "broker.hivemq.com";
char mqttTopic[100] = "esp32/state";
char mqttControlTopic[100] = "esp32/control";
char mqttModeTopic[100] = "esp32/control/mode";

void setup() {
  Serial.begin(115200);
  
  pinMode(RELAY_PUMP_PIN, OUTPUT);
  pinMode(RELAY_VALVE_PIN, OUTPUT);
  pinMode(BUTTON_AUTO_PIN, INPUT_PULLUP);
  pinMode(BUTTON_MANUAL_PIN, INPUT_PULLUP);
  pinMode(BUTTON_MANUAL_PUMP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_MANUAL_VALVE_PIN, INPUT_PULLUP);
  pinMode(LED_MODE_AUTO_PIN, OUTPUT);
  pinMode(LED_MODE_MANUAL_PIN, OUTPUT);
  pinMode(FLOAT_SWITCH_C1_CRITICAL_PIN, INPUT);
  pinMode(FLOAT_SWITCH_C1_FULL_PIN, INPUT);
  pinMode(FLOAT_SWITCH_C2_CRITICAL_PIN, INPUT);
  pinMode(FLOAT_SWITCH_C2_FULL_PIN, INPUT);
  pinMode(LED_C1_CRITICAL_PIN, OUTPUT);
  pinMode(LED_C1_FULL_PIN, OUTPUT);
  pinMode(LED_C2_CRITICAL_PIN, OUTPUT);
  pinMode(LED_C2_FULL_PIN, OUTPUT);
  
  digitalWrite(RELAY_PUMP_PIN, LOW);  // bomba desligada inicialmente
  digitalWrite(RELAY_VALVE_PIN, LOW); // válvula fechada inicialmente

  preferences.begin("wifi-creds", false);
  String savedSSID = preferences.getString("ssid", "");
  String savedPassword = preferences.getString("password", "");

  if (savedSSID != "" && savedPassword != "") {
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  } else {
    WiFi.begin(ssid_ap, password_ap);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  if (!MDNS.begin("ipump")) {
    Serial.println("Error setting up MDNS responder!");
  }
  MDNS.addService("http", "tcp", 80);

  mqttClient.setServer(mqttBroker, 1883);
  mqttClient.setCallback(mqttCallback);

  setupRoutes();
  server.begin();
  Serial.println("HTTP Server Started");

  xTaskCreatePinnedToCore(
    taskControl,        // Task function
    "TaskControl",      // Task name
    10000,              // Stack size
    NULL,               // Task input parameter
    1,                  // Task priority
    NULL,               // Task handle
    0                   // Core ID
  );

  xTaskCreatePinnedToCore(
    taskMQTT,           // Task function
    "TaskMQTT",         // Task name
    10000,              // Stack size
    NULL,               // Task input parameter
    1,                  // Task priority
    NULL,               // Task handle
    1                   // Core ID
  );
}

void loop() {
  // Empty loop as tasks handle the functionality
}

void taskControl(void * pvParameters) {
  while (true) {
    checkMode();
    updateLEDs();
  
    if (autoMode) {
      controlWaterLevelAutomatically();
    } else if (manualMode) {
      controlWaterLevelManually();
    }
    delay(100);
  }
}

void taskMQTT(void * pvParameters) {
  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!mqttClient.connected() && mqttEnabled) {
        reconnectMQTT();
      }
      mqttClient.loop();
    }
    delay(100);
  }
}

void checkMode() {
  if (digitalRead(BUTTON_AUTO_PIN) == LOW) {
    autoMode = true;
    manualMode = false;
  } else if (digitalRead(BUTTON_MANUAL_PIN) == LOW) {
    autoMode = false;
    manualMode = true;
  }
}

void updateLEDs() {
  digitalWrite(LED_MODE_AUTO_PIN, autoMode);
  digitalWrite(LED_MODE_MANUAL_PIN, manualMode);

  digitalWrite(LED_C1_CRITICAL_PIN, digitalRead(FLOAT_SWITCH_C1_CRITICAL_PIN));
  digitalWrite(LED_C1_FULL_PIN, digitalRead(FLOAT_SWITCH_C1_FULL_PIN));
  digitalWrite(LED_C2_CRITICAL_PIN, digitalRead(FLOAT_SWITCH_C2_CRITICAL_PIN));
  digitalWrite(LED_C2_FULL_PIN, digitalRead(FLOAT_SWITCH_C2_FULL_PIN));
}

void controlWaterLevelAutomatically() {
  if (digitalRead(FLOAT_SWITCH_C1_CRITICAL_PIN) == HIGH) {
    digitalWrite(RELAY_PUMP_PIN, HIGH);
    digitalWrite(RELAY_VALVE_PIN, LOW); // encher caixa 1
  } else if (digitalRead(FLOAT_SWITCH_C2_CRITICAL_PIN) == HIGH) {
    digitalWrite(RELAY_PUMP_PIN, HIGH);
    digitalWrite(RELAY_VALVE_PIN, HIGH); // encher caixa 2
  } else {
    digitalWrite(RELAY_PUMP_PIN, LOW); // desliga a bomba
  }

  if (digitalRead(FLOAT_SWITCH_C1_FULL_PIN) == HIGH) {
    digitalWrite(RELAY_PUMP_PIN, LOW); // desliga a bomba se a caixa 1 estiver cheia
  }

  if (digitalRead(FLOAT_SWITCH_C2_FULL_PIN) == HIGH) {
    digitalWrite(RELAY_PUMP_PIN, LOW); // desliga a bomba se a caixa 2 estiver cheia
  }
}

void controlWaterLevelManually() {
  if (digitalRead(BUTTON_MANUAL_PUMP_PIN) == LOW) {
    pumpManualState = !pumpManualState;
    delay(500); // debounce delay
  }
  
  if (digitalRead(BUTTON_MANUAL_VALVE_PIN) == LOW) {
    valveManualState = !valveManualState;
    delay(500); // debounce delay
  }
  
  digitalWrite(RELAY_PUMP_PIN, pumpManualState ? HIGH : LOW);
  digitalWrite(RELAY_VALVE_PIN, valveManualState ? HIGH : LOW);
}

void setupRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", mainPage());
  });

  server.on("/control", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", controlPage());
  });

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", statusPage());
  });

  server.on("/togglePump", HTTP_POST, [](AsyncWebServerRequest *request){
    pumpManualState = !pumpManualState;
    digitalWrite(RELAY_PUMP_PIN, pumpManualState ? HIGH : LOW);
    request->redirect("/control?done=1");
  });

  server.on("/toggleValve", HTTP_POST, [](AsyncWebServerRequest *request){
    valveManualState = !valveManualState;
    digitalWrite(RELAY_VALVE_PIN, valveManualState ? HIGH : LOW);
    request->redirect("/control?done=1");
  });

  server.on("/network", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", networkPage());
  });

  server.on("/saveNetwork", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
      String ssid = request->getParam("ssid", true)->value();
      String password = request->getParam("password", true)->value();
      preferences.putString("ssid", ssid);
      preferences.putString("password", password);
      WiFi.begin(ssid.c_str(), password.c_str());
    }
    request->redirect("/network?done=1");
  });

  server.on("/mqtt", HTTP_POST, [](AsyncWebServerRequest *request){
    if (request->hasParam("broker", true) && request->hasParam("topic", true)) {
      strcpy(mqttBroker, request->getParam("broker", true)->value().c_str());
      strcpy(mqttTopic, request->getParam("topic", true)->value().c_str());
      mqttEnabled = request->hasParam("mqttEnable", true) && request->getParam("mqttEnable", true)->value() == "on";
      reconnectMQTT();
    }
    request->redirect("/network");
  });

  server.on("/sensor-data", HTTP_GET, [](AsyncWebServerRequest *request){
    int sensorValue = analogRead(15);  // Analog read from pin 15
    String json = "{\"value\":" + String(sensorValue) + "}";
    request->send(200, "application/json", json);
  });
}

String mainPage() {
  String html = "<html><head><title>iPump Control</title></head><body>";
  html += "<h1>iPump Control</h1>";
  html += "<p><a href=\"/control\">Control</a></p>";
  html += "<p><a href=\"/status\">Status</a></p>";
  html += "<p><a href=\"/network\">Network</a></p>";
  html += "</body></html>";
  return html;
}

String controlPage() {
  String html = "<html><head><title>Control Page</title>";
  html += "<style>body { font-family: Arial; }</style></head><body>";
  html += "<h1>Manual Control</h1>";
  html += "<form action=\"/togglePump\" method=\"POST\"><button type=\"submit\">Toggle Pump</button></form>";
  html += "<form action=\"/toggleValve\" method=\"POST\"><button type=\"submit\">Toggle Valve</button></form>";
  html += "</body></html>";
  return html;
}

String statusPage() {
  String html = "<html><head><title>Status Page</title>";
  html += "<style>body { font-family: Arial; }</style></head><body>";
  html += "<h1>Status</h1>";
  html += "<p>Auto Mode: " + String(autoMode ? "Enabled" : "Disabled") + "</p>";
  html += "<p>Manual Mode: " + String(manualMode ? "Enabled" : "Disabled") + "</p>";
  html += "<p>Pump State: " + String(digitalRead(RELAY_PUMP_PIN) ? "ON" : "OFF") + "</p>";
  html += "<p>Valve State: " + String(digitalRead(RELAY_VALVE_PIN) ? "OPEN" : "CLOSED") + "</p>";
  html += "</body></html>";
  return html;
}

String networkPage() {
  String html = "<html><head><title>Network Settings</title>";
  html += "<style>body { font-family: Arial; }</style></head><body>";
  html += "<h1>Network Settings</h1>";
  html += "<form action=\"/saveNetwork\" method=\"POST\">";
  html += "WiFi SSID: <input type=\"text\" name=\"ssid\"><br>";
  html += "WiFi Password: <input type=\"password\" name=\"password\"><br>";
  html += "<button type=\"submit\">Save</button>";
  html += "</form>";
  html += "<form action=\"/mqtt\" method=\"POST\">";
  html += "MQTT Broker: <input type=\"text\" name=\"broker\" value=\"" + String(mqttBroker) + "\"><br>";
  html += "MQTT Topic: <input type=\"text\" name=\"topic\" value=\"" + String(mqttTopic) + "\"><br>";
  html += "Enable MQTT: <input type=\"checkbox\" name=\"mqttEnable\"" + String(mqttEnabled ? " checked" : "") + "><br>";
  html += "<button type=\"submit\">Save</button>";
  html += "</form>";
  html += "</body></html>";
  return html;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(message);

  if (String(topic) == mqttControlTopic) {
    if (message == "togglePump") {
      pumpManualState = !pumpManualState;
      digitalWrite(RELAY_PUMP_PIN, pumpManualState ? HIGH : LOW);
      publishState();
    } else if (message == "toggleValve") {
      valveManualState = !valveManualState;
      digitalWrite(RELAY_VALVE_PIN, valveManualState ? HIGH : LOW);
      publishState();
    }
  } else if (String(topic) == mqttModeTopic) {
    if (message == "auto") {
      autoMode = true;
      manualMode = false;
    } else if (message == "manual") {
      autoMode = false;
      manualMode = true;
    }
    publishState();
  }
}

void publishState() {
  String state = "{\"autoMode\":" + String(autoMode ? "true" : "false") + ",";
  state += "\"manualMode\":" + String(manualMode ? "true" : "false") + ",";
  state += "\"pumpState\":" + String(pumpManualState ? "true" : "false") + ",";
  state += "\"valveState\":" + String(valveManualState ? "true" : "false") + "}";
  mqttClient.publish(mqttTopic, state.c_str());
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (mqttClient.connect("ESP32Client")) {
      Serial.println("connected");
      mqttClient.subscribe(mqttControlTopic);
      mqttClient.subscribe(mqttModeTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
