// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  NEXUS-7 — Cerradura Inteligente Autónoma IoT (Versión Teclado 4x4)      ║
// ║  ESP32 + Matriz 4x4 + Servo (Pin 18) + Firebase Realtime Database        ║
// ║  • Matriz 4x4: PIN maestro "201106" + '#' (Autocierre en 15s)            ║
// ║  • Tecla '*': Cancela entrada o cierra cerradura inmediatamente          ║
// ║  • Servo corregido: CERRADO = 180° / ABIERTO = 0° (anti-jitter)          ║
// ║  • Web / Facial: Apertura manual sin límite de tiempo (cierre manual)   ║
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

// Calibración mecánica corregida:
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

// Pines GPIO asignados al ESP32 (100% seguros, con pull-up y sin conflicto de boot):
// Filas (R1 - R4)
byte pinesFilas[FILAS]       = {19, 21, 22, 23}; 
// Columnas (C1 - C4)
byte pinesColumnas[COLUMNAS] = {25, 26, 27, 32}; 

Keypad teclado = Keypad(makeKeymap(teclas), pinesFilas, pinesColumnas, FILAS, COLUMNAS);

// Clave maestra para apertura física por teclado
const String CLAVE_MAESTRA = "201106";
String bufferClave = "";

// ────────────────────────────────────────────────────────────
//  5. ESTADOS Y TIEMPOS
// ────────────────────────────────────────────────────────────
String estadoCerradura = "CERRADO";
String metodoAcceso    = "---";

// Control de autocierre
bool autoCierreActivo               = false;
unsigned long tiempoApertura        = 0;
const unsigned long TIEMPO_AUTOCIERRE_TECLADO = 15000UL; // 15 segundos para Teclado 4x4

// Temporizador para consultar Firebase (cada 1.5s sin bloquear el loop)
unsigned long ultimoCheckFirebase   = 0;
const unsigned long INTERVALO_CHECK = 1500UL;

// Declaraciones de funciones
void moverServo(int angulo);
void abrirCerradura(const char* metodo, bool conAutoCierre);
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
  delay(500); // 500ms suficiente para que el brazo complete el recorrido
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
          
          // Resetear orden a false inmediatamente
          if (https.begin(client, urlAbrir)) {
            https.addHeader("Content-Type", "application/json");
            https.PUT("false");
            https.end();
          }
          
          // Apertura web/facial: SIN autocierre (se cierra manualmente)
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
          Serial.println("🔒 [WEB] Orden de CIERRE MANUAL recibida desde el panel.");
          
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
  moverServo(ANGULO_ABIERTO); // 0°: Libera cerrojo
  
  actualizarFirebase("ABIERTO", metodoAcceso);
  
  if (conAutoCierre) {
    Serial.printf("🔓 ABIERTO — Método: %s (Autocierre en 15s)\n", metodo);
  } else {
    Serial.printf("🔓 ABIERTO — Método: %s (Cierre manual requerido)\n", metodo);
  }
}

void cerrarCerradura() {
  estadoCerradura  = "CERRADO";
  autoCierreActivo = false;
  bufferClave      = ""; // Limpiar cualquier dígito pendiente
  
  digitalWrite(LED_ROJO,  HIGH);
  digitalWrite(LED_VERDE, LOW);
  moverServo(ANGULO_CERRADO); // 180°: Traba cerrojo

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

  // TECLA '*' -> CANCELAR / BORRAR / BLOQUEO INMEDIATO
  if (tecla == '*') {
    if (estadoCerradura == "ABIERTO") {
      Serial.println("🔒 Tecla '*' pulsada mientras estaba abierto: Bloqueando inmediatamente...");
      cerrarCerradura();
    } else {
      Serial.println("❌ Entrada cancelada / Buffer borrado.");
      bufferClave = "";
      // Breve parpadeo de LED rojo como feedback de cancelación
      digitalWrite(LED_ROJO, LOW); delay(100); digitalWrite(LED_ROJO, HIGH);
    }
    return;
  }

  // TECLA '#' -> CONFIRMAR CLAVE (ENTER)
  if (tecla == '#') {
    Serial.printf("🔑 Verificando PIN ingresado: [%s]\n", bufferClave.c_str());
    
    if (bufferClave == CLAVE_MAESTRA) {
      Serial.println("✅ PIN CORRECTO. Desbloqueando por 15 segundos...");
      // Feedback visual LED verde
      for (int i = 0; i < 2; i++) {
        digitalWrite(LED_VERDE, HIGH); delay(80);
        digitalWrite(LED_VERDE, LOW);  delay(80);
      }
      abrirCerradura("TECLADO_4X4", true);
    } else {
      Serial.println("⛔ PIN INCORRECTO. Acceso denegado.");
      // Feedback error LED rojo
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_ROJO, LOW);  delay(100);
        digitalWrite(LED_ROJO, HIGH); delay(100);
      }
    }
    
    bufferClave = ""; // Limpiar después de evaluar
    return;
  }

  // DÍGITOS O LETRAS (1-9, 0, A, B, C, D)
  if (bufferClave.length() < 10) {
    bufferClave += tecla;
    Serial.printf("Entrada actual (%d digitos): ", bufferClave.length());
    for (unsigned int i = 0; i < bufferClave.length(); i++) Serial.print("*");
    Serial.println();
    
    // Pequeño parpadeo verde como feedback táctil
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
    Serial.println("\nWi-Fi no conectado. Operando en modo local autónomo (Teclado 4x4 operativo).");
  }

  Serial.println("Sistema 100% operativo y listo para recibir PIN (Ej: 201106#).\n");
}

// ════════════════════════════════════════════════════════════
//  LOOP PRINCIPAL
// ════════════════════════════════════════════════════════════
void loop() {
  unsigned long ahora = millis();

  // 1. Lectura inmediata de teclas en la matriz 4x4 (sin delay)
  procesarTeclado();

  // 2. Consultar comandos en Firebase (Abrir / Cerrar remoto)
  if (ahora - ultimoCheckFirebase >= INTERVALO_CHECK) {
    ultimoCheckFirebase = ahora;
    verificarComandosFirebase();
  }

  // 3. Autocierre de 15 segundos SOLO para apertura por Teclado 4x4
  if (estadoCerradura == "ABIERTO" && autoCierreActivo) {
    if (ahora - tiempoApertura >= TIEMPO_AUTOCIERRE_TECLADO) {
      Serial.println("⏰ [TECLADO] 15 segundos cumplidos. Bloqueando servo automáticamente...");
      cerrarCerradura();
    }
  }

  yield();
}
