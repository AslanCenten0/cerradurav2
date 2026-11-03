/**
 * ═════════════════════════════════════════════════════════════════════
 *  effects.js — Apple Liquid Glass Ambient Motion & Physical Springs
 *  • Dynamic Ambient Mesh Orbs (VisionOS / macOS Sequoia liquid backdrop)
 *  • Specular Surface Reflections (Light refraction over liquid glass)
 *  • Instant Physical Response (Apple WWDC 2018 Fluid Interfaces)
 * ═════════════════════════════════════════════════════════════════════
 */

// ── 1. LIQUID AMBIENT MESH (APPLE LIQUID BACKDROP) ──
export function initBackgroundGrid(canvasId = 'bgCanvas') {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;
  const ctx = canvas.getContext('2d');

  let width = 0;
  let height = 0;
  let dpr = 1;

  function resize() {
    dpr = Math.min(window.devicePixelRatio || 1, 2);
    width = window.innerWidth;
    height = window.innerHeight;
    canvas.width = width * dpr;
    canvas.height = height * dpr;
    ctx.scale(dpr, dpr);
  }

  resize();
  window.addEventListener('resize', resize);

  // Puntero interactivo suave con inercia
  const pointer = { x: width * 0.5, y: height * 0.35, tx: width * 0.5, ty: height * 0.35 };
  window.addEventListener('pointermove', (e) => {
    pointer.tx = e.clientX;
    pointer.ty = e.clientY;
  });

  // Orbes de color orgánico líquido Apple (Azul iOS, Esmeralda, Índigo profundo, Púrpura suave)
  const orbs = [
    { x: width * 0.25, y: height * 0.25, baseRadius: 320, color: 'rgba(10, 132, 255, 0.16)', vx: 0.15, vy: 0.12, phase: 0 },
    { x: width * 0.75, y: height * 0.70, baseRadius: 360, color: 'rgba(48, 209, 88, 0.13)', vx: -0.12, vy: 0.16, phase: 2 },
    { x: width * 0.55, y: height * 0.40, baseRadius: 280, color: 'rgba(94, 92, 230, 0.12)', vx: 0.10, vy: -0.14, phase: 4 },
    { x: width * 0.30, y: height * 0.85, baseRadius: 340, color: 'rgba(191, 90, 242, 0.09)', vx: -0.08, vy: -0.10, phase: 1 }
  ];

  let lastTime = performance.now();

  function draw(currentTime) {
    if (document.hidden) {
      requestAnimationFrame(draw);
      return;
    }

    const dt = Math.min((currentTime - lastTime) * 0.001, 0.1);
    lastTime = currentTime;

    // Física de resorte crítico para el puntero (damping 1.0, sin oscilación brusca)
    pointer.x += (pointer.tx - pointer.x) * (1 - Math.exp(-6 * dt));
    pointer.y += (pointer.ty - pointer.y) * (1 - Math.exp(-6 * dt));

    ctx.clearRect(0, 0, width, height);

    // Fondo profundo tipo Apple Dark Mode
    ctx.fillStyle = '#050608';
    ctx.fillRect(0, 0, width, height);

    // Renderizado de orbes etéreos que refractan a través del Liquid Glass
    orbs.forEach((orb, i) => {
      orb.phase += dt * 0.4;
      orb.x += orb.vx * 60 * dt;
      orb.y += orb.vy * 60 * dt;

      // Rebote suave en bordes
      if (orb.x < -100) { orb.x = -100; orb.vx *= -1; }
      if (orb.x > width + 100) { orb.x = width + 100; orb.vx *= -1; }
      if (orb.y < -100) { orb.y = -100; orb.vy *= -1; }
      if (orb.y > height + 100) { orb.y = height + 100; orb.vy *= -1; }

      // Gravitación sutil al puntero
      const dx = pointer.x - orb.x;
      const dy = pointer.y - orb.y;
      orb.x += dx * 0.0015;
      orb.y += dy * 0.0015;

      const currentRadius = orb.baseRadius + Math.sin(orb.phase) * 35;
      const grad = ctx.createRadialGradient(orb.x, orb.y, 0, orb.x, orb.y, currentRadius);
      grad.addColorStop(0, orb.color);
      grad.addColorStop(0.6, orb.color.replace(/[\d\.]+\)$/, '0.04)'));
      grad.addColorStop(1, 'transparent');

      ctx.fillStyle = grad;
      ctx.beginPath();
      ctx.arc(orb.x, orb.y, currentRadius, 0, Math.PI * 2);
      ctx.fill();
    });

    requestAnimationFrame(draw);
  }

  requestAnimationFrame(draw);
}

// ── 2. SPECULAR LIQUID GLASS REFLECTION ──
export function initSpotlightCards() {
  const cards = document.querySelectorAll('.opt-card, .status-hero, .status-shelf, .modal-card, .nav-bar, .tab-navigator');

  cards.forEach((card) => {
    card.addEventListener('pointermove', (e) => {
      const rect = card.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;
      card.style.setProperty('--mouse-x', `${x}px`);
      card.style.setProperty('--mouse-y', `${y}px`);
    });
  });
}

// ── 3. TIPOGRAFÍA DINÁMICA & DESENCRIPTADO REFINADO ──
export function decryptText(element, targetText, duration = 400, speed = 30) {
  if (!element) return;
  const original = targetText || element.textContent || '';
  element.textContent = original;
}

// ── 4. RESISTENCIA FÍSICA & MICROINTERACCIONES DE PULSACIÓN ──
export function initClickSparks() {
  // Apple Design prefiere microinteracciones táctiles nativas (resorte scale instantáneo)
  // en lugar de confeti de partículas que distrae del flujo limpio.
  const interactive = document.querySelectorAll('button, .btn-pill, .btn-primary, .btn-secondary, .tab-btn');
  interactive.forEach((btn) => {
    btn.addEventListener('pointerdown', () => {
      btn.style.transform = 'scale(0.965)';
    });
    btn.addEventListener('pointerup', () => {
      btn.style.transform = '';
    });
    btn.addEventListener('pointercancel', () => {
      btn.style.transform = '';
    });
  });
}
