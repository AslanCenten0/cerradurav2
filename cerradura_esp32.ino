// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  NEXUS-7 — Cerradura Inteligente IoT (Dual-Core Asíncrono v7.0)          ║
// ║  ESP32 + Matriz 4x4 (Core 1) + Firebase HTTPS en Segundo Plano (Core 0) ║
// ║  • Core 1: Teclado ultra-sensible a 1000Hz (CERO teclas perdidas)       ║
// ║  • Core 0: Wi-Fi y Firebase aislados (las peticiones no congelan CPU)   ║
// ║  • Debounce 25ms para eliminar falsos contactos de membrana              ║
// ║  • PIN maestro: "201106" + '#' -> Apertura continua                      ║
// ║  • Tecla '*' (o 'D'): Cierra / Bloquea manualmente la cerradura         ║
// ║  • Servo calibrado: CERRADO = 180° / ABIERTO = 0° (anti-jitter)          ║
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
//  5. ESTADOS Y VARIABLES COMPARTIDAS ENTRE NÚCLEOS
// ────────────────────────────────────────────────────────────
String estadoCerradura = "CERRADO";
String metodoAcceso    = "---";

// Flags de comunicación asíncrona entre Core 0 (Firebase) y Core 1 (Teclado/Servo)
volatile bool ordenWebAbrir           = false;
volatile bool ordenWebCerrar          = false;
volatile bool syncFirebasePendiente    = false;
String        syncEstadoPendiente     = "CERRADO";
String        syncMetodoPendiente     = "INICIO";

TaskHandle_t tareaFirebaseHandle = NULL;

// Declaraciones de funciones
void moverServo(int angulo);
void abrirCerradura(const char* metodo);
void cerrarCerradura();
void enviarActualizacionFirebase(const String& estado, const String& metodo);
void verificarComandosFirebase();
void procesarTeclado();
void tareaFirebaseCore0(void *pvParameters);

// ════════════════════════════════════════════════════════════
//  CONTROL DE SERVO SUAVE Y SIN VIBRACIONES (ANTI-JITTER)
// ════════════════════════════════════════════════════════════
void moverServo(int angulo) {
  miServo.setPeriodHertz(50);
  miServo.attach(PIN_SERVO, 500, 2400);
  miServo.write(angulo);
  delay(500); // 500ms para completar el recorrido mecánico
  miServo.detach(); // Detach apaga los pulsos PWM: elimina vibración, calor y consumo
}

// ════════════════════════════════════════════════════════════
//  ACCIONES DE CERRADURA (EJECUTADAS EN CORE 1)
// ════════════════════════════════════════════════════════════
void abrirCerradura(const char* metodo) {
  metodoAcceso    = metodo;
  estadoCerradura = "ABIERTO";
  
  digitalWrite(LED_ROJO,  LOW);
  digitalWrite(LED_VERDE, HIGH);
  moverServo(ANGULO_ABIERTO); // 0°: Libera cerrojo hacia arriba
  
  // Solicitar actualización a Firebase sin bloquear el Core 1
  syncEstadoPendiente  = "ABIERTO";
  syncMetodoPendiente  = metodoAcceso;
  syncFirebasePendiente = true;

  Serial.printf("🔓 ABIERTO — Método: %s (Apertura continua hasta presionar '*' o 'Cerrar')\n", metodo);
}

void cerrarCerradura() {
  estadoCerradura = "CERRADO";
  bufferClave     = ""; // Limpiar cualquier dígito pendiente
  
  digitalWrite(LED_ROJO,  HIGH);
  digitalWrite(LED_VERDE, LOW);
  moverServo(ANGULO_CERRADO); // 180°: Traba cerrojo hacia abajo

  // Solicitar actualización a Firebase sin bloquear el Core 1
  syncEstadoPendiente  = "CERRADO";
  syncMetodoPendiente  = metodoAcceso;
  syncFirebasePendiente = true;

  Serial.println("🔒 Cerradura bloqueada.");
}

