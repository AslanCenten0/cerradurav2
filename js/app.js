/**
 * ═════════════════════════════════════════════════════════════════════
 *  app.js — Orquestador Principal de la Aplicación
 *  Enlaza la interfaz minimalista Apple y el motor biométrico con Firebase
 * ═════════════════════════════════════════════════════════════════════
 */

import { FaceRecognitionEngine } from './recognition.js';
import { initBackgroundGrid, initSpotlightCards, initClickSparks, decryptText } from './effects.js';

// Exponer decryptText para que el panel de control pueda usarlo
window.decryptText = decryptText;

// Cooldown de 6 segundos tras apertura exitosa
const COOLDOWN_DURATION = 6000;

export class NexusApp {
  constructor() {
    this.engine = null;
    this.deferredPrompt = null;

    this.cooldownUntil = 0;
    this.audioContext = null;

    // Referencias al DOM
    this.dom = {
      video: document.getElementById('webcam'),
      canvas: document.getElementById('overlayCanvas'),
      cameraWrapper: document.getElementById('cameraWrapper'),
      loader: document.getElementById('loaderSplash'),
      loaderMsg: document.getElementById('loaderMessage'),
      toast: document.getElementById('feedbackToast'),
      toastMsg: document.getElementById('toastMessage'),
      statusPulse: document.getElementById('statusPulse'),
      statusText: document.getElementById('statusText'),
      cooldownTimer: document.getElementById('cooldownTimer'),
      fpsCounter: document.getElementById('fpsCounter'),
      scanReticle: document.getElementById('scanReticle'),
      
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
      inputAdminPassword: document.getElementById('inputAdminPassword'),
      btnTogglePassword: document.getElementById('btnTogglePassword'),
      enrollErrorMsg: document.getElementById('enrollErrorMsg'),

      // PWA / Descargar
      btnInstallApp: document.getElementById('btnInstallApp'),
      labelInstallApp: document.getElementById('labelInstallApp'),
      modalInstall: document.getElementById('modalInstall'),
      btnCloseInstallModal: document.getElementById('btnCloseInstallModal'),
      btnCloseInstallSheet: document.getElementById('btnCloseInstallSheet'),
      installNativeSection: document.getElementById('installNativeSection'),
      btnTriggerNativeInstall: document.getElementById('btnTriggerNativeInstall'),

      btnEnableCamera: document.getElementById('btnEnableCamera'),
    };
  }

  async init() {
    console.log("🚀 NexusApp.init() iniciando...");

    // 0. Inicializar Efectos Visuales
    try {
      initBackgroundGrid('bgCanvas');
      initSpotlightCards();
      initClickSparks();
    } catch (e) {
      console.warn("Efectos visuales no críticos:", e);
    }

    const brandName = document.querySelector('.brand-name');
    if (brandName) {
      decryptText(brandName, "NEXUS ACCESS", 900, 35);
    }

    // 1. Conectar Eventos de la Interfaz
    this.bindUIEvents();

    // 2. Registrar Service Worker para PWA
    this.registerServiceWorker();

    // 3. Inicializar Motor Biométrico
    this.engine = new FaceRecognitionEngine(this.dom.video, this.dom.canvas);
    this.updateUserBadge();

    // 4. Iniciar Cámara de forma prioritaria e INMEDIATA (sin esperar modelos pesados)
    const cameraPromise = this.tryStartCamera();

    // 5. Cargar Redes Neuronales en paralelo en segundo plano
    const modelsPromise = (async () => {
      try {
        if (this.dom.statusText) this.dom.statusText.textContent = "Cargando IA facial...";
        await this.engine.loadModels();
        if (this.dom.statusText) this.dom.statusText.textContent = "Esperando rostro...";
        console.log("✅ Modelos neuronales listos para reconocimiento.");
      } catch (errModels) {
        console.warn("Advertencia cargando modelos neuronales:", errModels);
        if (this.dom.statusText) this.dom.statusText.textContent = "Error al cargar IA facial";
      }
    })();

    // 6. Medidor de FPS
    this.startFPSMeter();

    await Promise.allSettled([cameraPromise, modelsPromise]);
    console.log("✅ NexusApp.init() completamente activo.");
  }

