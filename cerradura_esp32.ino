// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  NEXUS-7 — Cerradura Inteligente Autónoma IoT (Apertura Continua 4x4)   ║
// ║  ESP32 + Matriz 4x4 + Servo (Pin 18) + Firebase Realtime Database        ║
// ║  • Matriz 4x4: PIN maestro "201106" + '#' -> Apertura continua infinita ║
// ║  • Tecla '*' (o 'D'): Bloquea / Cierra manualmente la cerradura         ║
// ║  • Servo mecánico: CERRADO = 180° / ABIERTO = 0° (anti-jitter)           ║
// ║  • Web / Facial / Teclado: Cierre manual (se mantiene abierta hasta     ║
// ║    pulsar '*' en el teclado o 'Cerrar' en la web)                        ║
// ╚══════════════════════════════════════════════════════════════════════════╝

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <Keypad.h>
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
//  3. HARDWARE: SERVO Y CALIBRACIÓN DE ÁNGULOS
// ────────────────────────────────────────────────────────────
#define PIN_SERVO 18
Servo miServo;

// Calibración mecánica:
// CERRADO: 180° (traba hacia abajo)
// ABIERTO:   0° (libera el cerrojo hacia arriba)
const int ANGULO_CERRADO = 180;
const int ANGULO_ABIERTO = 0;

// Indicadores LED
#define LED_ROJO  15  // Encendido cuando está CERRADO / Alarma
#define LED_VERDE  2  // Encendido cuando está ABIERTO / Acceso OK

// ────────────────────────────────────────────────────────────
//  4. HARDWARE: MATRIZ DE TECLADO 4x4
// ────────────────────────────────────────────────────────────
const byte FILAS = 4;
const byte COLUMNAS = 4;

