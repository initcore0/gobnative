// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/win32.h"

#include <SDL.h>
#include <SDL_syswm.h>

#include <cstdio>

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace gob {

namespace {

std::wstring widen(const std::string &s) {
	if (s.empty())
		return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
	std::wstring w(size_t(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
	return w;
}

std::string narrow(const wchar_t *w) {
	const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1)
		return {};
	std::string s(size_t(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
	s.resize(size_t(n - 1));
	return s;
}

HWND windowHandle(SDL_Window *window) {
	if (!window)
		return nullptr;
	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	return SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_WINDOWS ? info.info.win.window : nullptr;
}

// COM for the dialog's thread (SDL may have set it up already, possibly
// in another mode: the dialog works either way).
struct ComScope {
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	~ComScope() {
		if (SUCCEEDED(hr))
			CoUninitialize();
	}
};

template <class T> void release(T *&p) {
	if (p)
		p->Release();
	p = nullptr;
}

std::string run(SDL_Window *owner, const std::string &title, bool folders, const std::string &filterName,
                const std::string &pattern) {
	ComScope com;
	IFileOpenDialog *dialog = nullptr;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
		return {};
	FILEOPENDIALOGOPTIONS options = 0;
	dialog->GetOptions(&options);
	options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
	options |= folders ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
	dialog->SetOptions(options);
	const std::wstring wtitle = widen(title);
	dialog->SetTitle(wtitle.c_str());
	const std::wstring wname = widen(filterName), wpattern = widen(pattern);
	if (!folders && !pattern.empty()) {
		const COMDLG_FILTERSPEC filters[] = {{wname.c_str(), wpattern.c_str()}, {L"All files", L"*.*"}};
		dialog->SetFileTypes(2, filters);
		dialog->SetFileTypeIndex(1);
	}
	if (!folders) {
		// Where the installers usually are; the dialog remembers the last
		// folder chosen and starts there after the first time.
		PWSTR downloads = nullptr;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &downloads))) {
			IShellItem *item = nullptr;
			if (SUCCEEDED(SHCreateItemFromParsingName(downloads, nullptr, IID_PPV_ARGS(&item))))
				dialog->SetDefaultFolder(item);
			release(item);
		}
		CoTaskMemFree(downloads);
	}
	std::string chosen;
	if (SUCCEEDED(dialog->Show(windowHandle(owner)))) {
		IShellItem *item = nullptr;
		if (SUCCEEDED(dialog->GetResult(&item))) {
			PWSTR path = nullptr;
			if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
				chosen = narrow(path);
			CoTaskMemFree(path);
		}
		release(item);
	}
	release(dialog);
	// The clicks that went to the dialog must not reach the game library.
	SDL_FlushEvents(SDL_MOUSEMOTION, SDL_MOUSEWHEEL);
	return chosen;
}

} // namespace

std::string windowsChooseFolder(SDL_Window *owner, const std::string &title) {
	return run(owner, title, true, {}, {});
}

std::string windowsChooseFile(SDL_Window *owner, const std::string &title, const std::string &filterName,
                              const std::string &pattern) {
	return run(owner, title, false, filterName, pattern);
}

void windowsAttachConsole() {
	if (!AttachConsole(ATTACH_PARENT_PROCESS))
		return;
	auto redirected = [](DWORD which) {
		const HANDLE h = GetStdHandle(which);
		return h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
	};
	if (!redirected(STD_OUTPUT_HANDLE))
		std::freopen("CONOUT$", "w", stdout);
	if (!redirected(STD_ERROR_HANDLE))
		std::freopen("CONOUT$", "w", stderr);
}

namespace {

// One argument quoted for CommandLineToArgvW (and the C runtime).
void appendQuoted(std::wstring &line, const std::wstring &arg) {
	if (!line.empty())
		line += L' ';
	if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
		line += arg;
		return;
	}
	line += L'"';
	for (size_t i = 0;; i++) {
		size_t backslashes = 0;
		while (i < arg.size() && arg[i] == L'\\') {
			i++;
			backslashes++;
		}
		if (i == arg.size()) {
			line.append(backslashes * 2, L'\\');
			break;
		}
		if (arg[i] == L'"')
			line.append(backslashes * 2 + 1, L'\\');
		else
			line.append(backslashes, L'\\');
		line += arg[i];
	}
	line += L'"';
}

} // namespace

bool windowsStartSelf(const std::vector<std::string> &args) {
	std::wstring exe(MAX_PATH, L'\0');
	for (;;) {
		const DWORD n = GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size()));
		if (n == 0)
			return false;
		if (n < exe.size()) {
			exe.resize(n);
			break;
		}
		exe.resize(exe.size() * 2);
	}
	std::wstring line;
	appendQuoted(line, exe);
	for (const std::string &a : args)
		appendQuoted(line, widen(a));
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
		return false;
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

} // namespace gob