  async tryStartCamera() {
    try {
      if (this.dom.loaderMsg) this.dom.loaderMsg.textContent = "Conectando cámara...";
      console.log("📷 Iniciando tryStartCamera...");
      
      await this.engine.startCamera();

      // Ocultar pantalla de carga de inmediato
      if (this.dom.loader) {
        this.dom.loader.classList.add('hidden');
        setTimeout(() => {
          if (this.dom.loader) this.dom.loader.style.display = 'none';
        }, 300);
      }
      this.showToast("Cámara activa");

      // Iniciar bucle de detección facial
      this.engine.onFaceDetected = (result) => this.handleFaceDetection(result);
      this.engine.startLoop();

      if (this.dom.btnEnableCamera) {
        this.dom.btnEnableCamera.style.display = 'none';
      }
      console.log("✅ Cámara y stream activos.");
    } catch (err) {
      console.error("❌ Error al iniciar la cámara:", err);
      if (this.dom.loaderMsg) {
        this.dom.loaderMsg.textContent = "Permiso de cámara requerido.";
      }
      
      if (this.dom.btnEnableCamera) {
        this.dom.btnEnableCamera.style.display = 'inline-flex';
        this.dom.btnEnableCamera.onclick = async (e) => {
          e.stopPropagation();
          if (this.dom.loaderMsg) this.dom.loaderMsg.textContent = "Solicitando permisos...";
          this.dom.btnEnableCamera.style.display = 'none';
          await this.tryStartCamera();
        };
      }
    }
  }

  /**
   * Registro de Service Worker para PWA
   */
  registerServiceWorker() {
    if ('serviceWorker' in navigator) {
      navigator.serviceWorker.register('sw.js?v=9')
        .then(reg => {
          console.log("✅ PWA Service Worker registrado con éxito:", reg.scope);
          // Forzar chequeo de actualización inmediata
          reg.update().catch(() => {});
        })
        .catch(err => console.log("PWA SW no registrado:", err));
    }
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
      if (this.dom.cooldownTimer) this.dom.cooldownTimer.textContent = `ESPERA: ${restantes}s`;
      if (this.dom.statusText) this.dom.statusText.textContent = "Acceso concedido recientemente";
      if (this.dom.statusPulse) this.dom.statusPulse.className = "pulse-ring active";
      return;
    } else {
      if (this.dom.cooldownTimer) this.dom.cooldownTimer.textContent = "";
    }

    if (!hasFace) {
      if (reticle) reticle.className = "scan-reticle";
      if (this.dom.statusText) this.dom.statusText.textContent = "Esperando rostro...";
      if (this.dom.statusPulse) this.dom.statusPulse.className = "pulse-ring";
      return;
    }

