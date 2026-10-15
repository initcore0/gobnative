// gob2native - the web page's service worker (scripts/build-web.sh copies it
// to dist/web/; index.html registers it when served over HTTPS)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keeps the page's own files, so that once opened it also starts without a
// network (the home-screen app on a phone). Network first, so a new version
// shows as soon as there is one; the cache only when the network fails.
// The games, settings and saves are in IndexedDB, never here.

const CACHE = 'gobnative';
const FILES = ['./', 'gobnative.js', 'gobnative.wasm', 'manifest.webmanifest', 'favicon-32.png',
	'apple-touch-icon.png', 'icon-192.png', 'icon-512.png'];

self.addEventListener('install', (e) => {
	e.waitUntil(caches.open(CACHE).then((c) => c.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => e.waitUntil(self.clients.claim()));

self.addEventListener('fetch', (e) => {
	const scope = self.registration.scope;
	if (e.request.method !== 'GET' || !e.request.url.startsWith(scope))
		return;
	// The page (with any query) is kept as './'.
	let file = e.request.url.slice(scope.length).split(/[?#]/)[0];
	if (file === '' || file === 'index.html')
		file = './';
	if (!FILES.includes(file))
		return;
	e.respondWith(fetch(e.request).then((response) => {
		if (response.ok) {
			const copy = response.clone();
			caches.open(CACHE).then((c) => c.put(file, copy));
		}
		return response;
	}).catch(() => caches.match(file).then((cached) => cached || Response.error())));
});
