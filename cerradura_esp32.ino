// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  NEXUS-7 — Cerradura Inteligente Autónoma IoT (Versión Estable v6.0)     ║
// ║  ESP32 + RFID RC522 + Servo (Pin 18) + Firebase Realtime Database        ║
// ║  • Cierre manual web: Se mantiene abierta hasta presionar "Cerrar"       ║
// ║  • Cierre automático RFID: Temporizador de 15 segundos                  ║
// ║  • Servo en Pin 18 con detach anti-jitter (cero vibración/calor)         ║
// ║  • SPI RC522 en Pin 4 (SCK) para no colisionar con el servo              ║
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
// IMPORTANTE: SCK asignado al Pin 4 para que NO colisione con el servo en Pin 18
#define RFID_SCK  4   // SCK -> Pin 4
#define RFID_MISO 19  // MISO -> Pin 19
#define RFID_MOSI 23  // MOSI -> Pin 23
#define RFID_SS   5   // SDA/SS -> Pin 5
#define RFID_RST  22  // RST -> Pin 22

MFRC522 mfrc522(RFID_SS, RFID_RST);
byte usuarioAutorizado[4] = {0x62, 0x55, 0x14, 0x5C}; // UID de tarjeta física autorizada

// Indicadores LED
#define LED_ROJO  15
#define LED_VERDE  2

// ────────────────────────────────────────────────────────────
//  4. ESTADOS Y TIEMPOS
// ────────────────────────────────────────────────────────────
String estadoCerradura = "CERRADO";
String metodoAcceso    = "---";

// Control de autocierre
bool autoCierreActivo              = false;
unsigned long tiempoApertura       = 0;
const unsigned long TIEMPO_RFID_AUTOCIERRE = 15000UL; // 15 segundos SOLO para RFID

// Temporizador para consultar Firebase (cada 1.5s sin bloquear el loop)
unsigned long ultimoCheckFirebase   = 0;
const unsigned long INTERVALO_CHECK = 1500UL;

// Modo RFID Libre (Bypass por 60 segundos)
bool          rfidLibre      = false;
unsigned long tRfidLibre     = 0;
const unsigned long DUR_RFID_LIBRE = 60000UL;

// Declaraciones de funciones
void moverServo(int angulo);
void abrirCerradura(const char* metodo, bool conAutoCierre);
void cerrarCerradura();
void actualizarFirebase(const String& estado, const String& metodo);
void verificarComandosFirebase();

// ════════════════════════════════════════════════════════════
//  CONTROL DE SERVO SUAVE Y SIN VIBRACIONES (ANTI-JITTER)
// ════════════════════════════════════════════════════════════
void moverServo(int angulo) {
  miServo.setPeriodHertz(50);
  miServo.attach(PIN_SERVO, 500, 2400);
  miServo.write(angulo);
  delay(500); // 500ms es suficiente para que el brazo complete el recorrido
  miServo.detach(); // Detach apaga los pulsos PWM: elimina vibración, calor y consumo
}

// ════════════════════════════════════════════════════════════
//  SINCRONIZACIÓN CON FIREBASE (NUBE)
// ════════════════════════════════════════════════════════════

// Actualiza el estado en la nube para que la web en Vercel refleje el estado en vivo
void actualizarFirebase(const String& estado, const String& metodo) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(2);
  
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

// Revisa si desde la web presionaron "Abrir" o "Cerrar"
void verificarComandosFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(2);
  
  HTTPClient https;
  https.setTimeout(2000);

  // 1. Si está CERRADO: Consultar si enviaron orden de abrir
  if (estadoCerradura != "ABIERTO") {
    String urlAbrir = FIREBASE_HOST + "/cerradura/comando_abrir.json";
    if (https.begin(client, urlAbrir)) {
      int httpCode = https.GET();
      if (httpCode == 200) {
        String payload = https.getString();
        payload.trim();
        https.end();

        if (payload == "true") {
          Serial.println("🌐 [VERCEL] Orden de APERTURA recibida desde la web.");
          
          // Resetear orden a false inmediatamente
          if (https.begin(client, urlAbrir)) {
            https.addHeader("Content-Type", "application/json");
            https.PUT("false");
            https.end();
          }
          
          // Apertura manual web: NO tiene autocierre (se cierra manualmente)
          abrirCerradura("WEB_MANUAL", false);
          return;
        }
      } else {
        https.end();
      }
    }
  }

  // 2. Si está ABIERTO: Consultar si enviaron orden de cerrar manualmente
  if (estadoCerradura == "ABIERTO") {
    String urlCerrar = FIREBASE_HOST + "/cerradura/comando_cerrar.json";
    if (https.begin(client, urlCerrar)) {
      int httpCode = https.GET();
      if (httpCode == 200) {
        String payload = https.getString();
        payload.trim();
        https.end();

        if (payload == "true") {
          Serial.println("🔒 [VERCEL] Orden de CIERRE MANUAL recibida desde la web.");
          
          // Resetear orden a false inmediatamente
          if (https.begin(client, urlCerrar)) {
            https.addHeader("Content-Type", "application/json");
            https.PUT("false");
            https.end();
          }
          
          cerrarCerradura();
          return;
        }
      } else {
        https.end();
      }
    }
  }
}

