#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// Definición de pines de los sensores (modificar estos valores según sea necesario)
#define PIN_MQ135_1 34        // Pin para el primer sensor MQ135
#define PIN_MQ135_2 35        // Pin para el segundo sensor MQ135
#define PIN_DHT11 15

// Pin para el sensor DHT11 (ambiente)
#define PIN_SOIL_MOISTURE 36  // Pin para el sensor de humedad del suelo
#define PIN_LDR 39            // Pin para el sensor de luminosidad (LDR)
#define PIN_PUMP 33           // Pin para la minibomba de agua
#define PIN_LEVEL_SENSOR 32   // Pin para el sensor de nivel

// Umbrales para el control de la bomba de agua
#define SOIL_MOISTURE_THRESHOLD_LOW 1500  // Umbral bajo para encender la bomba
#define SOIL_MOISTURE_THRESHOLD_HIGH 2500 // Umbral alto para apagar la bomba

// Inicialización del DHT11
#define DHTTYPE DHT11
DHT dht(PIN_DHT11, DHTTYPE);

// Configuración WiFi
const char* ssid = "POCO M5s";
const char* password = "Gallegos1234";

// Configuración MQTT
const char* mqtt_server = "100.28.201.242"; // Dirección del servidor RabbitMQ
const int mqtt_port = 1883;                // Puerto MQTT, usualmente 1883
const char* mqtt_user = "guest";           // Usuario de RabbitMQ
const char* mqtt_password = "guest";       // Contraseña de RabbitMQ
const char* mqtt_topic = "sensores/esp32/data"; // Tópico al cual enviar los datos

WiFiClient espClient;
PubSubClient client(espClient);

// Configuración del servidor web
AsyncWebServer server(80);

String jsonData = "";

// Página HTML
const char* index_html = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>PlantCare Dashboard</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background-color: #f0f8f5;
      color: #333;
      text-align: center;
    }
    h2 {
      color: #006400;
    }
    table {
      margin: 0 auto;
      border-collapse: collapse;
      width: 80%;
      max-width: 800px;
    }
    th, td {
      padding: 12px;
      border: 1px solid #ddd;
    }
    th {
      background-color: #006400;
      color: white;
    }
    tr:nth-child(even) {
      background-color: #f2f2f2;
    }
    .sensor-data {
      display: flex;
      justify-content: center;
      flex-wrap: wrap;
    }
    .sensor-card {
      background-color: #fff;
      border: 1px solid #ddd;
      border-radius: 8px;
      box-shadow: 0 4px 8px rgba(0,0,0,0.1);
      margin: 10px;
      padding: 16px;
      width: 250px;
    }
    .sensor-card h3 {
      color: #006400;
    }
  </style>
</head>
<body>
  <h2>Datos de los Sensores</h2>
  <div class="sensor-data">
    <div class="sensor-card">
      <h3>Calidad del Aire (MQ135)</h3>
      <p id="mq135_1">MQ135 1: N/A</p>
      <p id="mq135_2">MQ135 2: N/A</p>
      <p id="mq135_avg">Promedio: N/A</p>
    </div>
    <div class="sensor-card">
      <h3>Humedad y Temperatura</h3>
      <p id="humidity">Humedad: N/A</p>
      <p id="temperature">Temperatura: N/A</p>
    </div>
    <div class="sensor-card">
      <h3>Humedad del Suelo</h3>
      <p id="soil_moisture">Humedad del Suelo: N/A</p>
    </div>
    <div class="sensor-card">
      <h3>Luminosidad</h3>
      <p id="ldr">Luminosidad: N/A</p>
    </div>
    <div class="sensor-card">
      <h3>Sensor de Nivel</h3>
      <p id="level_sensor">Nivel: N/A</p>
    </div>
  </div>
  <script>
    function getSensorData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          document.getElementById('mq135_1').innerText = 'MQ135 1: ' + data.mq135_1;
          document.getElementById('mq135_2').innerText = 'MQ135 2: ' + data.mq135_2;
          document.getElementById('mq135_avg').innerText = 'Promedio: ' + data.mq135_avg;
          document.getElementById('humidity').innerText = 'Humedad: ' + data.humidity + '%';
          document.getElementById('temperature').innerText = 'Temperatura: ' + data.temperature + '°C';
          document.getElementById('soil_moisture').innerText = 'Humedad del Suelo: ' + data.soil_moisture;
          document.getElementById('ldr').innerText = 'Luminosidad: ' + data.ldr;
          document.getElementById('level_sensor').innerText = 'Nivel: ' + data.level_sensor;
        });
    }

    setInterval(getSensorData, 60000); // Actualizar cada minuto
    window.onload = getSensorData;
  </script>
