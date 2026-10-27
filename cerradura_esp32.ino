// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  NEXUS-7 — Cerradura Inteligente Autónoma IoT                           ║
// ║  ESP32 + RFID RC522 + Servo + Sincronización Nube Firebase              ║
// ║  (Teclado, Servidor Web local y Bot de Telegram eliminados)             ║
// ║  100% Autónomo: Solo requiere fuente de 5V y conexión Wi-Fi             ║
// ╚══════════════════════════════════════════════════════════════════════════╝

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>

// ────────────────────────────────────────────────────────────
//  1. CONFIGURACIÓN WI-FI
// ────────────────────────────────────────────────────────────
const char* ssid     = "Estudiantes";
const char* password = "educar_2018";

// ────────────────────────────────────────────────────────────
//  2. CONFIGURACIÓN FIREBASE REALTIME DATABASE
//  (Reemplaza con la URL de tu base de datos de Firebase)
// ────────────────────────────────────────────────────────────
const String FIREBASE_HOST = "https://nexus-cerradura-default-rtdb.firebaseio.com";

// ────────────────────────────────────────────────────────────
//  3. HARDWARE: SERVO & RFID & LEDS
// ────────────────────────────────────────────────────────────
Servo miServo;
#define PIN_SERVO 4

#define SS_PIN  5
#define RST_PIN 22
MFRC522 mfrc522(SS_PIN, RST_PIN);
byte usuarioAutorizado[4] = {0x62, 0x55, 0x14, 0x5C}; // UID de la tarjeta autorizada

#define LED_ROJO  15
#define LED_VERDE  2

// ────────────────────────────────────────────────────────────
//  4. ESTADOS Y TIEMPOS
// ────────────────────────────────────────────────────────────
String estadoCerradura = "CERRADO";
String metodoAcceso    = "---";

unsigned long tiempoApertura       = 0;
const unsigned long TIEMPO_ABIERTO = 5000UL; // Cierre automático a los 5 segundos

// Temporizador para consultar Firebase
unsigned long ultimoCheckFirebase   = 0;
const unsigned long INTERVALO_CHECK = 1200UL; // Consulta cada 1.2 segundos

// Modo RFID Libre (Bypass por 60 segundos)
bool         rfidLibre      = false;
unsigned long tRfidLibre    = 0;
const unsigned long DUR_RFID_LIBRE = 60000UL;

// Declaraciones adelantadas de funciones
void abrirCerradura(const char* metodo);
void cerrarCerradura();
void actualizarFirebase(const String& estado, const String& metodo);
void verificarComandoFirebase();

// ════════════════════════════════════════════════════════════
//  SINCRONIZACIÓN CON FIREBASE (NUBE)
// ════════════════════════════════════════════════════════════

// Actualiza el estado en la nube para que la web en Vercel cambie en vivo
void actualizarFirebase(const String& estado, const String& metodo) {
  if (WiFi.status() != WL_CONNECTED || FIREBASE_HOST.indexOf("TU_PROYECTO") >= 0) return;
  
  WiFiClientSecure client;
  client.setInsecure(); // No validar certificados SSL para optimizar memoria RAM
  HTTPClient https;

  String url = FIREBASE_HOST + "/cerradura.json";
  if (https.begin(client, url)) {
    https.addHeader("Content-Type", "application/json");
    String body = "{\"estado\":\"" + estado + "\",\"metodo\":\"" + metodo + "\"}";
    https.PATCH(body);
    https.end();
  }
}

// Revisa si desde la web de Vercel presionaron "Abrir Puerta"
void verificarComandoFirebase() {
  if (WiFi.status() != WL_CONNECTED || FIREBASE_HOST.indexOf("TU_PROYECTO") >= 0) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = FIREBASE_HOST + "/cerradura/comando_abrir.json";
  if (https.begin(client, url)) {
    int httpCode = https.GET();
    if (httpCode == 200) {
      String payload = https.getString();
      if (payload == "true") {
        Serial.println("🌐 [VERCEL] Orden de apertura remota recibida desde la nube");
        
        // Resetear la orden a false inmediatamente en Firebase
        https.begin(client, url);
        https.addHeader("Content-Type", "application/json");
        https.PUT("false");
        
        abrirCerradura("WEB_VERCEL");
      }
    }
    https.end();
  }
}

