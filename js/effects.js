/**
 * ═════════════════════════════════════════════════════════════════════
 *  effects.js — Apple Luminous Ambient Light Canvas & Fluid Micro-Interactions
 *  • Pure White & Soft Gray Palette
 *  • Gentle Organic Light blooms that refract behind the Liquid Glass
 *  • Zero Jitter, Zero Latency
 * ═════════════════════════════════════════════════════════════════════
 */

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

  // Orbes de luz suave difusa (Gris y Blanco sobre fondo Apple)
  const orbs = [
    { x: width * 0.2, y: height * 0.25, radius: 380, color: 'rgba(255, 255, 255, 0.8)', vx: 0.08, vy: 0.06 },
    { x: width * 0.8, y: height * 0.7, radius: 420, color: 'rgba(235, 235, 240, 0.65)', vx: -0.07, vy: 0.09 },
    { x: width * 0.5, y: height * 0.5, radius: 320, color: 'rgba(255, 255, 255, 0.75)', vx: 0.05, vy: -0.07 }
  ];

  let lastTime = performance.now();

  function draw(currentTime) {
    if (document.hidden) {
      requestAnimationFrame(draw);
      return;
    }

    const dt = Math.min((currentTime - lastTime) * 0.001, 0.1);
    lastTime = currentTime;

    ctx.clearRect(0, 0, width, height);

    // Fondo gris claro Apple
    ctx.fillStyle = '#F5F5F7';
    ctx.fillRect(0, 0, width, height);

    // Ondulaciones orgánicas de luz difusa
    orbs.forEach((orb) => {
      orb.x += orb.vx * 60 * dt;
      orb.y += orb.vy * 60 * dt;

      if (orb.x < -80 || orb.x > width + 80) orb.vx *= -1;
      if (orb.y < -80 || orb.y > height + 80) orb.vy *= -1;

      const grad = ctx.createRadialGradient(orb.x, orb.y, 0, orb.x, orb.y, orb.radius);
      grad.addColorStop(0, orb.color);
      grad.addColorStop(1, 'rgba(245, 245, 247, 0)');

      ctx.fillStyle = grad;
      ctx.beginPath();
      ctx.arc(orb.x, orb.y, orb.radius, 0, Math.PI * 2);
      ctx.fill();
    });

    requestAnimationFrame(draw);
  }

  requestAnimationFrame(draw);
}

export function initSpotlightCards() {
  // En la paleta clara Apple, la luz refracta de forma limpia y transparente
  const cards = document.querySelectorAll('.opt-card, .status-hero, .status-shelf, .modal-card, .nav-bar');
  cards.forEach((card) => {
    card.addEventListener('pointermove', (e) => {
      const rect = card.getBoundingClientRect();
      card.style.setProperty('--mouse-x', `${e.clientX - rect.left}px`);
      card.style.setProperty('--mouse-y', `${e.clientY - rect.top}px`);
    });
  });
}

export function decryptText(element, targetText) {
  if (!element) return;
  element.textContent = targetText || element.textContent || '';
}

export function initClickSparks() {
  // Microinteracción física Apple en pointerdown
  const btns = document.querySelectorAll('button, .btn-pill, .btn-primary, .btn-secondary, .tab-btn');
  btns.forEach((btn) => {
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
