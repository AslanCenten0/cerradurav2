const CACHE_NAME = 'nexus-access-v9';
const ASSETS_TO_CACHE = [
  './',
  'index.html',
  'manifest.webmanifest',
  'style.css',
  'js/app.js',
  'js/recognition.js',
  'js/effects.js',
  'icon.svg',
  'icon-192.png',
  'icon-512.png'
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => {
      console.log('[PWA SW] Pre-caching static assets v9');
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
  if (!event.request.url.startsWith('http')) return;

  const url = new URL(event.request.url);

  // Dejar que CDNs externos (Firebase, jsdelivr face-api) vayan directos a red
  if (url.origin !== self.location.origin) {
    return;
  }

  // Network-First para evitar que cambios de despliegue queden atrapados en caché
  event.respondWith(
    fetch(event.request)
      .then((networkResponse) => {
        if (networkResponse && networkResponse.status === 200) {
          const clone = networkResponse.clone();
          caches.open(CACHE_NAME).then((cache) => cache.put(event.request, clone));
        }
        return networkResponse;
      })
      .catch(() => caches.match(event.request))
  );
});