// ════════════════════════════════════════════════════════════
//  CONTROL DE CERRADURA (SERVO Y LEDS)
// ════════════════════════════════════════════════════════════
void abrirCerradura(const char* metodo) {
  metodoAcceso    = metodo;
  estadoCerradura = "ABIERTO";
  
  miServo.write(180); // Giro de apertura del cerrojo
  digitalWrite(LED_ROJO,  LOW);
  digitalWrite(LED_VERDE, HIGH);
  tiempoApertura = millis();

  // Actualizar la nube en tiempo real (Vercel se pone verde al instante)
  actualizarFirebase("ABIERTO", metodoAcceso);
  
  Serial.print("🔓 ABIERTO — Método: "); Serial.println(metodo);
}

void cerrarCerradura() {
  estadoCerradura = "CERRADO";
  
  miServo.write(0); // Giro de bloqueo del cerrojo
  digitalWrite(LED_ROJO,  HIGH);
  digitalWrite(LED_VERDE, LOW);

  // Actualizar la nube en tiempo real (Vercel se pone rojo)
  actualizarFirebase("CERRADO", metodoAcceso);
  
  Serial.println("🔒 Cerradura bloqueada.");
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Configuración de LEDs
  pinMode(LED_ROJO,  OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  digitalWrite(LED_ROJO,  HIGH);
  digitalWrite(LED_VERDE, LOW);

  // Configuración del Servomotor
  miServo.attach(PIN_SERVO);
  miServo.write(0); // Posición inicial cerrada

  // Inicialización del lector RFID RC522
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("\n--- NEXUS-7 CONTROL DE ACCESO ---");
  Serial.println("Hardware: RFID RC522 + Servo + Firebase Cloud");

  // Conexión Wi-Fi
  Serial.printf("Conectando a Wi-Fi: %s\n", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWi-Fi Conectado exitosamente. IP: %s\n", WiFi.localIP().toString().c_str());

  // Estado inicial en la nube
  actualizarFirebase("CERRADO", "INICIO");

  Serial.println("Sistema 100% operativo y sincronizado con la nube.\n");
}

// ════════════════════════════════════════════════════════════
//  LOOP PRINCIPAL
// ════════════════════════════════════════════════════════════
void loop() {
  unsigned long ahora = millis();

  // 1. Consultar si hay órdenes de apertura desde la web de Vercel
  if (ahora - ultimoCheckFirebase >= INTERVALO_CHECK) {
    ultimoCheckFirebase = ahora;
    verificarComandoFirebase();
  }

  // 2. Cierre automático tras 5 segundos de apertura
  if (estadoCerradura == "ABIERTO" && (ahora - tiempoApertura >= TIEMPO_ABIERTO)) {
    cerrarCerradura();
  }

  // 3. Expiración del modo RFID Libre (Bypass 60 segundos)
  if (rfidLibre && (ahora - tRfidLibre >= DUR_RFID_LIBRE)) {
    rfidLibre = false;
    Serial.println("[RFID] Bypass de 60s expirado.");
  }

  // 4. Lectura de tarjeta física RFID
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    Serial.print("Tarjeta detectada. UID:");
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      Serial.printf(" %02X", mfrc522.uid.uidByte[i]);
    }
    Serial.println();

    if (rfidLibre) {
      abrirCerradura("RFID_LIBRE");
    } else {
      bool autorizada = (mfrc522.uid.size == 4);
      for (byte i = 0; i < 4 && autorizada; i++) {
        if (mfrc522.uid.uidByte[i] != usuarioAutorizado[i]) autorizada = false;
      }

      if (autorizada) {
        abrirCerradura("RFID");
      } else {
        Serial.println("RFID DENEGADO — Tarjeta no autorizada.");
        // Parpadeo de advertencia en LED rojo
        digitalWrite(LED_ROJO, LOW); delay(150); digitalWrite(LED_ROJO, HIGH);
      }
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }

  yield();
}
