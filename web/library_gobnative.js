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
// On a touchscreen the touch bar's Keys button shows the phone's keyboard:
// a hidden text field is focused during the tap itself (phones show their
// keyboard for nothing else), and what is typed in it goes to the app as
// SDL key events (gob_web_key in src/frontend/web.cpp).
//
// For tests on this computer only (the page served from localhost),
// index.html can ask for an installer to be fetched from the same server
// before the app starts (Module.gobDevInstaller).

addToLibrary({
	$GobWeb__deps: ['$FS', '$IDBFS', '$addRunDependency', '$removeRunDependency', '$addOnPreRun', 'gob_web_key'],
	$GobWeb__postset: 'addOnPreRun(() => GobWeb.preRun());',
	$GobWeb: {
		root: '/gobnative',
		temp: '/tmp/gobnative-import',
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

		status(text) {
			if (Module['gobStatus'])
				Module['gobStatus'](text);
		},

		isLocalhost() {
			return ['localhost', '127.0.0.1', '[::1]', '::1'].includes(location.hostname);
		},

		preRun() {
			FS.mkdirTree(GobWeb.root);
			FS.mount(IDBFS, { autoPersist: false }, GobWeb.root);
			FS.mkdirTree(GobWeb.temp);
			addRunDependency('gobnative-storage');
			GobWeb.status("Loading your games from the browser's storage...");
			if (GobWeb.isLocalhost()) { // for tests: testFiles, the file system
				Module['gobWeb'] = GobWeb;
				Module['gobFS'] = FS;
			}
			const t0 = performance.now();
			FS.syncfs(true, (err) => {
				if (err)
					console.error('gobnative: cannot read the browser storage:', err);
				else
					console.log(`gobnative: storage loaded in ${Math.round(performance.now() - t0)} ms`);
				GobWeb.status('');
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
						const data = new Uint8Array(await f.arrayBuffer());
						const path = GobWeb.temp + '/' + f.name;
						FS.writeFile(path, data, { canOwn: true });
						GobWeb.dropped.push(path);
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
				const data = new Uint8Array(await res.arrayBuffer());
				const name = decodeURIComponent(url.split('/').pop());
				FS.writeFile(GobWeb.temp + '/' + name, data, { canOwn: true });
				console.log(`gobnative: fetched ${url} (${data.length} bytes)`);
				GobWeb.status('');
			} catch (e) {
				console.error('gobnative: cannot fetch the installer', url, e);
				GobWeb.status('Cannot fetch ' + url + ': ' + e.message);
			}
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
			FS.syncfs(false, (err) => {
				if (err)
					console.error('gobnative: cannot write the browser storage:', err);
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

		pickStart(accept, multiple, folder, topOnly, dest) {
			const p = GobWeb.pick;
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
					GobWeb.store(chosen, folder, topOnly, dest);
			});
			// Tests (localhost): files given by the page instead of the dialog.
			if (GobWeb.testFiles) {
				const files = GobWeb.testFiles;
				GobWeb.testFiles = null;
				input.remove();
				GobWeb.store(files, folder, topOnly, dest);
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
					const data = new Uint8Array(await file.arrayBuffer());
					const path = dest + '/' + rel;
					FS.mkdirTree(path.substring(0, path.lastIndexOf('/')));
					FS.writeFile(path, data, { canOwn: true }); // no copy
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

		blobUrl(path, mime) {
			const data = FS.readFile(path);
			return URL.createObjectURL(new Blob([data], { type: mime }));
		},
	},

	gob_web_is_localhost__deps: ['$GobWeb'],
	gob_web_is_localhost: () => (GobWeb.isLocalhost() ? 1 : 0),

	gob_web_sync__deps: ['$GobWeb'],
	gob_web_sync: () => GobWeb.sync(),

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
		const url = GobWeb.blobUrl(UTF8ToString(path), UTF8ToString(mime));
		const a = document.createElement('a');
		a.href = url;
		a.download = UTF8ToString(name);
		document.body.appendChild(a);
		a.click();
		a.remove();
		setTimeout(() => URL.revokeObjectURL(url), 60000);
	},

	gob_web_open_file__deps: ['$GobWeb', '$UTF8ToString'],
	gob_web_open_file: (path, mime) => {
		const url = GobWeb.blobUrl(UTF8ToString(path), UTF8ToString(mime));
		window.open(url, '_blank');
		setTimeout(() => URL.revokeObjectURL(url), 600000);
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

	gob_web_status__deps: ['$GobWeb', '$UTF8ToString'],
	gob_web_status: (text) => GobWeb.status(UTF8ToString(text)),
});
