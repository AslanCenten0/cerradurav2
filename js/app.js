/**
 * ═════════════════════════════════════════════════════════════════════
 *  app.js — Orquestador Principal de la Aplicación
 *  Enlaza la interfaz minimalista, el motor biométrico y Web Bluetooth
 * ═════════════════════════════════════════════════════════════════════
 */

import { BluetoothController } from './bluetooth.js';
import { FaceRecognitionEngine } from './recognition.js';
import { initBackgroundGrid, initSpotlightCards, initClickSparks, decryptText } from './effects.js';

// Exponer decryptText para que el panel de control pueda usarlo
window.decryptText = decryptText;

// Cooldown de 6 segundos tras apertura exitosa
const COOLDOWN_DURATION = 6000;

class NexusApp {
  constructor() {
    this.bt = new BluetoothController();
    this.engine = null;

    this.cooldownUntil = 0;
    this.audioContext = null;

    // Referencias al DOM
    this.dom = {
      video: document.getElementById('webcam'),
      canvas: document.getElementById('overlayCanvas'),
      loader: document.getElementById('loaderSplash'),
      loaderMsg: document.getElementById('loaderMessage'),
      toast: document.getElementById('feedbackToast'),
      toastMsg: document.getElementById('toastMessage'),
      statusPulse: document.getElementById('statusPulse'),
      statusText: document.getElementById('statusText'),
      cooldownTimer: document.getElementById('cooldownTimer'),
      fpsCounter: document.getElementById('fpsCounter'),
      scanReticle: document.getElementById('scanReticle'),
      
      btnBluetooth: document.getElementById('btnBluetooth'),
      btStatusLabel: document.getElementById('btStatusLabel'),
      btnOpenUsers: document.getElementById('btnOpenUsers'),
      badgeUserCount: document.getElementById('badgeUserCount'),
      btnManualUnlock: document.getElementById('btnManualUnlock'),
      btnRegisterFace: document.getElementById('btnRegisterFace'),
      
      // Modales
      modalUsers: document.getElementById('modalUsers'),
      btnCloseModal: document.getElementById('btnCloseModal'),
      userList: document.getElementById('userList'),
      btnClearAllUsers: document.getElementById('btnClearAllUsers'),
      
      modalPromptName: document.getElementById('modalPromptName'),
      inputUserName: document.getElementById('inputUserName'),
      btnCancelEnroll: document.getElementById('btnCancelEnroll'),
      btnConfirmEnroll: document.getElementById('btnConfirmEnroll'),

      btnInstallApp: document.getElementById('btnInstallApp'),
      btnEnableCamera: document.getElementById('btnEnableCamera'),
    };
  }

  async init() {
    // 0. Inicializar Efectos Visuales React Bits
    initBackgroundGrid('bgCanvas');
    initSpotlightCards();
    initClickSparks();

    const brandName = document.querySelector('.brand-name');
    if (brandName) {
      decryptText(brandName, "NEXUS ACCESS", 900, 35);
    }

    // 1. Inicializar PWA Service Worker
    this.registerServiceWorker();

    // 2. Conectar Eventos de Bluetooth
    this.setupBluetooth();

    // 3. Inicializar Cámara y Reconocimiento Facial
    this.engine = new FaceRecognitionEngine(this.dom.video, this.dom.canvas);
    this.engine.onStatusMessage = (msg) => {
      this.dom.loaderMsg.textContent = msg;
    };

    try {
      await this.engine.loadModels();
    } catch (errModels) {
      console.warn("Advertencia cargando modelos:", errModels);
    }

    await this.tryStartCamera();

    // 4. Conectar Eventos de la Interfaz
    this.bindUIEvents();
    this.updateUserBadge();

    // 5. Medidor de FPS
    this.startFPSMeter();
  }

  async tryStartCamera() {
    try {
      this.dom.loaderMsg.textContent = "Conectando cámara...";
      await this.engine.startCamera();

      // Ocultar splash loader suavemente
      this.dom.loader.classList.add('hidden');
      this.showToast("Cámara y biometría activas");

      // Configurar escucha de detección
      this.engine.onFaceDetected = (result) => this.handleFaceDetection(result);
      this.engine.startLoop();

      if (this.dom.btnEnableCamera) {
        this.dom.btnEnableCamera.style.display = 'none';
      }
    } catch (err) {
      console.error("Error al iniciar la cámara:", err);
      this.dom.loaderMsg.textContent = "Acceso a cámara requerido para la biometría.";
      
      if (this.dom.btnEnableCamera) {
        this.dom.btnEnableCamera.style.display = 'inline-flex';
        this.dom.btnEnableCamera.onclick = async () => {
          this.dom.loaderMsg.textContent = "Pidiendo permisos de cámara...";
          await this.tryStartCamera();
        };
      }
    }
  }

  /**
   * Registro de Service Worker para PWA (Instalable en iPhone y Android)
   */
  registerServiceWorker() {
    if ('serviceWorker' in navigator) {
      window.addEventListener('load', () => {
        navigator.serviceWorker.register('sw.js')
          .then(reg => console.log("✅ PWA Service Worker registrado:", reg.scope))
          .catch(err => console.log("PWA SW no registrado:", err));
      });
    }
  }

