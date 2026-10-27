const CACHE_NAME = 'nexus-access-v2';
const ASSETS_TO_CACHE = [
  '/',
  '/index.html',
  '/manifest.webmanifest',
  '/style.css',
  '/js/app.js',
  '/js/recognition.js',
  '/js/bluetooth.js',
  '/js/effects.js',
  '/icon.svg',
  '/icons/icon.svg'
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => {
      console.log('[PWA SW] Pre-caching static assets');
      return cache.addAll(ASSETS_TO_CACHE).catch(e => console.warn('Pre-cache error:', e));
    })
  );
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((keys) => {
      return Promise.all(
        keys.map((key) => {
          if (key !== CACHE_NAME) {
            console.log('[PWA SW] Removing old cache', key);
            return caches.delete(key);
          }
        })
      );
    })
  );
  self.clients.claim();
});

self.addEventListener('fetch', (event) => {
  // Ignorar protocolos no http/https
  if (!event.request.url.startsWith('http')) return;

  const url = new URL(event.request.url);

  // NO interceptar peticiones a CDNs externos (Google Fonts, Firebase, jsdelivr, etc.)
  // Deja que el navegador maneje directamente los headers y tipos MIME correctos
  if (url.origin !== self.location.origin) {
    return;
  }

  event.respondWith(
    caches.match(event.request).then((cachedResponse) => {
      if (cachedResponse) {
        return cachedResponse;
      }
      return fetch(event.request).then((networkResponse) => {
        if (networkResponse && networkResponse.status === 200) {
          const clone = networkResponse.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(event.request, clone));
        }
        return networkResponse;
      }).catch((err) => {
        // Solo devolver index.html para navegación entre páginas HTML, nunca para estilos o imágenes
        if (event.request.mode === 'navigate') {
          return caches.match('/index.html');
        }
        return Promise.reject(err);
      });
    })
  );
});
