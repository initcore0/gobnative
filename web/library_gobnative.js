// gob2native - web build: the browser side of src/frontend/web.h
// SPDX-License-Identifier: GPL-3.0-or-later
//
// An Emscripten JS library (--js-library). Before main() runs, the app's
// folder /gobnative is mounted as an IndexedDB file system (IDBFS) and
// loaded from the browser's storage; changes are written back on request
// (gob_web_sync), every few seconds and when the page is hidden. Files the
// player picks are read with <input type=file> into the in-memory file
// system (asynchronously; the app polls the state once per frame).
//
// Memory: the in-memory file system holds only what the page needs (a
// phone's tab has a few hundred megabytes). The games' big files (all but
// the .STK archives the library reads for detection and covers) and the
// HD packs, music and screenshots stay in IndexedDB: the page has an empty
// file in their place, marked as stored (node.gobStored, its path in the
// storage). Pressing Play fills in the game's folder and its settings
// folder (gob_web_load) before the game opens. The page writes back with
// its own sync instead of IDBFS's, which would delete what it didn't load:
// a stored file stays as it is, or is copied within IndexedDB when it was
// moved; after a write, what was written and isn't needed is emptied
// again (an import, in the library). A picked installer is held in pieces
// that are dropped as the import reads past them (gob_web_free_as_read).
//
// On a touchscreen the touch bar's Keys button shows the phone's keyboard:
// a hidden text field is focused during the tap itself (phones show their
// keyboard for nothing else), and what is typed in it goes to the app as
// SDL key events (gob_web_key in src/frontend/web.cpp).
//
// For tests on this computer only (the page served from localhost),
// index.html can ask for an installer to be fetched from the same server
// before the app starts (Module.gobDevInstaller).