  /**
   * Configuración de Web Bluetooth
   */
  setupBluetooth() {
    this.bt.onStateChange = (connected, deviceName) => {
      if (connected) {
        this.dom.btnBluetooth.classList.add('connected');
        this.dom.btStatusLabel.textContent = deviceName ? deviceName.slice(0, 10) : "BLE Activo";
        this.dom.btnManualUnlock.disabled = false;
        this.showToast(`Conectado a ${deviceName}`);
        this.playBeep(880, 0.1);
      } else {
        this.dom.btnBluetooth.classList.remove('connected');
        this.dom.btStatusLabel.textContent = "Conectar BLE";
        this.showToast("Bluetooth desconectado");
      }
    };

    this.bt.onMessageReceived = (msg) => {
      console.log("Mensaje de ESP32:", msg);
      this.showToast(`ESP32: ${msg}`);
    };
  }

  /**
   * Procesamiento de rostros detectados en tiempo real
   */
  handleFaceDetection({ hasFace, match }) {
    const ahora = Date.now();
    const reticle = this.dom.scanReticle;

    // Si estamos en período de enfriamiento (cooldown tras apertura)
    if (ahora < this.cooldownUntil) {
      const restantes = Math.ceil((this.cooldownUntil - ahora) / 1000);
      this.dom.cooldownTimer.textContent = `ESPERA: ${restantes}s`;
      this.dom.statusText.textContent = "Acceso concedido recientemente";
      this.dom.statusPulse.className = "pulse-ring active";
      return;
    } else {
      this.dom.cooldownTimer.textContent = "";
    }

    if (!hasFace) {
      reticle.className = "scan-reticle";
      this.dom.statusText.textContent = "Esperando rostro...";
      this.dom.statusPulse.className = "pulse-ring";
      return;
    }

    if (match) {
      // 🟢 Rostro Autorizado Detectado
      reticle.className = "scan-reticle authorized";
      this.dom.statusText.textContent = `AUTORIZADO: ${match.name.toUpperCase()} (${match.confidence}%)`;
      this.dom.statusPulse.className = "pulse-ring active";

      // Disparar Apertura Inalámbrica
      this.triggerUnlock(match.name);

    } else {
      // 🔴 Rostro No Reconocido
      reticle.className = "scan-reticle unauthorized";
      this.dom.statusText.textContent = "Rostro no registrado";
      this.dom.statusPulse.className = "pulse-ring denied";
    }
  }

  /**
   * Ejecuta la orden de apertura a través de Bluetooth o Simulación
   */
  async triggerUnlock(userName) {
    this.cooldownUntil = Date.now() + COOLDOWN_DURATION;
    this.playUnlockChime();
    this.showToast(`🔓 Acceso concedido a ${userName}`);

    if (this.bt.isConnected) {
      await this.bt.openLock(userName);
    } else if (window._firebaseAbrirRemoto) {
      await window._firebaseAbrirRemoto("FACIAL: " + userName);
    } else {
      console.log(`[LOCAL] Apertura concedida para: ${userName} (Sin Bluetooth conectado aún)`);
    }
  }

