/**
 * ═════════════════════════════════════════════════════════════════════
 *  effects.js — React Bits Visual Enhancements (Vanilla Port)
 *  • Interactive Aurora & Grid Background (React Bits Squares + Waves)
 *  • Spotlight Cards (Dynamic mouse/touch radial glow)
 *  • Decrypted Text (Cyber matrix letter resolution)
 *  • Click Spark (Tactile micro-particle bursts)
 * ═════════════════════════════════════════════════════════════════════
 */

// ── 1. FONDO ANIMADO INTERACTIVO: AURORA + CUADRÍCULA (SQUARES) ──
export function initBackgroundGrid(canvasId = 'bgCanvas') {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;
  const ctx = canvas.getContext('2d');

  let width = (canvas.width = window.innerWidth);
  let height = (canvas.height = window.innerHeight);

  window.addEventListener('resize', () => {
    width = canvas.width = window.innerWidth;
    height = canvas.height = window.innerHeight;
  });

  // Puntero interactivo para iluminar celdas al pasar el dedo/mouse
  const pointer = { x: -1000, y: -1000, active: false };
  window.addEventListener('pointermove', (e) => {
    pointer.x = e.clientX;
    pointer.y = e.clientY;
    pointer.active = true;
  });
  window.addEventListener('pointerleave', () => {
    pointer.active = false;
  });

  // Orbes luminosos flotantes (Aurora de baja opacidad)
  const orbs = [
    { x: width * 0.2, y: height * 0.3, radius: 260, color: 'rgba(16, 185, 129, 0.08)', vx: 0.25, vy: 0.18 },
    { x: width * 0.8, y: height * 0.7, radius: 320, color: 'rgba(59, 130, 246, 0.07)', vx: -0.2, vy: 0.22 },
    { x: width * 0.5, y: height * 0.5, radius: 220, color: 'rgba(99, 102, 241, 0.05)', vx: 0.15, vy: -0.15 }
  ];

  const gridSize = 42; // Tamaño de cada celda de la cuadrícula

  function draw() {
    if (document.hidden) {
      requestAnimationFrame(draw);
      return;
    }

    ctx.clearRect(0, 0, width, height);

    // Fondo base ultra oscuro mate
    ctx.fillStyle = '#09090b';
    ctx.fillRect(0, 0, width, height);

    // Actualizar y dibujar Orbes de Aurora
    orbs.forEach((orb) => {
      orb.x += orb.vx;
      orb.y += orb.vy;

      if (orb.x - orb.radius < 0 || orb.x + orb.radius > width) orb.vx *= -1;
      if (orb.y - orb.radius < 0 || orb.y + orb.radius > height) orb.vy *= -1;

      const grad = ctx.createRadialGradient(orb.x, orb.y, 0, orb.x, orb.y, orb.radius);
      grad.addColorStop(0, orb.color);
      grad.addColorStop(1, 'transparent');

      ctx.fillStyle = grad;
      ctx.beginPath();
      ctx.arc(orb.x, orb.y, orb.radius, 0, Math.PI * 2);
      ctx.fill();
    });

    // Dibujar Cuadrícula Táctica (Squares)
    const time = performance.now() * 0.001;
    ctx.lineWidth = 1;

    for (let x = 0; x < width; x += gridSize) {
      for (let y = 0; y < height; y += gridSize) {
        // Distancia al puntero
        const dx = pointer.x - x;
        const dy = pointer.y - y;
        const dist = Math.sqrt(dx * dx + dy * dy);

        let cellAlpha = 0.035;

        // Si el puntero está cerca, la celda se ilumina sutilmente
        if (pointer.active && dist < 140) {
          const proximity = 1 - dist / 140;
          cellAlpha += proximity * 0.12;

          // Relleno sutil del cuadrado activo
          ctx.fillStyle = `rgba(16, 185, 129, ${proximity * 0.08})`;
          ctx.fillRect(x, y, gridSize, gridSize);
        }

        ctx.strokeStyle = `rgba(255, 255, 255, ${cellAlpha})`;
        ctx.strokeRect(x, y, gridSize, gridSize);
      }
    }

    requestAnimationFrame(draw);
  }

  requestAnimationFrame(draw);
}

// ── 2. SPOTLIGHT CARDS (Efecto de halo de luz que sigue el mouse/dedo) ──
export function initSpotlightCards() {
  const cards = document.querySelectorAll('.spotlight-card, .opt-card, .status-hero, .status-shelf');

  cards.forEach((card) => {
    card.classList.add('spotlight-card');

    const handleMove = (e) => {
      const rect = card.getBoundingClientRect();
      const clientX = e.touches ? e.touches[0].clientX : e.clientX;
      const clientY = e.touches ? e.touches[0].clientY : e.clientY;
      const x = clientX - rect.left;
      const y = clientY - rect.top;

      card.style.setProperty('--mouse-x', `${x}px`);
      card.style.setProperty('--mouse-y', `${y}px`);
    };

    card.addEventListener('pointermove', handleMove);
    card.addEventListener('touchmove', handleMove, { passive: true });
  });
}

// ── 3. DECRYPTED TEXT (Efecto de desencriptación hacker/militar) ──
const CHARACTERS = '0123456789ABCDEF!@#$%&*+<>_';

export function decryptText(element, targetText, duration = 650, speed = 40) {
  if (!element) return;
  const original = targetText || element.textContent || '';
  const length = original.length;
  const startTime = performance.now();

  function update(now) {
    const elapsed = now - startTime;
    const progress = Math.min(elapsed / duration, 1);
    const resolvedIndex = Math.floor(progress * length);

    let output = '';
    for (let i = 0; i < length; i++) {
      if (original[i] === ' ') {
        output += ' ';
      } else if (i < resolvedIndex) {
        output += original[i];
      } else {
        output += CHARACTERS[Math.floor(Math.random() * CHARACTERS.length)];
      }
    }

    element.textContent = output;

    if (progress < 1) {
      setTimeout(() => requestAnimationFrame(update), speed);
    } else {
      element.textContent = original;
    }
  }

  requestAnimationFrame(update);
}

// ── 4. CLICK SPARK (Chispas micro-interactivas al hacer clic/tap) ──
export function initClickSparks() {
  const buttons = document.querySelectorAll('button, .btn-pill, .btn-primary, .btn-secondary, .tab-btn');

  buttons.forEach((btn) => {
    btn.addEventListener('pointerdown', (e) => {
      createSpark(e.clientX, e.clientY);
    });
  });

  function createSpark(x, y) {
    const container = document.createElement('div');
    container.className = 'spark-container';
    container.style.left = `${x}px`;
    container.style.top = `${y}px`;
    document.body.appendChild(container);

    const sparkCount = 8;
    for (let i = 0; i < sparkCount; i++) {
      const spark = document.createElement('div');
      spark.className = 'spark-particle';

      const angle = (i / sparkCount) * Math.PI * 2 + (Math.random() * 0.4 - 0.2);
      const distance = 25 + Math.random() * 25;
      const dx = Math.cos(angle) * distance;
      const dy = Math.sin(angle) * distance;

      spark.style.setProperty('--dx', `${dx}px`);
      spark.style.setProperty('--dy', `${dy}px`);
      container.appendChild(spark);
    }

    setTimeout(() => {
      container.remove();
    }, 450);
  }
}
