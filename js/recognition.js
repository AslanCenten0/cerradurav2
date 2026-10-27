/**
 * ═════════════════════════════════════════════════════════════════════
 *  recognition.js — Motor Biométrico Facial en Cliente (JavaScript)
 *  Basado en Redes Neuronales Convolucionales (TinyFace + Landmarks + ResNet)
 * ═════════════════════════════════════════════════════════════════════
 */

// Modelos servidos desde CDN de alta disponibilidad con compresión brotli/gzip
const MODEL_URL = 'https://cdn.jsdelivr.net/npm/@vladmandic/face-api@1.7.12/model/';
const STORAGE_KEY = 'nexus_authorized_faces_v1';
const MATCH_THRESHOLD = 0.52; // Distancia euclidiana máxima para match (0.52 es riguroso y seguro)

export class FaceRecognitionEngine {
  constructor(videoElement, canvasElement) {
    this.video = videoElement;
    this.canvas = canvasElement;
    this.ctx = canvasElement.getContext('2d');

    this.isModelLoaded = false;
    this.isRunning = false;
    this.knownFaces = []; // Array de { id, name, descriptor: Float32Array, thumbnail, date }

    this.onFaceDetected = null; // Callback emitido al detectar y clasificar
    this.onStatusMessage = null; // Mensajes informativos de carga

    this.loadEnrolledFaces();
  }

  /**
   * Carga las redes neuronales en WebGL a través de TensorFlow.js
   */
  async loadModels() {
    try {
      if (this.onStatusMessage) this.onStatusMessage("Inicializando red neuronal...");

      // 1. Detector ligero para móviles (TinyFaceDetector)
      await faceapi.nets.tinyFaceDetector.loadFromUri(MODEL_URL);
      if (this.onStatusMessage) this.onStatusMessage("Cargando puntos biométricos...");

      // 2. Modelo de 68 puntos faciales de referencia
      await faceapi.nets.faceLandmark68Net.loadFromUri(MODEL_URL);
      if (this.onStatusMessage) this.onStatusMessage("Cargando descriptor de 128 dimensiones...");

      // 3. Extractor de embeddings faciales ResNet de 128 dimensiones
      await faceapi.nets.faceRecognitionNet.loadFromUri(MODEL_URL);

      this.isModelLoaded = true;
      if (this.onStatusMessage) this.onStatusMessage("Modelos listos.");
      console.log("✅ Redes neuronales faciales cargadas correctamente en WebGL.");
      return true;
    } catch (err) {
      console.error("Error al cargar modelos de face-api:", err);
      if (this.onStatusMessage) this.onStatusMessage("Error al cargar redes neuronales.");
      return false;
    }
  }

