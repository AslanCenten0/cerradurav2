// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  NEXUS-7 — Cerradura Inteligente Autónoma IoT (Versión Estable v5.0)     ║
// ║  ESP32 + RFID RC522 + Servo (Pin 18) + Sincronización Firebase          ║
// ║  - Servo en Pin 18 con detach automático (cero zumbido/vibración)       ║
// ║  - SPI RC522 en Pin 4 (SCK) para no colisionar con el Servo             ║
// ║  - Polling optimizado a Firebase sin bloqueos de red                     ║
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
// ────────────────────────────────────────────────────────────
const String FIREBASE_HOST = "https://nexus-cerradura-default-rtdb.firebaseio.com";

// ────────────────────────────────────────────────────────────
//  3. HARDWARE: PINES Y DEFINICIONES
// ────────────────────────────────────────────────────────────
// Servo en GPIO 18
#define PIN_SERVO 18
Servo miServo;

// Lector RFID RC522 (Pines SPI dedicados)
// IMPORTANTE: SCK se asigna al Pin 4 para que NO colisione con el Servo en el Pin 18
#define RFID_SCK  4   // Reloj SPI (SCK) -> Pin 4 (Evita conflicto con Servo en Pin 18)
#define RFID_MISO 19  // MISO -> Pin 19
#define RFID_MOSI 23  // MOSI -> Pin 23
#define RFID_SS   5   // SDA / SS -> Pin 5
#define RFID_RST  22  // RST -> Pin 22

MFRC522 mfrc522(RFID_SS, RFID_RST);
byte usuarioAutorizado[4] = {0x62, 0x55, 0x14, 0x5C}; // UID de tu tarjeta autorizada

// Indicadores LED
#define LED_ROJO  15
#define LED_VERDE  2

// ────────────────────────────────────────────────────────────
//  4. ESTADOS Y TIEMPOS
// ────────────────────────────────────────────────────────────
String estadoCerradura = "CERRADO";
String metodoAcceso    = "---";

unsigned long tiempoApertura       = 0;
const unsigned long TIEMPO_ABIERTO = 5000UL; // Cierre automático a los 5 segundos

// Temporizador para consultar Firebase (cada 2 segundos para no ahogar el procesador)
unsigned long ultimoCheckFirebase   = 0;
const unsigned long INTERVALO_CHECK = 2000UL;

// Modo RFID Libre (Bypass por 60 segundos)
bool          rfidLibre      = false;
unsigned long tRfidLibre     = 0;
const unsigned long DUR_RFID_LIBRE = 60000UL;

// Declaraciones de funciones
void moverServo(int angulo);
void abrirCerradura(const char* metodo);
void cerrarCerradura();
void actualizarFirebase(const String& estado, const String& metodo);
void verificarComandoFirebase();

// ════════════════════════════════════════════════════════════
//  CONTROL DE SERVO SUAVE Y SIN VIBRACIONES (ANTI-JITTER)
// ════════════════════════════════════════════════════════════
void moverServo(int angulo) {
  // Conectar señal PWM, posicionar, esperar llegada mecánica y desconectar
  miServo.setPeriodHertz(50);
  miServo.attach(PIN_SERVO, 500, 2400);
  miServo.write(angulo);
  delay(500); // 500ms es suficiente para que el engranaje complete el recorrido
  miServo.detach(); // Detach apaga los pulsos PWM: elimina vibración, calor y consumo
}

// ════════════════════════════════════════════════════════════
//  SINCRONIZACIÓN CON FIREBASE (NUBE)
// ════════════════════════════════════════════════════════════

// Actualiza el estado en la nube para que la web en Vercel cambie en vivo
void actualizarFirebase(const String& estado, const String& metodo) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(2); // Timeout de 2s máximo para no congelar el loop
  
  HTTPClient https;
  https.setTimeout(2000);

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
  if (WiFi.status() != WL_CONNECTED) return;
  // Si la puerta ya está abierta, no hace falta consultar comandos
  if (estadoCerradura == "ABIERTO") return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(2);
  
  HTTPClient https;
  https.setTimeout(2000);

  String url = FIREBASE_HOST + "/cerradura/comando_abrir.json";
  if (https.begin(client, url)) {
    int httpCode = https.GET();
    if (httpCode == 200) {
      String payload = https.getString();
      payload.trim();
      https.end(); // Cerrar conexión previa antes de hacer la siguiente

      if (payload == "true") {
        Serial.println("🌐 [VERCEL] Orden de apertura remota recibida desde la nube");
        
        // Resetear la orden a false inmediatamente en Firebase
        if (https.begin(client, url)) {
          https.addHeader("Content-Type", "application/json");
          https.PUT("false");
          https.end();
        }
        
        abrirCerradura("WEB_VERCEL");
        return;
      }
    } else {
      https.end();
    }
  }
}

// ════════════════════════════════════════════════════════════
//  ACCIONES DE CERRADURA (SERVO Y LEDS)
// ════════════════════════════════════════════════════════════
void abrirCerradura(const char* metodo) {
  metodoAcceso    = metodo;
  estadoCerradura = "ABIERTO";
  
  digitalWrite(LED_ROJO,  LOW);
  digitalWrite(LED_VERDE, HIGH);
  moverServo(180); // Giro de apertura mecánica
  
  tiempoApertura = millis();

  // Actualizar la nube en tiempo real (Vercel se pone verde al instante)
  actualizarFirebase("ABIERTO", metodoAcceso);
  
  Serial.print("🔓 ABIERTO — Método: "); Serial.println(metodo);
}

void cerrarCerradura() {
  estadoCerradura = "CERRADO";
  
  digitalWrite(LED_ROJO,  HIGH);
  digitalWrite(LED_VERDE, LOW);
  moverServo(0); // Giro de bloqueo mecánico

  // Actualizar la nube en tiempo real (Vercel se pone rojo)
  actualizarFirebase("CERRADO", metodoAcceso);
  
  Serial.println("🔒 Cerradura bloqueada.");
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  // Configuración de LEDs
  pinMode(LED_ROJO,  OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  digitalWrite(LED_ROJO,  HIGH);
  digitalWrite(LED_VERDE, LOW);

  // Posición inicial del Servomotor (bloqueado)
  moverServo(0);

  // Inicialización de SPI personalizado para RC522 (SCK=4, MISO=19, MOSI=23, SS=5)
  // De esta manera el PIN 18 queda 100% exclusivo para el servomotor sin interferencias
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  mfrc522.PCD_Init();

  Serial.println("\n--- NEXUS-7 CONTROL DE ACCESO ---");
  Serial.printf("Servo Pin: %d | RFID SCK: %d, MISO: %d, MOSI: %d, SS: %d\n", 
                PIN_SERVO, RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);

  // Conexión Wi-Fi robusta
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  Serial.printf("Conectando a Wi-Fi: %s", ssid);
  WiFi.begin(ssid, password);
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 25) {
    delay(400);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWi-Fi Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    actualizarFirebase("CERRADO", "INICIO");
  } else {
    Serial.println("\nWi-Fi no conectado. Operando en modo local (RFID seguirá funcionando).");
  }

  Serial.println("Sistema 100% operativo y listo.\n");
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

  // 4. Lectura de tarjeta física RFID (No bloqueante)
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
        digitalWrite(LED_ROJO, LOW); delay(150); digitalWrite(LED_ROJO, HIGH);
      }
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }

  yield();
}