    if (match) {
      // 🟢 Rostro Autorizado Detectado
      if (reticle) reticle.className = "scan-reticle authorized";
      if (this.dom.statusText) this.dom.statusText.textContent = `AUTORIZADO: ${match.name.toUpperCase()} (${match.confidence}%)`;
      if (this.dom.statusPulse) this.dom.statusPulse.className = "pulse-ring active";

      // Disparar Apertura
      this.triggerUnlock(match.name);

    } else {
      // 🔴 Rostro No Reconocido
      if (reticle) reticle.className = "scan-reticle unauthorized";
      if (this.dom.statusText) this.dom.statusText.textContent = "Rostro no registrado";
      if (this.dom.statusPulse) this.dom.statusPulse.className = "pulse-ring denied";
    }
  }

  /**
   * Ejecuta la orden de apertura a través de Firebase Realtime Database
   */
  async triggerUnlock(userName) {
    this.cooldownUntil = Date.now() + COOLDOWN_DURATION;
    this.playUnlockChime();
    this.showToast(`🔓 Acceso concedido a ${userName}`);

    if (window._firebaseAbrirRemoto) {
      await window._firebaseAbrirRemoto("FACIAL: " + userName);
    } else {
      console.log(`[LOCAL] Apertura concedida para: ${userName}`);
    }
  }

  /**
   * Enlaza botones y modales
   */
  bindUIEvents() {
    // Escuchar el evento de instalación de PWA (Chrome, Edge, Android)
    window.addEventListener('beforeinstallprompt', (e) => {
      e.preventDefault();
      this.deferredPrompt = e;
      console.log("📲 Evento beforeinstallprompt capturado!");
      if (this.dom.installNativeSection) {
        this.dom.installNativeSection.style.display = 'block';
      }
    });

    window.addEventListener('appinstalled', () => {
      console.log("🎉 PWA instalada exitosamente!");
      this.deferredPrompt = null;
      if (this.dom.labelInstallApp) {
        this.dom.labelInstallApp.textContent = "Instalada";
      }
      this.showToast("¡Aplicación instalada!");
    });

    // Tap en cualquier parte del visor de cámara para desbloquear autoplay si el navegador lo pausó
    if (this.dom.cameraWrapper) {
      this.dom.cameraWrapper.addEventListener('click', () => {
        if (this.dom.video && this.dom.video.paused) {
          console.log("Tapping cámara para forzar video.play()...");
          this.dom.video.play().catch(e => console.warn("play err:", e));
        }
      });
    }

    // Botón Descargar / Instalar App
    if (this.dom.btnInstallApp) {
      this.dom.btnInstallApp.addEventListener('click', () => {
        if (this.deferredPrompt) {
          this.deferredPrompt.prompt();
          this.deferredPrompt.userChoice.then(({ outcome }) => {
            console.log(`Resultado de instalación: ${outcome}`);
            if (outcome === 'accepted') {
              this.deferredPrompt = null;
            }
          });
        } else {
          // Si no está disponible el prompt directo o es iOS, abrir modal explicativo con guía
          this.openInstallModal();
        }
      });
    }

    // Botón de instalación nativa dentro del modal
    if (this.dom.btnTriggerNativeInstall) {
      this.dom.btnTriggerNativeInstall.addEventListener('click', () => {
        if (this.deferredPrompt) {
          this.deferredPrompt.prompt();
          this.deferredPrompt.userChoice.then(() => {
            this.closeInstallModal();
          });
        }
      });
    }

    if (this.dom.btnCloseInstallModal) {
      this.dom.btnCloseInstallModal.addEventListener('click', () => this.closeInstallModal());
    }
    if (this.dom.btnCloseInstallSheet) {
      this.dom.btnCloseInstallSheet.addEventListener('click', () => this.closeInstallModal());
    }

    // Botón Manual Unlock (apertura remota por Firebase)
    if (this.dom.btnManualUnlock) {
      this.dom.btnManualUnlock.addEventListener('click', () => {
        this.initAudio();
        this.triggerUnlock("Manual");
      });
    }

    // Helper para mensajes de error al enrolar
    this.showEnrollError = (msg) => {
      if (this.dom.enrollErrorMsg) {
        this.dom.enrollErrorMsg.textContent = msg;
        this.dom.enrollErrorMsg.style.display = 'block';
      }
    };
    this.hideEnrollError = () => {
      if (this.dom.enrollErrorMsg) {
        this.dom.enrollErrorMsg.textContent = '';
        this.dom.enrollErrorMsg.style.display = 'none';
      }
    };

    // Botón Enrolar Rostro
    if (this.dom.btnRegisterFace) {
      this.dom.btnRegisterFace.addEventListener('click', () => {
        this.initAudio();
        this.dom.inputUserName.value = '';
        if (this.dom.inputAdminPassword) {
          this.dom.inputAdminPassword.value = '';
          this.dom.inputAdminPassword.type = 'password';
        }
        this.hideEnrollError();
        this.dom.modalPromptName.classList.add('open');
        setTimeout(() => this.dom.inputUserName.focus(), 150);
      });
    }

    // Toggle para visualizar/ocultar contraseña
    if (this.dom.btnTogglePassword && this.dom.inputAdminPassword) {
      this.dom.btnTogglePassword.addEventListener('click', () => {
        const isPwd = this.dom.inputAdminPassword.type === 'password';
        this.dom.inputAdminPassword.type = isPwd ? 'text' : 'password';
      });
    }

    // Tecla Enter en campos del modal
    if (this.dom.inputUserName) {
      this.dom.inputUserName.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
          e.preventDefault();
          if (this.dom.inputAdminPassword) this.dom.inputAdminPassword.focus();
        }
      });
    }
    if (this.dom.inputAdminPassword) {
      this.dom.inputAdminPassword.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
          e.preventDefault();
          this.dom.btnConfirmEnroll.click();
        }
      });
    }

    // Confirmar Enrolamiento con Contraseña de Admin (201106)
    if (this.dom.btnConfirmEnroll) {
      this.dom.btnConfirmEnroll.addEventListener('click', async () => {
        const name = this.dom.inputUserName.value.trim();
        const password = this.dom.inputAdminPassword ? this.dom.inputAdminPassword.value.trim() : '';

        this.hideEnrollError();

        if (!name) {
          this.showEnrollError("Ingresa un nombre para el rostro.");
          this.dom.inputUserName.focus();
          return;
        }

        if (password !== '201106') {
          this.showEnrollError("Contraseña incorrecta. Acceso denegado.");
          this.playBeep(200, 0.25);
          if (this.dom.inputAdminPassword) {
            this.dom.inputAdminPassword.classList.add('input-error-shake');
            setTimeout(() => this.dom.inputAdminPassword.classList.remove('input-error-shake'), 600);
            this.dom.inputAdminPassword.focus();
            this.dom.inputAdminPassword.select();
          }
          return;
        }

        try {
          this.dom.btnConfirmEnroll.disabled = true;
          this.dom.btnConfirmEnroll.textContent = "Analizando...";

          const nuevo = await this.engine.enrollCurrentFace(name);
          this.dom.modalPromptName.classList.remove('open');
          this.updateUserBadge();
          this.showToast(`Rostro de ${nuevo.name} registrado`);
          this.playBeep(600, 0.15);

        } catch (err) {
          this.showEnrollError(err.message || "No se detectó un rostro claro en cámara");
          this.playBeep(200, 0.2);
        } finally {
          this.dom.btnConfirmEnroll.disabled = false;
          this.dom.btnConfirmEnroll.textContent = "Guardar";
        }
      });
    }

    if (this.dom.btnCancelEnroll) {
      this.dom.btnCancelEnroll.addEventListener('click', () => {
        this.dom.modalPromptName.classList.remove('open');
      });
    }

    // Modal de Usuarios
    if (this.dom.btnOpenUsers) {
      this.dom.btnOpenUsers.addEventListener('click', () => {
        this.renderUsersList();
        this.dom.modalUsers.classList.add('open');
      });
    }

    if (this.dom.btnCloseModal) {
      this.dom.btnCloseModal.addEventListener('click', () => {
        this.dom.modalUsers.classList.remove('open');
      });
    }

    if (this.dom.btnClearAllUsers) {
      this.dom.btnClearAllUsers.addEventListener('click', () => {
        if (confirm("¿Seguro que deseas eliminar todos los rostros autorizados?")) {
          this.engine.clearAllUsers();
          this.renderUsersList();
          this.updateUserBadge();
          this.showToast("Todos los usuarios borrados");
        }
      });
    }

    // Cerrar modales pulsando en el fondo
    [this.dom.modalUsers, this.dom.modalPromptName, this.dom.modalInstall].forEach(m => {
      if (m) {
        m.addEventListener('click', (e) => {
          if (e.target === m) m.classList.remove('open');
        });
      }
    });
  }

  openInstallModal() {
    if (!this.dom.modalInstall) return;
    if (this.deferredPrompt && this.dom.installNativeSection) {
      this.dom.installNativeSection.style.display = 'block';
    }
    this.dom.modalInstall.classList.add('open');
  }

  closeInstallModal() {
    if (this.dom.modalInstall) {
      this.dom.modalInstall.classList.remove('open');
    }
  }

  renderUsersList() {
    const list = this.dom.userList;
    if (!list) return;
    list.innerHTML = '';

    const users = this.engine.knownFaces;
    if (users.length === 0) {
      list.innerHTML = '<li class="empty-state">No hay rostros registrados aún.</li>';
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
    if (this.dom.badgeUserCount && this.engine) {
      this.dom.badgeUserCount.textContent = this.engine.knownFaces.length;
    }
  }

  showToast(msg) {
    if (!this.dom.toastMsg || !this.dom.toast) return;
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
        if (this.dom.fpsCounter) this.dom.fpsCounter.textContent = `${frameCount} FPS`;
        frameCount = 0;
        lastTime = now;
      }
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }
}

// ── BOOT: arranque seguro ──
function boot() {
  if (window.__nexusAppLoaded) return;
  window.__nexusAppLoaded = true;
  console.log("🔧 boot() inicializando NexusApp...");
  const app = new NexusApp();
  app.init();
  window.nexusApp = app;
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', boot);
} else {
  boot();
}
