  /*
  * Nodo 3 - Gateway ESP32-S NodeMCU 38P
  * Recibe JSON desde nodos Arduino (19200 bps)
  * Interpreta, promedia, filtra y activa buzzer/LED
  * Envia a servidor MQTT
  * Hecho por Adrian Pinado para curso IOT
  * 12 NOV.2025
  */

  #include <HardwareSerial.h>

  #include <WiFi.h>
  #include <PubSubClient.h>

  // --- CONFIG WiFi ---
  const char* ssid = "#####";
  const char* password = "#######";

  // --- CONFIG MQTT ---
  const char* mqtt_server = "#################";
  const int mqtt_port = 1884; 
  const char* topic = "lima/ews/data"; //TOPIC

  // ---------- UART desde nodos ----------
  HardwareSerial SerialNode(1);          // usamos UART1 (hardware)
  const int RX_NODE = 16;                // GPIO16 (RX)
  const int TX_NODE = 17;                // GPIO17 (TX opcional)
  const long BAUD_NODE = 19200;

  // ---------- Pines de salida ----------
  const int PIN_LED    = 2;              // LED integrado o externo
  const int PIN_BUZZER = 4;              // buzzer

  // ---------- Umbrales ----------
  const int   UMBRAL_SOIL_RAW = 600;     // humedad (raw)
  const int   UMBRAL_TILT_OK  = 0;       // tilt esperado
  const float UMBRAL_VIB_HIT  = 0.0;     // vibración esperada

  WiFiClient espClient;
  PubSubClient client(espClient);

  // ---------- WIFI related functions ----------

  void setup_wifi() {
    const unsigned long TIMEOUT = 10000;  // 10 segundos por intento

    while (true) {
      Serial.println();
      Serial.print("Conectando a WiFi: ");
      Serial.println(ssid);

      WiFi.begin(ssid, password);

      unsigned long startAttempt = millis();

      // 10 seg para timeout
      while (WiFi.status() != WL_CONNECTED &&
            millis() - startAttempt < TIMEOUT) {
        delay(500);
        Serial.print(".");
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n\nWiFi conectado.");
        Serial.print("IP asignada: ");
        Serial.println(WiFi.localIP());
        break;  
      }

      // Si NO se conectó
      Serial.println("\n\nError: no se pudo conectar en 10s.");
      Serial.println("Reintentando en 2 segundos...\n");
      delay(2000);
    }
  }

  void reconnect() {
    // Reintento de conexión MQTT
    while (!client.connected()) {
      Serial.print("[MQTT] Intentando conexión... ");
      if (client.connect("ESP32Publisher")) {
        Serial.println("Conectado!");
      } else {
        Serial.print("Fallo, rc=");
        Serial.print(client.state());
        Serial.println(" | Reintentando en 5 segundos...");
        delay(5000);
      }
    }
  }

  // ---------- Setup ----------
  void setup() {
    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    digitalWrite(PIN_BUZZER, LOW);

    Serial.begin(115200);                                       // depuración
    SerialNode.begin(BAUD_NODE, SERIAL_8N1, RX_NODE, TX_NODE);  // UART desde nodos

    Serial.println();
    Serial.println(F("=== GATEWAY ESP32 ==="));
    Serial.println("Seteando broker MQTT y WIFI");
    setup_wifi();
    client.setServer(mqtt_server, mqtt_port);
    Serial.print(F("Escuchando en RX=")); Serial.println(RX_NODE);
    Serial.print(F("Baud: ")); Serial.println(BAUD_NODE);
    Serial.println(F("-----------------------------"));
  }

  bool recvJson(String &json) {
    const unsigned long TOUT = 3000;
    unsigned long t0 = millis();

    while (SerialNode.available() == 0) {
      if (millis() - t0 > TOUT) return false;
    }

    digitalWrite(PIN_LED, HIGH);               // indicador recepción
    String s = SerialNode.readStringUntil('\n');
    if (s.length() == 0) { digitalWrite(PIN_LED, LOW); return false; }

    int i0 = s.indexOf('{');
    int i1 = s.lastIndexOf('}');
    if (i0 == -1 || i1 == -1 || i1 < i0) {
      digitalWrite(PIN_LED, LOW);
      return false;
    }

    json = s.substring(i0, i1 + 1);
    Serial.println(F("[RX] JSON recibido:"));
    Serial.println(json);
    return true;
  }

  void enviarATopic(const String &json) {
    // 1) Asegurar WiFi
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("[WIFI] Desconectado. Reintentando conexión..."));
      setup_wifi();
    }

    if (!client.connected()) {
      reconnect();
    }

    client.loop();

    Serial.println(F("\n[MQTT] Publicando en topic:"));
    Serial.println(topic);
    Serial.println(F("Contenido:"));
    Serial.println(json);

    bool ok = client.publish(topic, json.c_str());

    if (ok) {
      Serial.println(F("[MQTT] Envío OK"));
    } else {
      Serial.println(F("[MQTT] Error al publicar"));
    }

    Serial.println(F("--------------------------------------------------\n"));
  }

  void interpretarYProcesar(const String &json) {
    float sumSoil = 0;
    int   sumTilt = 0;
    int   sumVib  = 0;
    int   count   = 0;

    int pos = 0;
    while ((pos = json.indexOf("{\"id\":", pos)) != -1) {
      int soilPos = json.indexOf("\"soil\":{\"raw\":", pos);
      int tiltPos = json.indexOf("\"tilt\":", pos);
      int vibPos  = json.indexOf("\"vib\":{\"pulse\":", pos);
      if (soilPos == -1 || tiltPos == -1 || vibPos == -1) break;

      int soilVal = json.substring(soilPos + 14, json.indexOf(",", soilPos)).toInt();
      int tiltVal = json.substring(tiltPos + 7, json.indexOf(",", tiltPos)).toInt();

      int vibHitPos = json.indexOf("\"hit\":", vibPos);
      bool vibHit = json.substring(vibHitPos + 6, json.indexOf("}", vibHitPos)).startsWith("true");

      sumSoil += soilVal;
      sumTilt += tiltVal;
      sumVib  += (vibHit ? 1 : 0);
      count++;

      pos = vibPos + 1;
    }

    if (count == 0) {
      Serial.println(F("[WARN] Paquete vacío o corrompido"));
      digitalWrite(PIN_LED, LOW);
      return;
    }

    float avgSoil = sumSoil / count;
    float avgTilt = (float)sumTilt / count;
    float avgVib  = (float)sumVib  / count;

    Serial.println(F("=== DATOS PROCESADOS ==="));
    Serial.print(F("Nodos: ")); Serial.println(count);
    Serial.print(F("Promedio Humedad (raw): ")); Serial.println(avgSoil);
    Serial.print(F("Promedio Tilt: ")); Serial.println(avgTilt);
    Serial.print(F("Promedio Vibración: ")); Serial.println(avgVib);

    bool alarma = (avgTilt > 0.0f) || (avgVib > 0.0f) || (avgSoil < UMBRAL_SOIL_RAW);

    // --- Accionar alarma ---
    if (alarma) {
      Serial.println(F("[ALERTA] CONDICIÓN DE RIESGO - BUZZER ON"));
      tone(PIN_BUZZER, 1000, 800);  // puedes ajustar frecuencia/duración si quieres
    } else {
      Serial.println(F("[INFO] Condición normal - BUZZER OFF"));
      noTone(PIN_BUZZER);
      digitalWrite(PIN_BUZZER, LOW);
    }
    // --- Agregar campo 'alert' al JSON antes de enviar ---
    String jsonFinal = json;
    jsonFinal.trim();

    // Si termina en '}', insertamos antes el campo "alert"
    if (jsonFinal.endsWith("}")) {
      jsonFinal.remove(jsonFinal.length() - 1);
      jsonFinal += ",\"alerta\":";
      jsonFinal += (alarma ? "1" : "0");
      jsonFinal += "}";
    }

    enviarATopic(jsonFinal);
    digitalWrite(PIN_LED, LOW);
  }

  void loop() {
    String paquete;
    if (recvJson(paquete)) {
      interpretarYProcesar(paquete);
    }
    delay(50);
  }