// ════════════════════════════════════════════════════════════
//  PROCESAMIENTO DE TECLADO MATRICIAL 4x4 (CORE 1 - TIEMPO REAL)
// ════════════════════════════════════════════════════════════
void procesarTeclado() {
  char tecla = teclado.getKey();
  if (!tecla) return;

  Serial.printf("⌨️ Tecla detectada: [%c]\n", tecla);

  // 1. SI LA CERRADURA ESTÁ ABIERTA:
  // Pulsar '*' o 'D' bloquea inmediatamente
  if (estadoCerradura == "ABIERTO") {
    if (tecla == '*' || tecla == 'D') {
      Serial.printf("🔒 Botón [%c] pulsado en la matriz. Bloqueando de inmediato...\n", tecla);
      cerrarCerradura();
      return;
    }
  }

  // 2. SI LA CERRADURA ESTÁ CERRADA:
  // Tecla '*' -> Cancelar / borrar buffer
  if (tecla == '*') {
    Serial.println("❌ Entrada cancelada / Buffer borrado.");
    bufferClave = "";
    digitalWrite(LED_ROJO, LOW); delay(80); digitalWrite(LED_ROJO, HIGH);
    return;
  }

  // Tecla '#' -> Confirmar PIN (Enter)
  if (tecla == '#') {
    Serial.printf("🔑 Verificando PIN ingresado: [%s]\n", bufferClave.c_str());
    
    if (bufferClave == CLAVE_MAESTRA) {
      Serial.println("✅ PIN CORRECTO. Desbloqueando...");
      for (int i = 0; i < 2; i++) {
        digitalWrite(LED_VERDE, HIGH); delay(60);
        digitalWrite(LED_VERDE, LOW);  delay(60);
      }
      abrirCerradura("TECLADO_4X4");
    } else {
      Serial.println("⛔ PIN INCORRECTO. Acceso denegado.");
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_ROJO, LOW);  delay(80);
        digitalWrite(LED_ROJO, HIGH); delay(80);
      }
    }
    
    bufferClave = ""; // Limpiar tras evaluar
    return;
  }

  // Dígitos numéricos (1-9, 0, A, B, C)
  if (bufferClave.length() < 10) {
    bufferClave += tecla;
    Serial.printf("PIN actual (%d digitos): ", bufferClave.length());
    for (unsigned int i = 0; i < bufferClave.length(); i++) Serial.print("*");
    Serial.println();
    
    // Feedback táctil instantáneo (solo 15ms para no entorpecer tipeo rápido)
    digitalWrite(LED_VERDE, HIGH);
    delay(15);
    digitalWrite(LED_VERDE, LOW);
  }
}

// ════════════════════════════════════════════════════════════
//  TAREA ASÍNCRONA EN SEGUNDO PLANO (CORE 0 - FIREBASE & WIFI)
// ════════════════════════════════════════════════════════════
void enviarActualizacionFirebase(const String& estado, const String& metodo) {
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

void verificarComandosFirebase() {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(2);
  
  HTTPClient https;
  https.setTimeout(2000);

  // 1. Consultar si enviaron orden de abrir desde la web
  if (estadoCerradura != "ABIERTO") {
    String urlAbrir = FIREBASE_HOST + "/cerradura/comando_abrir.json";
    if (https.begin(client, urlAbrir)) {
      int httpCode = https.GET();
      if (httpCode == 200) {
        String payload = https.getString();
        payload.trim();
        https.end();

        if (payload == "true") {
          Serial.println("🌐 [WEB] Orden de APERTURA recibida.");
          
          if (https.begin(client, urlAbrir)) {
            https.addHeader("Content-Type", "application/json");
            https.PUT("false");
            https.end();
          }
          
          ordenWebAbrir = true; // Notificar a Core 1
          return;
        }
      } else {
        https.end();
      }
    }
  }

  // 2. Consultar si enviaron orden de cerrar desde la web
  if (estadoCerradura == "ABIERTO") {
    String urlCerrar = FIREBASE_HOST + "/cerradura/comando_cerrar.json";
    if (https.begin(client, urlCerrar)) {
      int httpCode = https.GET();
      if (httpCode == 200) {
        String payload = https.getString();
        payload.trim();
        https.end();

        if (payload == "true") {
          Serial.println("🔒 [WEB] Orden de CIERRE MANUAL recibida.");
          
          if (https.begin(client, urlCerrar)) {
            https.addHeader("Content-Type", "application/json");
            https.PUT("false");
            https.end();
          }
          
          ordenWebCerrar = true; // Notificar a Core 1
          return;
        }
      } else {
        https.end();
      }
    }
  }
}

// Bucle en segundo plano dedicado exclusivamente a la red en Core 0
void tareaFirebaseCore0(void *pvParameters) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      // 1. Si hay una actualización de estado pendiente, enviarla
      if (syncFirebasePendiente) {
        syncFirebasePendiente = false;
        enviarActualizacionFirebase(syncEstadoPendiente, syncMetodoPendiente);
      }

      // 2. Verificar comandos remotos en Firebase
      verificarComandosFirebase();
    }
    
    // Intervalo de consulta sin interferir con el teclado
    vTaskDelay(pdMS_TO_TICKS(1200));
  }
}

