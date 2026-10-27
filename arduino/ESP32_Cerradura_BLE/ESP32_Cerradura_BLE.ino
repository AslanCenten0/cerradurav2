/*
 * ═══════════════════════════════════════════════════════════════════════════
 *  ESP32_Cerradura_BLE.ino — Servidor Bluetooth Low Energy (BLE) para Cerradura
 *  Recibe el comando "abrir" desde la aplicación Web (Vercel) y acciona el servo
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * Requisitos en Arduino IDE:
 * 1. Placa: ESP32 Dev Module (desde el Gestor de Tarjetas ESP32 de Espressif)
 * 2. Librería: "ESP32Servo" (instalar desde: Herramientas -> Administrar Bibliotecas -> buscar "ESP32Servo")
 * 
 * Conexión de Hardware (ejemplo estándar):
 * - Servomotor SG90 / MG90S:
 *     - Cable Naranja / Amarillo (Señal PWM) -> GPIO 18 (o el definido en SERVO_PIN)
 *     - Cable Rojo (VCC)                     -> 5V (o VIN con fuente externa)
 *     - Cable Marrón / Negro (GND)           -> GND común del ESP32
 * - LED indicador (opcional):
 *     - GPIO 2 (LED interno de la mayoría de placas ESP32)
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ESP32Servo.h>

// ── CONFIGURACIÓN DE PINES Y PARÁMETROS ──
#define SERVO_PIN        18      // Pin PWM donde está conectada la señal del servomotor
#define LED_INDICADOR    2       // Pin del LED integrado (GPIO 2 en la mayoría de ESP32)

#define ANGULO_CERRADO   0       // Grados del servomotor para la posición cerrada
#define ANGULO_ABIERTO   180     // Grados del servomotor para la posición abierta
#define TIEMPO_ABIERTO   4000    // Milisegundos que permanece abierta la cerradura antes de volver a cerrar

// ── IDENTIFICADORES BLUETOOTH (Deben coincidir exactamente con js/bluetooth.js) ──
#define DEVICE_NAME         "ESP32_Cerradura"
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Objetos globales
Servo miServo;
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;

bool dispositivoConectado = false;
bool dispositivoConectadoAnterior = false;
volatile bool solicitudApertura = false;

// ── CALLBACKS DEL SERVIDOR BLE (Gestión de conexión y reconexión automática) ──
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      dispositivoConectado = true;
      digitalWrite(LED_INDICADOR, HIGH);
      Serial.println("\n[BLE] ¡Dispositivo cliente conectado (Web Chrome / PWA)!");
    }

    void onDisconnect(BLEServer* pServer) {
      dispositivoConectado = false;
      digitalWrite(LED_INDICADOR, LOW);
      Serial.println("\n[BLE] Cliente desconectado. Reiniciando publicidad (advertising)...");
      
      // Reiniciar la publicidad inmediatamente para permitir nuevas reconexiones
      pServer->startAdvertising();
      Serial.println("[BLE] Publicidad reiniciada. Listo para volver a conectar.");
    }
};

// ── CALLBACKS DE LA CARACTERÍSTICA (Recepción de comandos desde la Web) ──
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      // Leer valor enviado por Web Bluetooth
      std::string valorStd = pCharacteristic->getValue();
      String comando = String(valorStd.c_str());
      comando.trim();

      Serial.print("[BLE RECIBIDO]: \"");
      Serial.print(comando);
      Serial.println("\"");

      // Validar si el comando contiene la orden de apertura
      if (comando.equalsIgnoreCase("abrir") || comando.indexOf("abrir") >= 0 || comando.indexOf("ABRIR") >= 0) {
        Serial.println(">>> ¡COMANDO VÁLIDO RECIBIDO! Programando apertura de cerradura...");
        solicitudApertura = true;
      } else {
        Serial.println("Comando no reconocido. Se esperaba: 'abrir'");
      }
    }
};

// ── FUNCIÓN DE ACCIONAMIENTO DEL SERVOMOTOR ──
void ejecutarAperturaCerradura() {
  Serial.println("=========================================");
  Serial.println(" [MECANISMO] >> DESBLOQUEANDO CERRADURA <<");
  Serial.println("=========================================");

  // 1. Mover servo al ángulo de apertura
  miServo.write(ANGULO_ABIERTO);
  
  // Parpadeo rápido del LED indicador mientras está abierta
  for (int i = 0; i < (TIEMPO_ABIERTO / 200); i++) {
    digitalWrite(LED_INDICADOR, !digitalRead(LED_INDICADOR));
    delay(200);
  }

  // 2. Regresar servo a la posición de reposo (bloqueada)
  Serial.println("[MECANISMO] Cerradura rebloqueada (posición inicial).");
  miServo.write(ANGULO_CERRADO);
  
  if (dispositivoConectado) {
    digitalWrite(LED_INDICADOR, HIGH);
  } else {
    digitalWrite(LED_INDICADOR, LOW);
  }
}

void setup() {
  // Inicialización de la consola serie
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- INICIANDO CERRADURA INTELIGENTE ESP32 BLE ---");

  // Configurar LED indicador
  pinMode(LED_INDICADOR, OUTPUT);
  digitalWrite(LED_INDICADOR, LOW);

  // Configuración del Servomotor con timer ESP32
  ESP32PWM::allocateTimer(0);
  miServo.setPeriodHertz(50);             // Estándar 50Hz para servos analógicos SG90/MG995
  miServo.attach(SERVO_PIN, 500, 2400);   // Rango de pulsos en microsegundos (500us a 2400us)
  
  // Posición inicial: cerrado
  miServo.write(ANGULO_CERRADO);
  Serial.print("[SERVO] Inicializado en GPIO ");
  Serial.print(SERVO_PIN);
  Serial.print(" | Posición: ");
  Serial.print(ANGULO_CERRADO);
  Serial.println("°");

  // ── INICIALIZACIÓN DE BLUETOOTH LOW ENERGY (BLE) ──
  Serial.print("[BLE] Inicializando servidor con nombre: ");
  Serial.println(DEVICE_NAME);
  BLEDevice::init(DEVICE_NAME);

  // Crear Servidor BLE
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Crear Servicio BLE
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Crear Característica BLE con permisos de Escritura y Lectura
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());

  // Iniciar servicio
  pService->start();

  // Iniciar Publicidad BLE para ser visible ante Chrome / Web Bluetooth
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // Ayuda con compatibilidad en navegadores e iOS/Android
  pAdvertising->setMinPreferred(0x12);
  
  BLEDevice::startAdvertising();
  Serial.println("[BLE] Servidor en línea y anunciando. Esperando conexión desde Vercel...");
  Serial.println("-----------------------------------------------------------------");
}

void loop() {
  // 1. Ejecutar apertura fuera de la interrupción BLE para máxima estabilidad
  if (solicitudApertura) {
    solicitudApertura = false;
    ejecutarAperturaCerradura();
  }

  // 2. Control de reconexión continua en segundo plano
  if (!dispositivoConectado && dispositivoConectadoAnterior) {
    delay(500); // Pequeña pausa para permitir que el stack BLE se estabilice
    pServer->startAdvertising();
    Serial.println("[BLE] Publicidad activa tras desconexión.");
    dispositivoConectadoAnterior = dispositivoConectado;
  }

  if (dispositivoConectado && !dispositivoConectadoAnterior) {
    dispositivoConectadoAnterior = dispositivoConectado;
  }

  delay(20);
}