// ════════════════════════════════════════════════════════════
//  ACCIONES DE CERRADURA (SERVO Y LEDS)
// ════════════════════════════════════════════════════════════
void abrirCerradura(const char* metodo, bool conAutoCierre) {
  metodoAcceso     = metodo;
  estadoCerradura  = "ABIERTO";
  autoCierreActivo = conAutoCierre;
  tiempoApertura   = millis();
  
  digitalWrite(LED_ROJO,  LOW);
  digitalWrite(LED_VERDE, HIGH);
  moverServo(180); // Giro de apertura mecánica del servo
  
  actualizarFirebase("ABIERTO", metodoAcceso);
  
  if (conAutoCierre) {
    Serial.printf("🔓 ABIERTO — Método: %s (Cierre automático en 15s)\n", metodo);
  } else {
    Serial.printf("🔓 ABIERTO — Método: %s (Cierre manual requerido)\n", metodo);
  }
}

void cerrarCerradura() {
  estadoCerradura  = "CERRADO";
  autoCierreActivo = false;
  
  digitalWrite(LED_ROJO,  HIGH);
  digitalWrite(LED_VERDE, LOW);
  moverServo(0); // Giro de bloqueo mecánico a 0 grados

  actualizarFirebase("CERRADO", metodoAcceso);
  
  Serial.println("🔒 Cerradura bloqueada.");
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  // Configuración de LEDs indicadores
  pinMode(LED_ROJO,  OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  digitalWrite(LED_ROJO,  HIGH);
  digitalWrite(LED_VERDE, LOW);

  // Posición inicial del Servomotor (bloqueado a 0 grados)
  moverServo(0);

  // SPI para lector RFID RC522 (SCK=4, MISO=19, MOSI=23, SS=5)
  // De esta forma el Pin 18 queda 100% libre para el servo
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  mfrc522.PCD_Init();

  Serial.println("\n--- NEXUS-7 CONTROL DE ACCESO v6.0 ---");
  Serial.printf("Servo Pin: %d | RFID SCK: %d, MISO: %d, MOSI: %d, SS: %d\n", 
                PIN_SERVO, RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);

  // Conexión Wi-Fi
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

  // 1. Consultar comandos en Firebase (Abrir / Cerrar)
  if (ahora - ultimoCheckFirebase >= INTERVALO_CHECK) {
    ultimoCheckFirebase = ahora;
    verificarComandosFirebase();
  }

  // 2. Cierre automático de 15 segundos ÚNICAMENTE para apertura por RFID
  if (estadoCerradura == "ABIERTO" && autoCierreActivo) {
    if (ahora - tiempoApertura >= TIEMPO_RFID_AUTOCIERRE) {
      Serial.println("⏰ [RFID] 15 segundos cumplidos. Bloqueando servo automáticamente...");
      cerrarCerradura();
    }
  }

  // 3. Expiración de modo RFID Libre (Bypass 60 segundos)
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
      // Con RFID Libre: apertura con temporizador de 15s
      abrirCerradura("RFID_LIBRE", true);
    } else {
      bool autorizada = (mfrc522.uid.size == 4);
      for (byte i = 0; i < 4 && autorizada; i++) {
        if (mfrc522.uid.uidByte[i] != usuarioAutorizado[i]) autorizada = false;
      }

      if (autorizada) {
        // Tarjeta autorizada: abre el servo con autocierre de 15 segundos
        abrirCerradura("RFID", true);
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