addToLibrary({
	$GobWeb__deps: ['$FS', '$MEMFS', '$IDBFS', '$addRunDependency', '$removeRunDependency', '$addOnPreRun', 'gob_web_key'],
	$GobWeb__postset: 'addOnPreRun(() => GobWeb.preRun());',
	$GobWeb: {
		root: '/gobnative',
		temp: '/tmp/gobnative-import',
		mount: null,   // the IDBFS mount of root
		loaded: false, // the storage was read (until then nothing is written back)
		kept: [],      // folders whose files are all in memory (the game being played)
		loading: 0,    // gob_web_load()s running
		piece: 4 << 20, // a picked file is held in pieces of this size
		syncing: false,
		syncAgain: false,
		afterSync: [],
		autoSync: true,
		// The file picker: state 0 idle, 1 choosing, 2 reading, 3 done,
		// 4 cancelled, 5 failed (web.h PickState).
		pick: { state: 0, progress: 0, current: '', result: '' },
		// Files dropped onto the page, read into the temporary folder; the
		// app takes their paths once per frame (gob_web_take_dropped).
		dropped: [],
		dropping: 0,
		// The Keys button, [x, y, w, h] in CSS pixels of the canvas (null
		// while it isn't shown), and the hidden text field.
		keysButton: null,
		keysField: null,
		// The storage: bytes used (-1 until known) and whether the browser
		// keeps it (null until known).
		storage: { used: -1, kept: null },

		status(text) {
			if (Module['gobStatus'])
				Module['gobStatus'](text);
		},

		isLocalhost() {
			return ['localhost', '127.0.0.1', '[::1]', '::1'].includes(location.hostname);
		},

		// A phone or tablet: the main pointer is a finger.
		isTouch() {
			return matchMedia('(pointer: coarse)').matches;
		},

		// Started from the home screen (a web app with a screen of its own).
		isStandalone() {
			return matchMedia('(display-mode: fullscreen), (display-mode: standalone)').matches ||
				navigator.standalone === true;
		},

		refreshStorage() {
			const s = navigator.storage;
			if (!s || !s.estimate)
				return;
			s.estimate().then((e) => { GobWeb.storage.used = e.usage || 0; }).catch(() => {});
			if (s.persisted)
				s.persisted().then((p) => { GobWeb.storage.kept = p; }).catch(() => {});
		},

		preRun() {
			FS.mkdirTree(GobWeb.root);
			GobWeb.mount = FS.mount(IDBFS, { autoPersist: false }, GobWeb.root).mount;
			FS.mkdirTree(GobWeb.temp);
			addRunDependency('gobnative-storage');
			GobWeb.status("Loading your games from the browser's storage...");
			if (GobWeb.isLocalhost()) { // for tests: testFiles, the file system
				Module['gobWeb'] = GobWeb;
				Module['gobFS'] = FS;
			}
			// A game folder given as an argument (?arg=, localhost) starts
			// that game without the library: its files are needed now.
			for (const a of Module['arguments'] || []) {
				const m = a.startsWith(GobWeb.root + '/Games/') && /^Games\/([^/]+)/.exec(a.slice(GobWeb.root.length + 1));
				if (m)
					GobWeb.kept.push(GobWeb.root + '/Games/' + m[1], GobWeb.root + '/' + m[1]);
			}
			const t0 = performance.now();
			GobWeb.load((err, stats) => {
				if (err)
					console.error('gobnative: cannot read the browser storage:', err);
				else
					console.log(`gobnative: storage loaded in ${Math.round(performance.now() - t0)} ms: ` +
						`${stats.loaded} entries read (${GobWeb.mb(stats.bytes)} MB), ${stats.stored} files left in the storage`);
				GobWeb.status('');
				GobWeb.refreshStorage();
				GobWeb.fetchDevInstaller().finally(() => removeRunDependency('gobnative-storage'));
			});
			setInterval(() => {
				if (GobWeb.autoSync && GobWeb.pick.state !== 2)
					GobWeb.sync();
			}, 5000);
			document.addEventListener('visibilitychange', () => {
				if (document.visibilityState === 'hidden')
					GobWeb.sync();
			});
			GobWeb.listenForDrops();
			GobWeb.listenForKeys();
			// The keys the app uses that browsers also want (save page,
			// address bar on macOS's Cmd).
			window.addEventListener('keydown', (e) => {
				if ((e.metaKey || e.ctrlKey) && (e.code === 'KeyS' || e.code === 'KeyL'))
					e.preventDefault();
			}, true);
		},

		// Drag and drop onto the page: files only (an installer, a pack zip);
		// the page shows a frame while something is dragged over it.
		listenForDrops() {
			let depth = 0;
			const hasFiles = (e) => e.dataTransfer && Array.from(e.dataTransfer.types || []).includes('Files');
			window.addEventListener('dragenter', (e) => {
				if (!hasFiles(e)) return;
				e.preventDefault();
				if (depth++ === 0) document.body.classList.add('gob-drag');
			});
			window.addEventListener('dragover', (e) => {
				if (!hasFiles(e)) return;
				e.preventDefault();
				e.dataTransfer.dropEffect = 'copy';
			});
			window.addEventListener('dragleave', (e) => {
				if (!hasFiles(e)) return;
				if (--depth <= 0) { depth = 0; document.body.classList.remove('gob-drag'); }
			});
			window.addEventListener('drop', async (e) => {
				if (!hasFiles(e)) return;
				e.preventDefault();
				depth = 0;
				document.body.classList.remove('gob-drag');
				const files = Array.from(e.dataTransfer.files || []).filter((f) => f.size > 0);
				GobWeb.dropping++;
				try {
					for (const f of files) {
						GobWeb.status('Reading ' + f.name + '...');
						GobWeb.dropped.push(await GobWeb.writePieces(GobWeb.temp + '/' + f.name, f));
					}
				} catch (err) {
					console.error('gobnative: cannot read the dropped files', err);
				} finally {
					GobWeb.dropping--;
					GobWeb.status('');
				}
			});
		},

		listenForKeys() {
			const field = document.createElement('input');
			field.type = 'text';
			field.autocomplete = 'off';
			field.autocapitalize = 'none';
			field.spellcheck = false;
			field.setAttribute('autocorrect', 'off');
			field.setAttribute('aria-hidden', 'true');
			field.tabIndex = -1;
			// Out of sight but focusable; 16px, as iOS zooms into a field
			// with smaller text.
			field.style.cssText = 'position:fixed;left:0;bottom:0;width:1px;height:1px;opacity:0;' +
				'border:0;padding:0;font-size:16px;pointer-events:none;';
			document.body.appendChild(field);
			GobWeb.keysField = field;

			// SDL key codes; text keys are their (lower case) character.
			const named = { Enter: 13, Backspace: 8, Escape: 27, Tab: 9, Delete: 127 };
			const key = (sym, shift, ch) => _gob_web_key(sym, shift ? 1 : 0, ch || 0);
			const type = (text) => {
				for (const c of text) {
					if (c === '\n') {
						key(13, false, 0);
						continue;
					}
					const lower = c.toLowerCase();
					const code = c.codePointAt(0);
					key(lower.length === c.length ? lower.codePointAt(0) : code, lower !== c, code);
				}
			};
			// The keys go to the app from here only, not to SDL's listener
			// on the window too.
			field.addEventListener('keydown', (e) => {
				e.stopPropagation();
				if (e.isComposing)
					return;
				if (e.key in named) {
					e.preventDefault();
					key(named[e.key], false, 0);
				} else if (e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey) {
					e.preventDefault();
					type(e.key);
				}
				// else a phone keyboard's "Unidentified" key: the input
				// event has what it typed
			});
			field.addEventListener('keyup', (e) => e.stopPropagation());
			field.addEventListener('keypress', (e) => e.stopPropagation());
			// The field keeps one space, so that Backspace has something to
			// delete (phone keyboards send no key for it in an empty field).
			const reset = () => {
				field.value = ' ';
				field.setSelectionRange(1, 1);
			};
			field.addEventListener('input', (e) => {
				if (e.isComposing)
					return;
				if (e.inputType && e.inputType.startsWith('delete'))
					key(8, false, 0);
				else if (e.inputType === 'insertLineBreak')
					key(13, false, 0);
				else if (e.data)
					type(e.data);
				reset();
			});
			field.addEventListener('compositionend', (e) => {
				if (e.data)
					type(e.data);
				reset();
			});
			field.addEventListener('focus', reset);

			// The tap on the Keys button shows the keyboard, another hides it.
			const canvas = Module['canvas'];
			canvas.addEventListener('touchend', (e) => {
				const r = GobWeb.keysButton;
				if (!r)
					return;
				const b = canvas.getBoundingClientRect();
				for (const t of e.changedTouches) {
					const x = t.clientX - b.left, y = t.clientY - b.top;
					if (x >= r[0] && y >= r[1] && x < r[0] + r[2] && y < r[1] + r[3]) {
						if (document.activeElement === field)
							field.blur();
						else
							field.focus({ preventScroll: true });
						return;
					}
				}
			}, { capture: true });
		},

		// localhost only: Module.gobDevInstaller (a URL on the same server)
		// is fetched into the temporary folder for --import-at.
		async fetchDevInstaller() {
			const url = Module['gobDevInstaller'];
			if (!url || !GobWeb.isLocalhost())
				return;
			try {
				GobWeb.status('Fetching ' + url + ' (developer option)...');
				const res = await fetch(url);
				if (!res.ok)
					throw new Error(res.status + ' ' + res.statusText);
				const data = await res.blob();
				const name = decodeURIComponent(url.split('/').pop());
				await GobWeb.writePieces(GobWeb.temp + '/' + name, data);
				console.log(`gobnative: fetched ${url} (${data.size} bytes)`);
				GobWeb.status('');
			} catch (e) {
				console.error('gobnative: cannot fetch the installer', url, e);
				GobWeb.status('Cannot fetch ' + url + ': ' + e.message);
			}
		},

		mb(bytes) {
			return (bytes / (1 << 20)).toFixed(1);
		},

		// --- The storage (see the top of this file)

		// A file the page may leave in the storage: in a game's folder all
		// but the .STK archives at its top (the library detects the games
		// and draws their covers from those), and HD packs, music and
		// screenshots.
		mayLeave(path) {
			const rel = path.slice(GobWeb.root.length + 1);
			const game = /^Games\/[^/]+\/(.+)$/.exec(rel);
			if (game)
				return game[1].includes('/') || !/\.stk$/i.test(game[1]);
			return /^[^/]+\/(HDPacks|Music|Screenshots)\/./.test(rel);
		},
		// ... and leaves there: not in a kept folder, and a name with an
		// extension (the storage's list of paths doesn't tell an empty
		// folder from a file; a folder seldom has an extension).
		leaves(path) {
			return GobWeb.mayLeave(path) && /\.[^./]{1,5}$/.test(path) &&
				!GobWeb.kept.some((f) => path === f || path.startsWith(f + '/'));
		},

		node(path) {
			try {
				return FS.lookupPath(path, { follow: false }).node;
			} catch (e) {
				return null;
			}
		},

		// The file's contents now live only in the storage, under `path`.
		markStored(node, path) {
			node.contents = new Uint8Array(0);
			node.usedBytes = 0;
			node.gobStored = path;
			node.gobStoredTime = +node.mtime;
		},

		// Reads entries from IndexedDB into the file system, `paths` as
		// [where in the storage, where in the file system]; done(err,
		// bytes) when all are in.
		fetch(db, paths, done) {
			if (!paths.length)
				return done(null, 0);
			let bytes = 0, failed = null;
			const tx = db.transaction([IDBFS.DB_STORE_NAME], 'readonly');
			const store = tx.objectStore(IDBFS.DB_STORE_NAME);
			tx.oncomplete = () => done(failed, bytes);
			tx.onerror = tx.onabort = (e) => {
				e.preventDefault();
				done(failed || e.target.error, bytes);
			};
			for (const [from, path] of paths)
				store.get(from).onsuccess = (e) => {
					const entry = e.target.result;
					if (!entry || failed)
						return;
					try {
						const old = GobWeb.node(path);
						if (old && FS.isFile(old.mode) && !FS.isFile(entry.mode))
							FS.unlink(path); // a folder with a file's name, left at the start
						IDBFS.storeLocalEntry(path, entry, (err) => { if (err) throw err; });
						const node = GobWeb.node(path);
						if (node)
							delete node.gobStored;
						bytes += entry.contents ? entry.contents.length : 0;
					} catch (err) {
						failed = err;
					}
				};
		},

		// Reads the storage at the start: folders, the files the page needs,
		// and an empty file marked as stored for each of the others.
		load(done) {
			IDBFS.getRemoteSet(GobWeb.mount, (err, remote) => {
				if (err)
					return done(err);
				const paths = Object.keys(remote.entries).sort();
				const dirs = new Set(); // the paths with something in them
				for (const p of paths)
					for (let i = p.lastIndexOf('/'); i > GobWeb.root.length; i = p.lastIndexOf('/', i - 1)) {
						const d = p.slice(0, i);
						if (dirs.has(d))
							break; // and its parents
						dirs.add(d);
					}
				const fetch = [];
				let stored = 0;
				try {
					for (const d of [...dirs].sort())
						FS.mkdirTree(d);
					for (const p of paths) {
						const time = remote.entries[p].timestamp;
						if (dirs.has(p))
							continue;
						if (GobWeb.leaves(p)) {
							FS.writeFile(p, new Uint8Array(0));
							FS.utime(p, time, time);
							GobWeb.markStored(GobWeb.node(p), p);
							stored++;
						} else
							fetch.push([p, p]);
					}
				} catch (e) {
					return done(e);
				}
				GobWeb.fetch(remote.db, fetch, (err, bytes) => {
					if (err)
						return done(err);
					// The folders' times as stored (adding files changed them),
					// so that the next write leaves them.
					for (const d of [...dirs].sort().reverse())
						if (remote.entries[d])
							FS.utime(d, remote.entries[d].timestamp, remote.entries[d].timestamp);
					GobWeb.loaded = true;
					done(null, { loaded: fetch.length, bytes, stored });
				});
			});
		},

		// Fills in the stored files under `folders` (a game's, before it
		// starts), which are kept in memory from now on.
		fill(folders, done) {
			GobWeb.kept.push(...folders);
			const paths = [];
			const walk = (dir) => {
				let names = [];
				try {
					names = FS.readdir(dir);
				} catch (e) {
					return;
				}
				for (const name of names) {
					if (name === '.' || name === '..')
						continue;
					const p = dir + '/' + name;
					const node = GobWeb.node(p);
					if (!node)
						continue;
					if (FS.isDir(node.mode))
						walk(p);
					else if (node.gobStored !== undefined)
						paths.push([node.gobStored, p]);
				}
			};
			folders.forEach(walk);
			IDBFS.getDB(GobWeb.root, (err, db) => {
				if (err)
					return done(err);
				GobWeb.fetch(db, paths, done);
			});
		},

		// Writes what changed to IndexedDB, like IDBFS's sync but keeping
		// the stored files; done(err, written) with the files written
		// (path -> [node, time]).
		save(done) {
			if (!GobWeb.loaded) // still reading it (or that failed: then nothing is written over it)
				return done(null, new Map());
			const local = new Map(); // path -> node
			const walk = (dir) => {
				for (const name of FS.readdir(dir)) {
					if (name === '.' || name === '..')
						continue;
					const p = dir + '/' + name;
					const node = GobWeb.node(p);
					local.set(p, node);
					if (FS.isDir(node.mode))
						walk(p);
				}
			};
			try {
				walk(GobWeb.root);
			} catch (e) {
				return done(e);
			}
			IDBFS.getRemoteSet(GobWeb.mount, (err, remote) => {
				if (err)
					return done(err);
				const copy = [], write = [], remove = [];
				for (const [p, node] of local) {
					if (node.gobStored !== undefined) {
						if (+node.mtime === node.gobStoredTime) { // as it was loaded
							if (node.gobStored !== p)
								copy.push([node.gobStored, p, node]); // moved (or its folder)
							continue;
						}
						delete node.gobStored; // written since: what it holds now
					}
					const r = remote.entries[p];
					if (!r || +r.timestamp !== +node.mtime)
						write.push(p);
				}
				for (const p of Object.keys(remote.entries))
					if (!local.has(p))
						remove.push(p);
				if (!copy.length && !write.length && !remove.length)
					return done(null, new Map());
				const written = new Map();
				let failed = null;
				const tx = remote.db.transaction([IDBFS.DB_STORE_NAME], 'readwrite');
				const store = tx.objectStore(IDBFS.DB_STORE_NAME);
				tx.oncomplete = () => {
					for (const [, to, node] of copy)
						node.gobStored = to;
					done(failed, written);
				};
				tx.onerror = tx.onabort = (e) => {
					e.preventDefault();
					done(failed || e.target.error || new Error('the write was aborted'), written);
				};
				// In this order: the requests of a transaction run in the
				// order they are made, so the copies read before anything
				// is replaced or removed.
				for (const [from, to] of copy)
					store.get(from).onsuccess = (e) => {
						if (e.target.result)
							store.put(e.target.result, to);
					};
				for (const p of write.sort()) {
					const node = local.get(p);
					IDBFS.loadLocalEntry(p, (err, entry) => {
						if (err) {
							failed = failed || err;
							return;
						}
						store.put(entry, p);
						if (FS.isFile(node.mode))
							written.set(p, [node, +node.mtime]);
					});
				}
				for (const p of remove.sort().reverse())
					store.delete(p);
			});
		},

		// After a write: the files written that the page may leave in the
		// storage are emptied (unless changed since, or open).
		release(written) {
			const open = new Set(FS.streams.filter(Boolean).map((s) => s.node));
			let bytes = 0;
			for (const [p, [node, time]] of written) {
				if (!GobWeb.leaves(p) || +node.mtime !== time || open.has(node) || GobWeb.node(p) !== node)
					continue;
				bytes += node.usedBytes;
				GobWeb.markStored(node, p);
			}
			if (bytes)
				console.log(`gobnative: ${GobWeb.mb(bytes)} MB written to the storage and released from memory`);
		},

		// The contents of a file, from memory or from the storage.
		async readFile(path) {
			const node = GobWeb.node(path);
			if (!node || node.gobStored === undefined)
				return FS.readFile(path);
			const db = await new Promise((resolve, reject) =>
				IDBFS.getDB(GobWeb.root, (err, db) => (err ? reject(err) : resolve(db))));
			return new Promise((resolve, reject) => {
				const req = db.transaction([IDBFS.DB_STORE_NAME], 'readonly')
					.objectStore(IDBFS.DB_STORE_NAME).get(node.gobStored);
				req.onsuccess = () => resolve(req.result ? req.result.contents : new Uint8Array(0));
				req.onerror = () => reject(req.error);
			});
		},

		// A picked file (a Blob) into the temporary folder, as pieces: an
		// installer is read once from start to end, and the pieces behind
		// are dropped (freeAsRead).
		async writePieces(path, blob) {
			const pieces = [];
			for (let at = 0; at < blob.size; at += GobWeb.piece)
				pieces.push(new Uint8Array(await blob.slice(at, at + GobWeb.piece).arrayBuffer()));
			FS.writeFile(path, new Uint8Array(0));
			const node = GobWeb.node(path);
			node.gobPieces = pieces;
			node.gobFreed = 0;  // pieces dropped (from the first)
			node.gobFree = false;
			node.usedBytes = blob.size;
			node.stream_ops = GobWeb.piecesOps ||= {
				...node.stream_ops,
				read: GobWeb.piecesRead,
				write: () => { throw new FS.ErrnoError({{{ cDefs.EPERM }}}); },
			};
			return path;
		},
		piecesOps: null,

		piecesRead(stream, buffer, offset, length, position) {
			const node = stream.node, size = GobWeb.piece;
			if (position >= node.usedBytes)
				return 0;
			length = Math.min(node.usedBytes - position, length);
			for (let done = 0; done < length;) {
				const at = position + done, i = Math.floor(at / size), piece = node.gobPieces[i];
				if (!piece)
					throw new FS.ErrnoError({{{ cDefs.EIO }}}); // read again after it was dropped
				const n = Math.min(piece.length - (at - i * size), length - done);
				buffer.set(piece.subarray(at - i * size, at - i * size + n), offset + done);
				done += n;
			}
			if (node.gobFree) // the pieces before the one before this read
				for (const end = Math.floor(position / size) - 1; node.gobFreed < end; node.gobFreed++)
					node.gobPieces[node.gobFreed] = null;
			return length;
		},

		// Writes /gobnative to IndexedDB; one write at a time. `done` is
		// called after the write that includes everything up to now.
		sync(done) {
			if (done)
				GobWeb.afterSync.push(done);
			if (GobWeb.syncing) {
				GobWeb.syncAgain = true;
				return;
			}
			GobWeb.syncing = true;
			const callbacks = GobWeb.afterSync;
			GobWeb.afterSync = [];
			GobWeb.save((err, written) => {
				if (err)
					console.error('gobnative: cannot write the browser storage:', err);
				else
					GobWeb.release(written);
				GobWeb.syncing = false;
				callbacks.forEach((f) => f(err));
				if (GobWeb.syncAgain || GobWeb.afterSync.length) {
					GobWeb.syncAgain = false;
					GobWeb.sync();
				}
			});
		},

		// Files at the top of a game folder that the games never read.
		skipped(name) {
			return /\.(exe|dll|ico|zip|dat|bin|lnk|url|info|hashdb|ini)$/i.test(name) || name.startsWith('.');
		},

		canPickFolders() {
			return 'webkitdirectory' in HTMLInputElement.prototype && !GobWeb.isTouch();
		},

		pickStart(accept, multiple, folder, topOnly, dest) {
			const p = GobWeb.pick;
			// Phones: no folders (the player chooses the files in it) and
			// no filter (".exe" has no file type a phone knows, so its
			// dialog would grey the installers out).
			const asFiles = folder && !GobWeb.canPickFolders();
			if (asFiles) {
				folder = false;
				multiple = true;
			}
			if (GobWeb.isTouch())
				accept = '';
			p.state = 1;
			p.progress = 0;
			p.current = '';
			p.result = '';
			const input = document.createElement('input');
			input.type = 'file';
			if (accept)
				input.accept = accept;
			input.multiple = multiple;
			if (folder)
				input.webkitdirectory = true;
			input.style.display = 'none';
			document.body.appendChild(input);
			input.addEventListener('cancel', () => {
				input.remove();
				if (p.state === 1)
					p.state = 4;
			});
			input.addEventListener('change', () => {
				const chosen = Array.from(input.files || []);
				input.remove();
				if (p.state === 1) // not given up meanwhile
					GobWeb.store(chosen, folder, topOnly || asFiles, dest);
			});
			// Tests (localhost): files given by the page instead of the dialog.
			if (GobWeb.testFiles) {
				const files = GobWeb.testFiles;
				GobWeb.testFiles = null;
				input.remove();
				GobWeb.store(files, folder, topOnly || asFiles, dest);
				return;
			}
			input.click();
		},

		// Writes the chosen files (File objects) below dest.
		async store(chosen, folder, topOnly, dest) {
			const p = GobWeb.pick;
			// Where each file goes: a folder keeps its own name below
			// dest, unless only its top-level files are wanted.
			const files = [];
			for (const f of chosen) {
				let rel = f.name;
				if (!folder && topOnly && GobWeb.skipped(f.name))
					continue;
				if (folder) {
					const parts = (f.webkitRelativePath || f.name).split('/');
					if (topOnly) {
						if (parts.length !== 2 || GobWeb.skipped(parts[1]))
							continue;
						rel = parts[1];
					} else
						rel = parts.join('/');
				}
				files.push({ file: f, rel });
			}
			if (!files.length) {
				p.result = chosen.length ? 'no usable files there' : '';
				p.state = chosen.length ? 5 : 4;
				return;
			}
			p.state = 2;
			try {
				const total = files.reduce((n, f) => n + f.file.size, 0);
				let done = 0;
				const written = [];
				for (const { file, rel } of files) {
					p.current = rel;
					const path = dest + '/' + rel;
					FS.mkdirTree(path.substring(0, path.lastIndexOf('/')));
					if (dest === GobWeb.temp) // an installer or a zip, read once
						await GobWeb.writePieces(path, file);
					else
						FS.writeFile(path, new Uint8Array(await file.arrayBuffer()), { canOwn: true }); // no copy
					written.push(path);
					done += file.size;
					p.progress = total ? done / total : 1;
				}
				p.result = written.join('\n');
				p.state = 3;
			} catch (e) {
				console.error('gobnative: cannot read the files', e);
				p.result = String((e && e.message) || e);
				p.state = 5;
			}
		},

		async blobUrl(path, mime) {
			return URL.createObjectURL(new Blob([await GobWeb.readFile(path)], { type: mime }));
		},
	},

	gob_web_is_localhost__deps: ['$GobWeb'],
	gob_web_is_localhost: () => (GobWeb.isLocalhost() ? 1 : 0),

	gob_web_sync__deps: ['$GobWeb'],
	gob_web_sync: () => GobWeb.sync(() => GobWeb.refreshStorage()),

	gob_web_load__deps: ['$GobWeb', '$UTF8ToString'],
	gob_web_load: (folders) => {
		const list = UTF8ToString(folders).split('\n').filter(Boolean);
		GobWeb.loading++;
		const t0 = performance.now();
		GobWeb.fill(list, (err, bytes) => {
			if (err)
				console.error('gobnative: cannot read the game from the browser storage:', err);
			else
				console.log(`gobnative: ${GobWeb.mb(bytes)} MB loaded for ${list.join(', ')} ` +
					`in ${Math.round(performance.now() - t0)} ms`);
			GobWeb.loading--;
		});
	},

	gob_web_loading__deps: ['$GobWeb'],
	gob_web_loading: () => (GobWeb.loading ? 1 : 0),

	gob_web_free_as_read__deps: ['$GobWeb', '$UTF8ToString'],
	gob_web_free_as_read: (path) => {
		const node = GobWeb.node(UTF8ToString(path));
		if (node && node.gobPieces)
			node.gobFree = true;
	},

	gob_web_sync_busy__deps: ['$GobWeb'],
	gob_web_sync_busy: () => (GobWeb.syncing ? 1 : 0),

	gob_web_autosync__deps: ['$GobWeb'],
	gob_web_autosync: (on) => {
		GobWeb.autoSync = !!on;
	},

	gob_web_pick__deps: ['$GobWeb', '$UTF8ToString'],
	gob_web_pick: (accept, multiple, folder, topOnly, dest) =>
		GobWeb.pickStart(UTF8ToString(accept), !!multiple, !!folder, !!topOnly, UTF8ToString(dest)),

	gob_web_pick_state__deps: ['$GobWeb'],
	gob_web_pick_state: () => GobWeb.pick.state,

	gob_web_pick_progress__deps: ['$GobWeb'],
	gob_web_pick_progress: () => GobWeb.pick.progress,

	gob_web_pick_current__deps: ['$GobWeb', '$stringToNewUTF8'],
	gob_web_pick_current: () => stringToNewUTF8(GobWeb.pick.current),

	// The result, and back to idle.
	gob_web_pick_result__deps: ['$GobWeb', '$stringToNewUTF8'],
	gob_web_pick_result: () => {
		const p = GobWeb.pick;
		const r = stringToNewUTF8(p.result);
		p.state = 0;
		p.result = '';
		p.current = '';
		p.progress = 0;
		return r;
	},

	gob_web_download__deps: ['$GobWeb', '$UTF8ToString'],
	gob_web_download: (path, name, mime) => {
		name = UTF8ToString(name);
		GobWeb.blobUrl(UTF8ToString(path), UTF8ToString(mime)).then((url) => {
			const a = document.createElement('a');
			a.href = url;
			a.download = name;
			document.body.appendChild(a);
			a.click();
			a.remove();
			setTimeout(() => URL.revokeObjectURL(url), 60000);
		}).catch((e) => console.error('gobnative: cannot read the file', e));
	},

	gob_web_open_file__deps: ['$GobWeb', '$UTF8ToString'],
	// (A file left in the storage takes a moment to read: the tab still
	// opens, within the few seconds a click lets a page open one.)
	gob_web_open_file: (path, mime) => {
		GobWeb.blobUrl(UTF8ToString(path), UTF8ToString(mime)).then((url) => {
			window.open(url, '_blank');
			setTimeout(() => URL.revokeObjectURL(url), 600000);
		}).catch((e) => console.error('gobnative: cannot read the file', e));
	},

	// The page again, without the developer options (a fresh start of the
	// app, as the desktop app execs itself for the library).
	gob_web_reload__deps: ['$GobWeb', '$UTF8ToString'],
	gob_web_reload: (query) => {
		const q = UTF8ToString(query);
		GobWeb.status('Saving...');
		GobWeb.sync(() => location.replace(location.pathname + (q ? '?' + q : '')));
	},

	gob_web_take_dropped__deps: ['$GobWeb', '$stringToNewUTF8'],
	gob_web_take_dropped: () => {
		const r = stringToNewUTF8(GobWeb.dropped.join('\n'));
		GobWeb.dropped = [];
		return r;
	},

	gob_web_open_url__deps: ['$UTF8ToString'],
	gob_web_open_url: (url) => {
		window.open(UTF8ToString(url), '_blank', 'noopener');
	},

	gob_web_keyboard_button__deps: ['$GobWeb'],
	gob_web_keyboard_button: (x, y, w, h) => {
		GobWeb.keysButton = w > 0 ? [x, y, w, h] : null;
		if (!GobWeb.keysButton && GobWeb.keysField && document.activeElement === GobWeb.keysField)
			GobWeb.keysField.blur(); // the bar is gone (the overlay opened, a mouse)
	},

	gob_web_playing: (playing) => {
		document.body.classList.toggle('gob-playing', !!playing);
	},

	gob_web_is_touch__deps: ['$GobWeb'],
	gob_web_is_touch: () => (GobWeb.isTouch() ? 1 : 0),

	gob_web_fullscreen__deps: ['$GobWeb'],
	gob_web_fullscreen: () => {
		const page = document.documentElement;
		if (!GobWeb.isTouch() || GobWeb.isStandalone() || document.fullscreenElement || !page.requestFullscreen)
			return; // (an iPhone has no fullscreen for pages: the home-screen app is)
		page.requestFullscreen({ navigationUI: 'hide' })
			.then(() => screen.orientation && screen.orientation.lock && screen.orientation.lock('landscape'))
			.catch(() => {});
	},

	gob_web_persist__deps: ['$GobWeb'],
	gob_web_persist: () => {
		const s = navigator.storage;
		if (!s || !s.persist || GobWeb.storage.kept)
			return;
		s.persist().then((kept) => {
			GobWeb.storage.kept = kept;
			console.log('gobnative: persistent storage ' + (kept ? 'granted' : 'not granted'));
		}).catch(() => {}).finally(() => GobWeb.refreshStorage());
	},

	gob_web_storage_info__deps: ['$GobWeb', '$stringToNewUTF8'],
	gob_web_storage_info: () => {
		const { used, kept } = GobWeb.storage;
		let text = '';
		if (used >= 1 << 20) {
			text = `Your games and saves use ${Math.round(used / (1 << 20))} MB of this browser's storage. `;
			if (kept)
				text += 'The browser keeps it until you remove it.';
			else if (GobWeb.isTouch() && !GobWeb.isStandalone())
				text += "The browser may clear it after a while without a visit; add this page to your " +
					'home screen to keep it.';
			else
				text += 'The browser may clear it when space runs low.';
		}
		return stringToNewUTF8(text);
	},

	gob_web_can_pick_folders__deps: ['$GobWeb'],
	gob_web_can_pick_folders: () => (GobWeb.canPickFolders() ? 1 : 0),

	gob_web_status__deps: ['$GobWeb', '$UTF8ToString'],
	gob_web_status: (text) => GobWeb.status(UTF8ToString(text)),
});