  /**
   * Inicia la cámara web del dispositivo
   */
  async startCamera() {
    try {
      if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        throw new Error("Este dispositivo no soporta acceso a la cámara o requiere HTTPS.");
      }

      let stream = null;
      // 1. Intentar con cámara frontal optimizada
      try {
        stream = await navigator.mediaDevices.getUserMedia({
          audio: false,
          video: {
            facingMode: 'user',
            width: { ideal: 640 },
            height: { ideal: 480 }
          }
        });
      } catch (err1) {
        console.warn("Fallo con opciones ideales de cámara, reintentando con fallback:", err1);
        // 2. Fallback a cualquier cámara disponible
        stream = await navigator.mediaDevices.getUserMedia({
          audio: false,
          video: true
        });
      }

      this.video.srcObject = stream;
      this.video.setAttribute('playsinline', '');
      this.video.setAttribute('muted', '');
      this.video.muted = true;

      return new Promise((resolve) => {
        let resolved = false;
        const doResolve = () => {
          if (resolved) return;
          resolved = true;
          this.adjustCanvasDimensions();
          window.addEventListener('resize', () => this.adjustCanvasDimensions());
          this.video.play().catch(e => console.warn("Video play:", e));
          resolve(true);
        };

        if (this.video.readyState >= 2) {
          doResolve();
        } else {
          this.video.onloadeddata = doResolve;
          this.video.onloadedmetadata = doResolve;
        }

        // Timeout de seguridad de 2.5s para no bloquear la aplicación
        setTimeout(doResolve, 2500);
      });
    } catch (err) {
      console.error("Error accediendo a la cámara:", err);
      throw err;
    }
  }

  /**
   * Sincroniza el tamaño del canvas con la visualización del video
   */
  adjustCanvasDimensions() {
    if (!this.video.videoWidth) return;
    this.canvas.width = this.video.videoWidth;
    this.canvas.height = this.video.videoHeight;
  }

  /**
   * Bucle continuo de reconocimiento facial
   */
  startLoop() {
    if (this.isRunning) return;
    this.isRunning = true;

    const detectLoop = async () => {
      if (!this.isRunning) return;

      if (this.video.readyState === 4 && this.isModelLoaded) {
        await this.detectAndRecognize();
      }

      requestAnimationFrame(detectLoop);
    };

    requestAnimationFrame(detectLoop);
  }

  stopLoop() {
    this.isRunning = false;
  }

  /**
   * Detecta rostros en el fotograma actual y los compara con la base local
   */
  async detectAndRecognize() {
    // Configuración optimizada de TinyFaceDetector
    const options = new faceapi.TinyFaceDetectorOptions({ inputSize: 224, scoreThreshold: 0.5 });
    
    // Detectar rostro, alinear landmarks y calcular descriptor de 128 floats
    const detection = await faceapi
      .detectSingleFace(this.video, options)
      .withFaceLandmarks()
      .withFaceDescriptor();

    // Limpiar canvas de overlay
    this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

    if (!detection) {
      if (this.onFaceDetected) {
        this.onFaceDetected({ hasFace: false });
      }
      return;
    }

    const { box } = detection.detection;
    let match = null;

    // Comparar con los descriptores almacenados
    if (this.knownFaces.length > 0) {
      let menorDistancia = Infinity;
      let mejorUsuario = null;

      for (const usuario of this.knownFaces) {
        const distancia = faceapi.euclideanDistance(detection.descriptor, usuario.descriptor);
        if (distancia < menorDistancia) {
          menorDistancia = distancia;
          mejorUsuario = usuario;
        }
      }

      if (menorDistancia <= MATCH_THRESHOLD) {
        const score = Math.max(0, Math.min(100, Math.round((1 - (menorDistancia / MATCH_THRESHOLD)) * 100)));
        match = {
          name: mejorUsuario.name,
          confidence: score,
          distance: menorDistancia.toFixed(3)
        };
      }
    }

    // Dibujar recuadro minimalista en el canvas
    this.drawMinimalHUD(box, match);

    if (this.onFaceDetected) {
      this.onFaceDetected({
        hasFace: true,
        match: match,
        box: box
      });
    }
  }

  /**
   * Dibuja los indicadores de detección de forma minimalista y elegante
   */
  drawMinimalHUD(box, match) {
    const { x, y, width, height } = box;
    const isAuth = !!match;
    const color = isAuth ? '#10b981' : '#f43f5e';

    this.ctx.save();
    this.ctx.lineWidth = 2;
    this.ctx.strokeStyle = color;

    // Solo esquinas tipo retícula táctica
    const cornerSize = Math.min(width, height) * 0.2;
    
    // Esquina superior izquierda
    this.ctx.beginPath();
    this.ctx.moveTo(x, y + cornerSize);
    this.ctx.lineTo(x, y);
    this.ctx.lineTo(x + cornerSize, y);
    this.ctx.stroke();

    // Esquina superior derecha
    this.ctx.beginPath();
    this.ctx.moveTo(x + width - cornerSize, y);
    this.ctx.lineTo(x + width, y);
    this.ctx.lineTo(x + width, y + cornerSize);
    this.ctx.stroke();

    // Esquina inferior izquierda
    this.ctx.beginPath();
    this.ctx.moveTo(x, y + height - cornerSize);
    this.ctx.lineTo(x, y + height);
    this.ctx.lineTo(x + cornerSize, y + height);
    this.ctx.stroke();

    // Esquina inferior derecha
    this.ctx.beginPath();
    this.ctx.moveTo(x + width - cornerSize, y + height);
    this.ctx.lineTo(x + width, y + height);
    this.ctx.lineTo(x + width, y + height - cornerSize);
    this.ctx.stroke();

    // Etiqueta con tipografía limpia
    const label = isAuth ? `${match.name.toUpperCase()} (${match.confidence}%)` : 'DESCONOCIDO';
    this.ctx.font = '600 12px "JetBrains Mono", monospace';
    this.ctx.fillStyle = color;
    this.ctx.fillText(label, x + 4, y - 8);

    this.ctx.restore();
  }

  /**
   * Enrola el rostro actualmente enfocado por la cámara
   */
  async enrollCurrentFace(userName) {
    if (!userName || !userName.trim()) {
      throw new Error("El nombre no puede estar vacío");
    }

    const options = new faceapi.TinyFaceDetectorOptions({ inputSize: 320, scoreThreshold: 0.6 });
    const detection = await faceapi
      .detectSingleFace(this.video, options)
      .withFaceLandmarks()
      .withFaceDescriptor();

    if (!detection) {
      throw new Error("No se detectó ningún rostro con claridad. Mira de frente a la cámara.");
    }

    // Capturar miniatura (thumbnail) de la cara
    const thumbnail = this.captureThumbnail(detection.detection.box);

    const nuevoUsuario = {
      id: 'usr_' + Date.now(),
      name: userName.trim(),
      descriptor: Array.from(detection.descriptor), // Convertir Float32Array a Array estándar para JSON
      thumbnail: thumbnail,
      date: new Date().toLocaleDateString('es-AR')
    };

    // Agregar a la lista y persistir en LocalStorage
    this.knownFaces.push({
      ...nuevoUsuario,
      descriptor: new Float32Array(nuevoUsuario.descriptor)
    });

    this.saveEnrolledFaces();
    return nuevoUsuario;
  }

  /**
   * Extrae un recorte cuadrado de la cara para usar como avatar en la lista
   */
  captureThumbnail(box) {
    const thumbCanvas = document.createElement('canvas');
    thumbCanvas.width = 64;
    thumbCanvas.height = 64;
    const thumbCtx = thumbCanvas.getContext('2d');

    const margin = box.width * 0.2;
    const sx = Math.max(0, box.x - margin);
    const sy = Math.max(0, box.y - margin);
    const sWidth = Math.min(this.video.videoWidth - sx, box.width + margin * 2);
    const sHeight = Math.min(this.video.videoHeight - sy, box.height + margin * 2);

    thumbCtx.drawImage(this.video, sx, sy, sWidth, sHeight, 0, 0, 64, 64);
    return thumbCanvas.toDataURL('image/jpeg', 0.85);
  }

  /**
   * Persistencia en LocalStorage
   */
  saveEnrolledFaces() {
    const serializable = this.knownFaces.map(u => ({
      id: u.id,
      name: u.name,
      descriptor: Array.from(u.descriptor),
      thumbnail: u.thumbnail,
      date: u.date
    }));
    localStorage.setItem(STORAGE_KEY, JSON.stringify(serializable));
  }

  loadEnrolledFaces() {
    try {
      const data = localStorage.getItem(STORAGE_KEY);
      if (data) {
        const parsed = JSON.parse(data);
        this.knownFaces = parsed.map(u => ({
          ...u,
          descriptor: new Float32Array(u.descriptor)
        }));
      }
    } catch (e) {
      console.warn("Error cargando rostros desde localStorage:", e);
      this.knownFaces = [];
    }
  }

  removeUser(userId) {
    this.knownFaces = this.knownFaces.filter(u => u.id !== userId);
    this.saveEnrolledFaces();
  }

  clearAllUsers() {
    this.knownFaces = [];
    localStorage.removeItem(STORAGE_KEY);
  }
}