char teclas[FILAS][COLUMNAS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// Pines GPIO asignados al ESP32:
byte pinesFilas[FILAS]       = {19, 21, 22, 23}; // R1, R2, R3, R4
byte pinesColumnas[COLUMNAS] = {25, 26, 27, 32}; // C1, C2, C3, C4

Keypad teclado = Keypad(makeKeymap(teclas), pinesFilas, pinesColumnas, FILAS, COLUMNAS);

// Clave maestra para apertura física por teclado
const String CLAVE_MAESTRA = "201106";
String bufferClave = "";

// ────────────────────────────────────────────────────────────
//  5. ESTADOS Y TIEMPOS
// ────────────────────────────────────────────────────────────
String estadoCerradura = "CERRADO";
String metodoAcceso    = "---";

// Temporizador para consultar Firebase (cada 1.5s sin bloquear el loop)
unsigned long ultimoCheckFirebase   = 0;
const unsigned long INTERVALO_CHECK = 1500UL;

// Declaraciones de funciones
void moverServo(int angulo);
void abrirCerradura(const char* metodo);
void cerrarCerradura();
void actualizarFirebase(const String& estado, const String& metodo);
void verificarComandosFirebase();
void procesarTeclado();

// ════════════════════════════════════════════════════════════
//  CONTROL DE SERVO SUAVE Y SIN VIBRACIONES (ANTI-JITTER)
// ════════════════════════════════════════════════════════════
void moverServo(int angulo) {
  miServo.setPeriodHertz(50);
  miServo.attach(PIN_SERVO, 500, 2400);
  miServo.write(angulo);
  delay(500); // 500ms suficiente para que el brazo complete el recorrido mecánico
  miServo.detach(); // Detach apaga los pulsos PWM: elimina vibración, calor y consumo
}

// ════════════════════════════════════════════════════════════
//  SINCRONIZACIÓN CON FIREBASE (NUBE)
// ════════════════════════════════════════════════════════════
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

  // 1. Si está CERRADO: Consultar si enviaron orden de abrir desde la web
  if (estadoCerradura != "ABIERTO") {
    String urlAbrir = FIREBASE_HOST + "/cerradura/comando_abrir.json";
    if (https.begin(client, urlAbrir)) {
      int httpCode = https.GET();
      if (httpCode == 200) {
        String payload = https.getString();
        payload.trim();
        https.end();

        if (payload == "true") {
          Serial.println("🌐 [WEB] Orden de APERTURA recibida desde el panel.");
          
          if (https.begin(client, urlAbrir)) {
            https.addHeader("Content-Type", "application/json");
            https.PUT("false");
            https.end();
          }
          
          // Apertura manual web (se mantiene abierta hasta pulsar Cerrar)
          abrirCerradura("WEB_MANUAL");
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
          Serial.println("🔒 [WEB] Orden de CIERRE MANUAL recibida desde el panel.");
          
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
void abrirCerradura(const char* metodo) {
  metodoAcceso    = metodo;
  estadoCerradura = "ABIERTO";
  
  digitalWrite(LED_ROJO,  LOW);
  digitalWrite(LED_VERDE, HIGH);
  moverServo(ANGULO_ABIERTO); // 0°: Libera cerrojo hacia arriba
  
  actualizarFirebase("ABIERTO", metodoAcceso);
  Serial.printf("🔓 ABIERTO — Método: %s (Apertura continua hasta presionar '*' o 'Cerrar')\n", metodo);
}

void cerrarCerradura() {
  estadoCerradura = "CERRADO";
  bufferClave     = ""; // Limpiar cualquier dígito pendiente
  
  digitalWrite(LED_ROJO,  HIGH);
  digitalWrite(LED_VERDE, LOW);
  moverServo(ANGULO_CERRADO); // 180°: Traba cerrojo hacia abajo

  actualizarFirebase("CERRADO", metodoAcceso);
  Serial.println("🔒 Cerradura bloqueada.");
}

// ════════════════════════════════════════════════════════════
//  PROCESAMIENTO DE TECLADO MATRICIAL 4x4
// ════════════════════════════════════════════════════════════
void procesarTeclado() {
  char tecla = teclado.getKey();
  if (!tecla) return;

  Serial.printf("⌨️ Tecla pulsada: [%c]\n", tecla);

  // 1. SI LA CERRADURA ESTÁ ABIERTA:
  // Al pulsar '*' o 'D' se bloquea/cierra inmediatamente
  if (estadoCerradura == "ABIERTO") {
    if (tecla == '*' || tecla == 'D') {
      Serial.printf("🔒 Botón manual de cierre [%c] presionado en la matriz. Bloqueando...\n", tecla);
      cerrarCerradura();
      return;
    }
  }

  // 2. SI LA CERRADURA ESTÁ CERRADA:
  // Tecla '*' -> Cancelar / borrar buffer
  if (tecla == '*') {
    Serial.println("❌ Entrada cancelada / Buffer borrado.");
    bufferClave = "";
    digitalWrite(LED_ROJO, LOW); delay(100); digitalWrite(LED_ROJO, HIGH);
    return;
  }

  // Tecla '#' -> Confirmar PIN (Enter)
  if (tecla == '#') {
    Serial.printf("🔑 Verificando PIN ingresado: [%s]\n", bufferClave.c_str());
    
    if (bufferClave == CLAVE_MAESTRA) {
      Serial.println("✅ PIN CORRECTO. Desbloqueando de forma continua (abierta hasta presionar '*')...");
      for (int i = 0; i < 2; i++) {
        digitalWrite(LED_VERDE, HIGH); delay(80);
        digitalWrite(LED_VERDE, LOW);  delay(80);
      }
      abrirCerradura("TECLADO_4X4"); // Abre sin temporizador de cierre
    } else {
      Serial.println("⛔ PIN INCORRECTO. Acceso denegado.");
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_ROJO, LOW);  delay(100);
        digitalWrite(LED_ROJO, HIGH); delay(100);
      }
    }
    
    bufferClave = ""; // Limpiar tras verificar
    return;
  }

  // Dígitos numéricos o caracteres
  if (bufferClave.length() < 10) {
    bufferClave += tecla;
    Serial.printf("Entrada actual (%d digitos): ", bufferClave.length());
    for (unsigned int i = 0; i < bufferClave.length(); i++) Serial.print("*");
    Serial.println();
    
    // Feedback táctil con parpadeo suave del LED verde
    digitalWrite(LED_VERDE, HIGH);
    delay(40);
    digitalWrite(LED_VERDE, LOW);
  }
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
  digitalWrite(LED_ROJO,  HIGH); // Comienza CERRADO
  digitalWrite(LED_VERDE, LOW);

  // Posición inicial del Servomotor: Bloqueado (180°)
  moverServo(ANGULO_CERRADO);

  Serial.println("\n╔═══════════════════════════════════════════════════════╗");
  Serial.println("║   NEXUS-7 CONTROL DE ACCESO — TECLADO MATRIZ 4x4     ║");
  Serial.println("╚═══════════════════════════════════════════════════════╝");
  Serial.printf("Servo GPIO: %d (Cerrado: %d°, Abierto: %d°)\n", PIN_SERVO, ANGULO_CERRADO, ANGULO_ABIERTO);
  Serial.printf("Filas Teclado:    [%d, %d, %d, %d]\n", pinesFilas[0], pinesFilas[1], pinesFilas[2], pinesFilas[3]);
  Serial.printf("Columnas Teclado: [%d, %d, %d, %d]\n", pinesColumnas[0], pinesColumnas[1], pinesColumnas[2], pinesColumnas[3]);

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
    Serial.println("\nWi-Fi no conectado. Modo local autónomo (Teclado 4x4 operativo).");
  }

  Serial.println("Sistema 100% operativo. Abre con [201106#] y cierra con [*].\n");
}

// ════════════════════════════════════════════════════════════
//  LOOP PRINCIPAL
// ════════════════════════════════════════════════════════════
void loop() {
  unsigned long ahora = millis();

  // 1. Lectura inmediata de teclas en la matriz 4x4
  procesarTeclado();

  // 2. Consultar comandos en Firebase (Abrir / Cerrar remoto)
  if (ahora - ultimoCheckFirebase >= INTERVALO_CHECK) {
    ultimoCheckFirebase = ahora;
    verificarComandosFirebase();
  }

  yield();
}
