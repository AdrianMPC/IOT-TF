// sensor_controlador
// v 1.0.1
// entrega final

#include <SoftwareSerial.h>

const uint8_t NODE_ID = 1;     // cambia por 1, 2, 3, etc. El 1 solo envia
const bool IS_HEAD = true;     // solo el primero = true. con detalles importantes
const unsigned long PERIOD_MS = 5000; // periodo de envío

// ---------- Pines ----------
const int PIN_LED = 13;
const int PIN_SOIL_A = A0; // FC-28
const int PIN_TILT   = 2;  // SW-520
const int PIN_VIB    = 3;  // SW-420

const int RX_IN = 8,  TX_IN = 9;   // recepción del nodo anterior
const int RX_OUT = 10, TX_OUT = 11;// envío al siguiente

SoftwareSerial serialIn(RX_IN, TX_IN);
SoftwareSerial serialOut(RX_OUT, TX_OUT);

// ---------- Calibración FC-28 ----------
const int SOIL_MIN_RAW = 300;
const int SOIL_MAX_RAW = 900;
const unsigned long VIB_TIMEOUT = 20000;
const int VIB_HIT_US = 1000;

// ---------- Funciones ----------
int clampInt(int v, int lo, int hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }

int soilPercent(int raw){
  raw = clampInt(raw, SOIL_MIN_RAW, SOIL_MAX_RAW);
  long p = (long)(SOIL_MAX_RAW - raw) * 100L / (SOIL_MAX_RAW - SOIL_MIN_RAW);
  return clampInt((int)p, 0, 100);
}

String readNodeSample(){
  int soilRaw = analogRead(PIN_SOIL_A);
  int soilPct = soilPercent(soilRaw);
  int tiltVal = digitalRead(PIN_TILT);
  unsigned long pulse = pulseIn(PIN_VIB, HIGH, VIB_TIMEOUT);
  bool vibHit = (pulse >= (unsigned long)VIB_HIT_US);

  String j = "{\"id\":"; j += NODE_ID;
  j += ",\"soil\":{\"raw\":"; j += soilRaw;
  j += ",\"pct\":"; j += soilPct;
  j += "},\"tilt\":"; j += tiltVal;
  j += ",\"vib\":{\"pulse\":"; j += pulse;
  j += ",\"hit\":"; j += (vibHit ? "true":"false");
  j += "}}";
  return j;
}

bool recvJsonFromPrev(String &json){
  serialIn.listen();              // <<< asegúrate de estar escuchando
  const unsigned long TOUT = 1500;
  unsigned long t0 = millis();

  // Espera a que llegue algo
  while (serialIn.available() == 0) {
    if (millis() - t0 > TOUT) return false;
  }

  String s = serialIn.readStringUntil('\n'); // EOP, importante para nosotros no remover
  if (s.length() == 0) return false;

  int i0 = s.indexOf('{');
  int i1 = s.lastIndexOf('}');
  if (i0 == -1 || i1 == -1 || i1 < i0) return false;

  json = s.substring(i0, i1 + 1);
  Serial.println(F("[RX] Cadena bruta recibida:"));
  Serial.println(s);
  return true;
}

void sendToNext(const String &json){
  serialOut.print(json);
  serialOut.print('\n'); 
  serialOut.flush();
  Serial.println(F("[TX] → JSON enviado al siguiente nodo:"));
  Serial.println(json);
  Serial.println(F("--------------------------------------------------"));
}

void setup(){
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_TILT, INPUT_PULLUP);
  pinMode(PIN_VIB, INPUT);

  serialIn.begin(19200);
  serialOut.begin(19200);
  Serial.begin(115200); // para ver logs en el monitor serie
  serialIn.listen(); 
  Serial.println(F("=== Nodo iniciado ==="));
  Serial.print(F("ID: ")); Serial.println(NODE_ID);
  Serial.print(F("Cabecera: ")); Serial.println(IS_HEAD ? "Sí":"No");
}

void loop(){
  static unsigned long tLast = 0;
  static uint16_t seq = 0;

  String inbound;
  bool got = recvJsonFromPrev(inbound);

  if (got){
    Serial.println(F("[RX] JSON recibido del nodo anterior:"));
    Serial.println(inbound);

    int posArr = inbound.indexOf("\"samples\":[");
    if (posArr==-1){
      inbound = "{\"seq\":0,\"samples\":[]}";
      posArr = inbound.indexOf("\"samples\":[");
    }

    int closePos = inbound.lastIndexOf(']');
    String mine = readNodeSample();
    String out = inbound.substring(0, closePos);
    if (inbound.charAt(closePos-1) != '[') out += ",";
    out += mine;
    out += inbound.substring(closePos);

    Serial.println(F("[NODE] Lectura propia agregada:"));
    Serial.println(mine);
    sendToNext(out);
    digitalWrite(PIN_LED, HIGH); delay(100); digitalWrite(PIN_LED, LOW);

  } else if (IS_HEAD && millis()-tLast >= PERIOD_MS){
    seq++;
    String head = "{\"seq\":";
    head += seq;
    head += ",\"samples\":[";
    head += readNodeSample();
    head += "]}";
    Serial.println(F("[HEAD] Generando nuevo paquete inicial:"));
    Serial.println(head);
    sendToNext(head);
    tLast = millis();
    digitalWrite(PIN_LED, HIGH); delay(100); digitalWrite(PIN_LED, LOW);
  }

  delay(10);
}