</body>
</html>
)rawliteral";

void setup_wifi() {
  delay(10);
  Serial.print("Conectando a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado. Dirección IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);

  // Configuración del pin de la bomba de agua
  pinMode(PIN_PUMP, OUTPUT);
  digitalWrite(PIN_PUMP, LOW); // Apagar la bomba inicialmente

  // Configuración del pin del sensor de nivel
  pinMode(PIN_LEVEL_SENSOR, INPUT);

  // Configuración del servidor web
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", jsonData);
  });

  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando a MQTT...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("Conectado");
    } else {
      Serial.print("Error, rc=");
      Serial.print(client.state());
      Serial.println(" intentar de nuevo en 5 segundos");
      delay(5000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Leer el primer sensor MQ135 (calidad del aire)
  int mq135Value1 = analogRead(PIN_MQ135_1);
  Serial.print("MQ135_1: ");
  Serial.println(mq135Value1);

  // Leer el segundo sensor MQ135 (calidad del aire)
  int mq135Value2 = analogRead(PIN_MQ135_2);
  Serial.print("MQ135_2: ");
  Serial.println(mq135Value2);

  // Decidir cuál valor usar (aquí tomamos la media)
  int mq135Average = (mq135Value1 + mq135Value2) / 2;
  Serial.print("MQ135 (average): ");
  Serial.println(mq135Average);

  // Leer el sensor DHT11 (humedad y temperatura del ambiente)
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  
  if (isnan(h) || isnan(t)) {
    Serial.println("Error leyendo del DHT11!");
  } else {
    Serial.print("Humedad (ambiente): ");
    Serial.print(h);
    Serial.println("%");
    Serial.print("Temperatura (ambiente): ");
    Serial.print(t);
    Serial.println("°C");
  }

  // Leer el sensor de humedad del suelo
  int soilMoistureValue = analogRead(PIN_SOIL_MOISTURE);
  Serial.print("Humedad del suelo: ");
  Serial.println(soilMoistureValue);

  // Controlar la minibomba de agua basado en el valor de humedad del suelo
  if (soilMoistureValue > SOIL_MOISTURE_THRESHOLD_HIGH) {
    digitalWrite(PIN_PUMP, HIGH); // Encender la bomba
    Serial.println("Bomba encendida");
  } else if (soilMoistureValue < SOIL_MOISTURE_THRESHOLD_LOW) {
    digitalWrite(PIN_PUMP, LOW); // Apagar la bomba
    Serial.println("Bomba apagada");
  } else {
     digitalWrite(PIN_PUMP, LOW);
    Serial.println("Bomba mantiene estado anterior");
  }

  // Leer el sensor de luminosidad (LDR)
  int ldrValue = analogRead(PIN_LDR);
  Serial.print("Luminosidad: ");
  Serial.println(ldrValue);

  // Leer el sensor de nivel
  int levelSensorValue = digitalRead(PIN_LEVEL_SENSOR);
  String levelStatus = (levelSensorValue == HIGH) ? "Lleno" : "Vacío";
  Serial.print("Nivel: ");
  Serial.println(levelStatus);

 String macAddress = WiFi.macAddress();
  Serial.print("Dirección MAC: ");
  Serial.println(macAddress);
  // Crear un objeto JSON para enviar los datos al servidor
  StaticJsonDocument<300> doc;
  doc["mac_adrress"] = macAddress;
  doc["mq135_1"] = mq135Value1;
  doc["mq135_2"] = mq135Value2;
  doc["mq135_avg"] = mq135Average;
  doc["humidity"] = h;
  doc["temperature"] = t;
  doc["soil_moisture"] = soilMoistureValue;
  doc["ldr"] = ldrValue;
  doc["level_sensor"] = levelStatus;

  // Convertir el objeto JSON en una cadena
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  jsonData = String(jsonBuffer);

  // Publicar los datos en el tópico MQTT
  client.publish(mqtt_topic, jsonBuffer);

  // Esperar 20 segundos antes de leer los sensores de nuevo
  delay(20000);
}