  /**
   * Enlaza botones y modales
   */
  bindUIEvents() {
    // Botón de Descargar / Instalar App (PWA)
    let deferredPrompt = null;
    window.addEventListener('beforeinstallprompt', (e) => {
      e.preventDefault();
      deferredPrompt = e;
      if (this.dom.btnInstallApp) {
        this.dom.btnInstallApp.style.display = 'inline-flex';
      }
    });

    if (this.dom.btnInstallApp) {
      this.dom.btnInstallApp.addEventListener('click', async () => {
        if (deferredPrompt) {
          deferredPrompt.prompt();
          const { outcome } = await deferredPrompt.userChoice;
          console.log(`Instalación PWA: ${outcome}`);
          deferredPrompt = null;
        } else {
          const isIOS = /iPad|iPhone|iPod/.test(navigator.userAgent) && !window.MSStream;
          if (isIOS) {
            alert("📲 Para instalar en iPhone / iPad:\n\n1. Toca el botón Compartir de Safari (icono 📤 en la barra inferior).\n2. Selecciona 'Añadir a la pantalla de inicio'.");
          } else {
            alert("📲 Para instalar en Android o PC:\n\n1. Toca los tres puntos (⋮) arriba a la derecha en Chrome.\n2. Presiona 'Instalar aplicación' o 'Añadir a pantalla de inicio'.");
          }
        }
      });
    }

    // Botón Bluetooth
    this.dom.btnBluetooth.addEventListener('click', async () => {
      this.initAudio();
      if (!this.bt.isConnected) {
        await this.bt.connect();
      } else {
        this.bt.disconnect();
      }
    });

    // Botón Manual Unlock
    this.dom.btnManualUnlock.addEventListener('click', () => {
      this.initAudio();
      this.triggerUnlock("Manual");
    });

    // Botón Enrolar Rostro
    this.dom.btnRegisterFace.addEventListener('click', () => {
      this.initAudio();
      this.dom.inputUserName.value = '';
      this.dom.modalPromptName.classList.add('open');
      setTimeout(() => this.dom.inputUserName.focus(), 150);
    });

    // Confirmar Enrolamiento
    this.dom.btnConfirmEnroll.addEventListener('click', async () => {
      const name = this.dom.inputUserName.value.trim();
      if (!name) return;

      try {
        this.dom.btnConfirmEnroll.disabled = true;
        this.dom.btnConfirmEnroll.textContent = "Analizando...";

        const nuevo = await this.engine.enrollCurrentFace(name);
        this.dom.modalPromptName.classList.remove('open');
        this.updateUserBadge();
        this.showToast(`Rostro de ${nuevo.name} registrado`);
        this.playBeep(600, 0.15);

      } catch (err) {
        alert(err.message || "Error al enrolar");
      } finally {
        this.dom.btnConfirmEnroll.disabled = false;
        this.dom.btnConfirmEnroll.textContent = "Guardar Rostro";
      }
    });

    this.dom.btnCancelEnroll.addEventListener('click', () => {
      this.dom.modalPromptName.classList.remove('open');
    });

    // Modal de Usuarios
    this.dom.btnOpenUsers.addEventListener('click', () => {
      this.renderUsersList();
      this.dom.modalUsers.classList.add('open');
    });

    this.dom.btnCloseModal.addEventListener('click', () => {
      this.dom.modalUsers.classList.remove('open');
    });

    this.dom.btnClearAllUsers.addEventListener('click', () => {
      if (confirm("¿Seguro que deseas eliminar todos los rostros autorizados?")) {
        this.engine.clearAllUsers();
        this.renderUsersList();
        this.updateUserBadge();
        this.showToast("Todos los usuarios borrados");
      }
    });

    // Cerrar modales pulsando fuera
    [this.dom.modalUsers, this.dom.modalPromptName].forEach(m => {
      m.addEventListener('click', (e) => {
        if (e.target === m) m.classList.remove('open');
      });
    });
  }

  renderUsersList() {
    const list = this.dom.userList;
    list.innerHTML = '';

    const users = this.engine.knownFaces;
    if (users.length === 0) {
      list.innerHTML = '<li class="empty-state">No hay rostros registrados aún. Pulsa "Enrolar Rostro Actual".</li>';
      return;
    }

    users.forEach(u => {
      const li = document.createElement('li');
      li.className = 'user-item';
      li.innerHTML = `
        <div class="user-info">
          <img src="${u.thumbnail}" class="user-avatar" alt="${u.name}">
          <div>
            <div class="user-name">${u.name}</div>
            <div class="user-date">Registrado: ${u.date}</div>
          </div>
        </div>
        <button class="btn-delete-user" data-id="${u.id}" title="Eliminar">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <polyline points="3 6 5 6 21 6"></polyline>
            <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
          </svg>
        </button>
      `;

      li.querySelector('.btn-delete-user').addEventListener('click', () => {
        this.engine.removeUser(u.id);
        this.renderUsersList();
        this.updateUserBadge();
      });

      list.appendChild(li);
    });
  }

  updateUserBadge() {
    this.dom.badgeUserCount.textContent = this.engine.knownFaces.length;
  }

  showToast(msg) {
    this.dom.toastMsg.textContent = msg;
    this.dom.toast.classList.add('show');
    clearTimeout(this._toastTimeout);
    this._toastTimeout = setTimeout(() => {
      this.dom.toast.classList.remove('show');
    }, 2800);
  }

  // ── SONIDOS SINTETIZADOS WEBAUDIO ──
  initAudio() {
    if (!this.audioContext) {
      this.audioContext = new (window.AudioContext || window.webkitAudioContext)();
    }
  }

  playBeep(freq, dur) {
    try {
      this.initAudio();
      const osc = this.audioContext.createOscillator();
      const gain = this.audioContext.createGain();
      osc.frequency.value = freq;
      gain.gain.setValueAtTime(0.05, this.audioContext.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.001, this.audioContext.currentTime + dur);
      osc.connect(gain);
      gain.connect(this.audioContext.destination);
      osc.start();
      osc.stop(this.audioContext.currentTime + dur);
    } catch(e) {}
  }

  playUnlockChime() {
    this.playBeep(523.25, 0.1);
    setTimeout(() => this.playBeep(659.25, 0.1), 80);
    setTimeout(() => this.playBeep(783.99, 0.2), 160);
  }

  startFPSMeter() {
    let lastTime = performance.now();
    let frameCount = 0;

    const tick = () => {
      frameCount++;
      const now = performance.now();
      if (now - lastTime >= 1000) {
        this.dom.fpsCounter.textContent = `${frameCount} FPS`;
        frameCount = 0;
        lastTime = now;
      }
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }
}

// Inicializar la aplicación cuando cargue la página
window.addEventListener('DOMContentLoaded', () => {
  const app = new NexusApp();
  app.init();
});
