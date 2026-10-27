# 📑 Informe Técnico de Arquitectura IoT
## Cerradura Inteligente NEXUS-7: Integración Vercel, ESP32 y Telegram Sin Dependencia Local

---

## 1. Resumen Ejecutivo y Objetivo del Sistema

El objetivo de esta evaluación es diseñar una arquitectura donde la cerradura inteligente opere de forma **100% autónoma**, cumpliendo dos requisitos fundamentales:
1. **Independencia de hardware (Sin PC):** El ESP32 funciona conectado exclusivamente a una fuente de alimentación de 5V (cargador de celular USB común), ejecutando su firmware de forma aislada.
2. **Independencia de red (Sin requerir el mismo Wi-Fi):** El usuario puede controlar la cerradura y recibir alertas en tiempo real desde cualquier lugar del mundo (con datos móviles 4G/5G o cualquier otra red Wi-Fi), sin que el teléfono y el ESP32 deban compartir la misma red local.

---

## 2. Diagnóstico del Escenario Actual y Problemática de Red

```
[ Celular en 4G o Red Externa ] ──► [ Web en Vercel (Nube Pública HTTPS) ]
                                                │
                                                ▼  ¿Conexión Directa?
                                                ❌ IMPOSIBLE POR RED LOCAL
                                                ▼
[ Router Escolar / Hogar (NAT) ] ──► [ ESP32 (IP Privada 192.168.x.x) ]
```

### ¿Por qué una web pública no puede hablar directamente con el ESP32?
* **Redes privadas detrás de NAT:** El ESP32 obtiene una dirección IP local (ej. `192.168.1.50`). Esta dirección solo existe dentro de esa red Wi-Fi. Desde el exterior de la escuela o tu casa es invisible.
* **Firewalls y políticas escolares:** En redes educativas o corporativas no se pueden abrir puertos (Port Forwarding), ni asignar IPs públicas estáticas.
* **Seguridad del Navegador (Mixed Content):** Vercel sirve la página bajo `https://`. Si el navegador intentara hacer una petición a `http://192.168...`, el motor de seguridad del navegador la bloquearía automáticamente.

---

## 3. Evaluación Técnica: Usar Telegram como Puente entre Vercel y ESP32

Tu propuesta consiste en utilizar los servidores de Telegram (`api.telegram.org`) como el punto de encuentro central:

### A. Flujo de Salida: ESP32 ➔ Telegram ➔ Usuario (Viabilidad: 100% ✅)
* **Cómo funciona:** Cuando el ESP32 abre la cerradura (por tarjeta RFID o biometría facial Bluetooth), hace una petición HTTPS hacia la API de Telegram:
  $$\text{ESP32} \xrightarrow{\text{HTTPS POST}} \text{api.telegram.org} \xrightarrow{\text{Push}} \text{App Telegram del Usuario}$$
* **Veredicto:** Excelente. Ya está implementado en tu código de FreeRTOS en el Core 0. Funciona en cualquier Wi-Fi escolar sin problemas.

### B. Flujo de Entrada: Vercel ➔ Telegram ➔ ESP32 (Viabilidad: Condicional ⚠️)
* **El desafío técnico:** Cuando un usuario presiona *"Abrir"* en la web de Vercel y la web le pide a la API de Telegram enviar un mensaje:
  $$\text{Web Vercel} \xrightarrow{\text{sendMessage}} \text{Chat de Telegram}$$
  El bot publica el texto en el chat. Sin embargo, el ESP32 escucha mediante la función `bot.getUpdates()`.
* **La limitación de la API de Telegram:** **Los bots de Telegram no leen sus propios mensajes.** Telegram descarta los mensajes enviados por el mismo Bot Token para evitar bucles infinitos de auto-respuesta.
* **La solución si se usa Telegram:** Para que el ESP32 reciba la orden, el mensaje debe ser enviado por una cuenta de usuario o mediante un **Callback Query** (botones interactivos en el chat) generado por una persona, no por el propio bot.
* **Latencia de Polling:** El ESP32 debe consultar a Telegram cada 1 o 2 segundos. Esto introduce una demora de respuesta de **1.5 a 3 segundos** entre que pulsas el botón y el servomotor gira.

---

## 4. Comparativa de Opciones de Arquitectura

| Criterio | Opción 1: Todo por Telegram | Opción 2: Nube IoT (Firebase) + Alertas Telegram |
| :--- | :--- | :--- |
| **Independencia de PC** | Total (funciona a 5V) | Total (funciona a 5V) |
| **Independencia de Wi-Fi** | Total (opera en cualquier red) | Total (opera en cualquier red) |
| **Velocidad de Respuesta** | Media (1 a 3 segundos) | Ultrarrápida (< 200 milisegundos) |
| **Visualización en Vivo** | Solo dentro de la app Telegram | En tiempo real en la página de Vercel |
| **Complejidad de Código** | Media (manejo de strings) | Baja (JSON estructurado) |
| **Consumo de Memoria ESP32** | Alto (TLS constante en Core 0) | Moderado / Eficiente |

---

## 5. Arquitectura Híbrida Recomendada

```
┌────────────────────────────────────────────────────────────────────────┐
│                        CERRADURA INTELIGENTE                           │
├────────────────────────────────────┬───────────────────────────────────┤
│        ACCESO DE CORTA DISTANCIA   │      ACCESO REMOTO / GLOBAL       │
│        (Frente a la puerta)        │      (Cualquier parte del mundo)  │
├────────────────────────────────────┼───────────────────────────────────┤
│ • Reconocimiento Facial en Web     │ • Botón Remoto en Web de Vercel   │
│ • Comunicación Web Bluetooth (BLE) │ • Bot interactivo de Telegram     │
│ • No depende de internet ni Wi-Fi  │ • Notificaciones push inmediatas  │
│ • Apertura instantánea sin lag     │ • Registro de auditoría en nube   │
└────────────────────────────────────┴───────────────────────────────────┘
```

1. **Cuando estás frente a la cerradura:**
   Abres la web en tu celular, la cámara valida tu rostro y mediante **Web Bluetooth** abre la cerradura en 50 milisegundos sin tocar internet.
2. **Cuando estás lejos (en la calle o en otra ciudad):**
   * Puedes abrir desde la **Web de Vercel** o directamente desde el chat de **Telegram** presionando el botón *"🔓 Abrir Puerta"*.
   * Cada apertura reporta al instante a Telegram: *"🔓 Puerta abierta — Método: FACIAL / WEB / RFID"*.