// ════════════════════════════════════════════════════════════
//  SETUP (INICIALIZACIÓN)
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(300);

  // Configuración de LEDs indicadores
  pinMode(LED_ROJO,  OUTPUT);
  pinMode(LED_VERDE, OUTPUT);
  digitalWrite(LED_ROJO,  HIGH); // Comienza CERRADO
  digitalWrite(LED_VERDE, LOW);

  // Posición inicial del Servomotor: Bloqueado (180°)
  moverServo(ANGULO_CERRADO);

  // Configuración de sensibilidad y debounce del teclado físico
  teclado.setDebounceTime(25); // 25ms para eliminar rebotes y falsos contactos
  teclado.setHoldTime(400);

  Serial.println("\n╔═══════════════════════════════════════════════════════╗");
  Serial.println("║  NEXUS-7 — TECLADO 4x4 DE ALTA SENSIBILIDAD DUAL-CORE ║");
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
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(300);
    Serial.print(".");
    intentos++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWi-Fi Conectado. IP: %s\n", WiFi.localIP().toString().c_str());
    enviarActualizacionFirebase("CERRADO", "INICIO");
  } else {
    Serial.println("\nWi-Fi no conectado. Modo local autónomo activo.");
  }

  // Iniciar la tarea de Firebase aislada en Core 0 para que NUNCA bloquee la lectura de teclas
  xTaskCreatePinnedToCore(
    tareaFirebaseCore0,    // Función de la tarea
    "TareaFirebase",       // Nombre
    8192,                  // Stack en bytes (holgado para TLS/HTTPS)
    NULL,                  // Parámetros
    1,                     // Prioridad
    &tareaFirebaseHandle,  // Handle
    0                      // Ejecutar en Core 0
  );

  Serial.println("✅ Tarea de red aislada en Core 0.");
  Serial.println("✅ Teclado 4x4 corriendo a máxima velocidad en Core 1.");
  Serial.println("Sistema 100% operativo. Abre con [201106#] y cierra con [*].\n");
}

// ════════════════════════════════════════════════════════════
//  LOOP PRINCIPAL (CORE 1 - TIEMPO REAL ULTRA-RÁPIDO)
// ════════════════════════════════════════════════════════════
void loop() {
  // 1. Lectura inmediata de teclas en la matriz 4x4 a máxima frecuencia
  procesarTeclado();

  // 2. Atender órdenes web provenientes de Core 0
  if (ordenWebAbrir) {
    ordenWebAbrir = false;
    abrirCerradura("WEB_MANUAL");
  }
  if (ordenWebCerrar) {
    ordenWebCerrar = false;
    cerrarCerradura();
  }

  yield();
}
