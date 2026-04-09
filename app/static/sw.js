const CACHE = 'gooey-remote-v1';
const PRECACHE = [
  '/remote',
  '/static/css/style.css',
  '/static/js/remote.js',
  '/static/js/socket.io.min.js',
  '/static/favicon.svg',
];

self.addEventListener('install', e =>
  e.waitUntil(
    caches.open(CACHE)
      .then(c => c.addAll(PRECACHE))
      .then(() => self.skipWaiting())
  )
);

self.addEventListener('activate', e =>
  e.waitUntil(
    caches.keys()
      .then(keys => Promise.all(
        keys.filter(k => k !== CACHE).map(k => caches.delete(k))
      ))
      .then(() => self.clients.claim())
  )
);

self.addEventListener('fetch', e => {
  // Network-only for SocketIO and API routes.
  if (e.request.url.includes('/socket.io') || e.request.url.includes('/api')) return;
  e.respondWith(
    caches.match(e.request).then(cached => cached || fetch(e.request))
  );
});
