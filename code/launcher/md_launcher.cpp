/*
===========================================================================
MovieDuels launcher
===========================================================================

The team's launcher (MovieDuels-SJE-Launcher-master, a WPF program) built into the game's code, the way the
SerenityJediEngine launcher is: same art, same layout, same behaviour, but a plain Win32 + GDI+ exe that CMake builds
with the game (project "Launcher", MovieDuels-Launcher.exe).

The window is the team's design (1920x1080 layers in art/) shown at 1088x612 units: the MovieDuels logo between two
bars of link buttons, Vader on the left and Kylo Ren / Rey on the right, and a bottom bar with SINGLEPLAYER MODE and
MULTIPLAYER MODE either side of the emblem. Hovering a mode fades in its glow layer.

- SINGLEPLAYER MODE starts MovieDuels-SP.x86_64.exe from the launcher's folder, passing the OPTIONS as "+set" values,
  and closes the launcher. MULTIPLAYER MODE opens the MULTIPLAYER page on the info card: START MULTIPLAYER starts
  MovieDuels-MP.x86_64.exe the same way, START SERVER runs "Start MovieDuels-MP-FFA-Server.bat" from the MD folder in
  its own console (the launcher stays open, so the game can be started next to join it). Multiplayer needs Update 9:
  on Update 9 assets the button only says it is coming soon.
- The gear opens the OPTIONS page on the info card: Renderer, Display, Resolution (read from and written to the game's
  own configs) and Controller (remembered in MovieDuels-Launcher.ini in Documents\My Games\MovieDuels-UD9).
- The other icons and HELP open web pages in the browser.
- Which update the game's assets are is read from them the way the game does it (ui/main.menu "uiScript mdUpdate N").

The window is a layered window with per-pixel alpha: its shape (the rounded corners) is the art's own alpha. It is
DPI aware: everything is laid out in the WPF launcher's units (1/96 inch) and drawn at the screen's scale, so it is as
sharp and as big as the WPF launcher was.
*/

#include <algorithm> // before gdiplus.h: with NOMINMAX its headers use std::min / std::max
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <mmsystem.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include <minizip/unzip.h>

#include "resource.h"
#include "win32/AutoVersion.h"

// minizip (lib/minizip/unzip.c) allocates through these; the engine has its own in z_memman_pc.cpp
extern "C" void* MovieDuels_minizip_malloc(const int size)
{
	return malloc(size > 0 ? static_cast<size_t>(size) : 1);
}

extern "C" void MovieDuels_minizip_free(void* to_free)
{
	free(to_free);
}

namespace
{
	// the WPF window's size, in units (1/96 inch); the art is stretched over it
	constexpr float kUnitW = 1088.0f;
	constexpr float kUnitH = 612.0f;

	const wchar_t* const kSPExe = L"MovieDuels-SP.x86_64.exe";
	const wchar_t* const kMPExe = L"MovieDuels-MP.x86_64.exe";

	// the mod folder (fs_game) next to the exes, and the dedicated server batch file in it (it does "cd .." itself,
	// so it runs from the mod folder)
	const wchar_t* const kModFolder = L"MD";
	const wchar_t* const kServerBat = L"Start MovieDuels-MP-FFA-Server.bat";

	// Release builds of the game only start when this is set on their process (shared/sys/sys_main.cpp
	// Sys_RequireLauncher: MD_LAUNCHER_ENV / MD_LAUNCHER_TOKEN); keep the two the same.
	const wchar_t* const kLauncherEnv = L"MOVIEDUELS_LAUNCHER";
	const wchar_t* const kLauncherToken = L"md-launcher-7f3a9c21";

	const wchar_t* const kIniName = L"MovieDuels-Launcher.ini";
	const wchar_t* const kHomeFolder = L"MovieDuels-UD9"; // the game's folder in My Games (HOMEPATH_NAME_WIN)
	const wchar_t* const kOldHomeFolder = L"MovieDuels";  // where the WPF launcher kept its ini (Update 9's folder)
	const wchar_t* const kSPConfig = L"MovieDuels-SP.cfg";
	const wchar_t* const kMPConfig = L"MovieDuels-MP.cfg";

	const wchar_t* const kUrlModDB = L"https://www.moddb.com/mods/movie-duels";
	const wchar_t* const kUrlHelp = L"https://www.moddb.com/mods/movie-duels/tutorials";
	const wchar_t* const kUrlDiscord = L"https://discord.com/invite/PhQPy7j";
	const wchar_t* const kUrlYouTube = L"https://www.youtube.com/@MovieDuels";
	const wchar_t* const kUrlJKHub = L"https://jkhub.org/files/file/3860-star-wars-movie-duels/";
	const wchar_t* const kUrlNexus = L"https://www.nexusmods.com/starwarsjediknightjediacademy/mods/45";

	// background music: MCI plays the MP3 (from a temp file), PlaySound the menu sounds alongside
	const wchar_t* const kMusicAlias = L"md_launcher_music";
	constexpr int kMusicVolume = 600; // 0..1000

	// the MovieDuels update of assets without "uiScript mdUpdate" in their main menu
	constexpr int kDefaultUpdate = 9;

	// colours from the art (the WPF launcher's brushes)
	const Gdiplus::Color kWhiteText(255, 249, 249, 249);
	const Gdiplus::Color kGoldText(255, 255, 204, 102);
	const Gdiplus::Color kGold(230, 253, 178, 42);
	const Gdiplus::Color kGoldBright(255, 255, 214, 120);
	const Gdiplus::Color kVersionGrey(150, 200, 200, 200);

	// the gold glow under the mouse (WPF DropShadowEffect #FFAA28, blur radius 12)
	constexpr BYTE kGlowR = 255, kGlowG = 170, kGlowB = 40;
	constexpr float kGlowBlur = 12.0f;
	constexpr float kGlowStrength = 1.5f;

	// the mode glow layers fade in and out in this time
	constexpr UINT_PTR kFadeTimer = 1;
	constexpr float kFadeMs = 180.0f;

	enum class Page { Main, Options, Multiplayer };

	// resolutions the OPTIONS page cycles through (0 x 0 = the desktop resolution, r_mode -2)
	struct Resolution
	{
		int width;
		int height;
	};
	const Resolution kResolutions[] = { { 0, 0 },{ 1280, 720 },{ 1366, 768 },{ 1600, 900 },{ 1920, 1080 },{ 2560, 1440 },
		{ 3840, 2160 } };
	// the engine's fixed r_mode list (shared/sdl/sdl_window.cpp r_vidModes, r_mode 0..20)
	const Resolution kRModes[] = { { 320, 240 },{ 400, 300 },{ 512, 384 },{ 640, 480 },{ 800, 600 },{ 856, 480 },
		{ 960, 720 },{ 1024, 768 },{ 1152, 864 },{ 1280, 720 },{ 1280, 1024 },{ 1366, 768 },{ 1440, 900 },
		{ 1600, 900 },{ 1600, 1200 },{ 1680, 1050 },{ 1920, 1080 },{ 2048, 1536 },{ 2400, 600 },{ 2560, 1440 },
		{ 3840, 2160 } };

	struct Settings
	{
		bool rend2 = false;
		bool windowed = false;
		bool controller = false; // in_joystick
		bool music = true;       // launcher background music (speaker button)
		Resolution resolution = { 0, 0 }; // { 0, 0 } = desktop
	};

	// The main page's buttons: the icons on the top bars, HELP, and the two modes on the bottom bar (units).
	struct MainButton
	{
		Gdiplus::RectF box;
		int icon;            // index into the icons, or -1 for a text button
		const wchar_t* text; // text buttons
		float size;          // their font size
		const wchar_t* url;  // link buttons
	};

	constexpr int kIconCount = 6;
	const int kIconResources[kIconCount] = { IDR_ICON_SETTINGS, IDR_ICON_MODDB, IDR_ICON_DISCORD, IDR_ICON_YOUTUBE,
		IDR_ICON_JKHUB, IDR_ICON_NEXUS };

	const MainButton kMainButtons[] = {
		{ { 72.0f, 54.5f, 32.0f, 32.0f }, 0, nullptr, 0.0f, nullptr }, // OPTIONS page
		{ { 161.0f, 54.5f, 32.0f, 32.0f }, 1, nullptr, 0.0f, kUrlModDB },
		{ { 217.0f, 58.0f, 100.0f, 26.0f }, -1, L"HELP", 19.0f, kUrlHelp },
		{ { 801.0f, 54.5f, 32.0f, 32.0f }, 2, nullptr, 0.0f, kUrlDiscord },
		{ { 865.0f, 54.5f, 32.0f, 32.0f }, 3, nullptr, 0.0f, kUrlYouTube },
		{ { 929.0f, 54.5f, 32.0f, 32.0f }, 4, nullptr, 0.0f, kUrlJKHub },
		{ { 993.0f, 54.5f, 32.0f, 32.0f }, 5, nullptr, 0.0f, kUrlNexus },
		{ { 140.0f, 544.0f, 360.0f, 29.0f }, -1, L"SINGLEPLAYER MODE", 20.0f, nullptr },
		{ { 588.0f, 544.0f, 360.0f, 29.0f }, -1, L"MULTIPLAYER MODE", 20.0f, nullptr },
	};
	constexpr int kMainButtonCount = static_cast<int>(sizeof kMainButtons / sizeof kMainButtons[0]);

	// hit ids: the main page's buttons, the info card's bars, the window buttons
	enum Hit
	{
		kHitNone = -1,
		kHitSettings = 0,
		kHitSingle = 7,
		kHitMulti = 8,
		kHitCard0 = kMainButtonCount, // up to five bars on the info card
		kHitMusic = kHitCard0 + 5,
		kHitMinimise,
		kHitClose
	};

	// the info card's column of bars (units)
	constexpr float kCardX = 364.0f;
	constexpr float kCardW = 360.0f;
	constexpr float kCardTitleY = 176.0f;
	constexpr float kCardBarY = 236.0f;
	constexpr float kCardBarH = 38.0f;
	constexpr float kCardBarStep = 50.0f; // bar + gap

	// the window buttons (units)
	constexpr float kWindowButtonY = 16.0f;
	constexpr float kWindowButtonSize = 22.0f;
	constexpr float kMusicX = 978.0f;
	constexpr float kMinimiseX = 1006.0f;
	constexpr float kCloseX = 1034.0f;

	// a glow drawn behind lit text or a lit icon (window pixels)
	struct Glow
	{
		std::unique_ptr<Gdiplus::Bitmap> bitmap;
		int x = 0;
		int y = 0;
	};

	struct Launcher
	{
		HINSTANCE instance = nullptr;
		HWND hwnd = nullptr;
		std::wstring folder; // where the launcher (and the game exes) are
		Settings settings;
		int md_update = kDefaultUpdate;
		Page page = Page::Main;
		int hover = kHitNone;
		int pressed = kHitNone;
		bool tracking_mouse = false;

		std::wstring music_file; // the music written to %TEMP% for MCI
		bool music_open = false;

		// window size in pixels, and pixels per unit
		float scale = 1.0f;
		int width = 0;
		int height = 0;

		// the art at window size: base (backing + background + play background), the two mode glows, top (play
		// panel + logo glow), card (the dimmer and the info panel)
		std::unique_ptr<Gdiplus::Bitmap> base, glow_single, glow_multi, top, card;
		std::unique_ptr<Gdiplus::Bitmap> icon[kIconCount], icon_hover[kIconCount];
		Glow icon_glow[kIconCount];
		std::map<std::wstring, Glow> text_glows;
		std::unique_ptr<Gdiplus::FontFamily> label_family; // freed before GDI+ goes, with the art

		// the mode glow layers' opacity
		float single_glow = 0.0f;
		float multi_glow = 0.0f;
		ULONGLONG fade_tick = 0;
		bool fading = false;

		// the layered window's pixels (32 bpp premultiplied DIB)
		HDC mem_dc = nullptr;
		HBITMAP dib = nullptr;
		HGDIOBJ old_bitmap = nullptr;
		void* bits = nullptr;
	};

	Launcher g_launcher;

	// ------------------------------------------------------------------------------------------
	// Resources
	// ------------------------------------------------------------------------------------------

	// The bytes of an RCDATA resource of the exe (valid while the program runs).
	bool GetResourceBytes(const int id, const void** bytes, DWORD* size)
	{
		const HRSRC res = FindResourceW(g_launcher.instance, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10)); // RT_RCDATA
		const HGLOBAL data = res ? LoadResource(g_launcher.instance, res) : nullptr;
		*bytes = data ? LockResource(data) : nullptr;
		*size = res ? SizeofResource(g_launcher.instance, res) : 0;
		return *bytes && *size;
	}

	// Loads a PNG from the exe's RCDATA resources.
	std::unique_ptr<Gdiplus::Bitmap> LoadImageResource(const int id)
	{
		const void* bytes;
		DWORD size;
		if (!GetResourceBytes(id, &bytes, &size))
		{
			return nullptr;
		}
		IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(bytes), size);
		if (!stream)
		{
			return nullptr;
		}
		// the bitmap reads from the stream while it lives: copy it so the stream can go
		std::unique_ptr<Gdiplus::Bitmap> from_stream(Gdiplus::Bitmap::FromStream(stream));
		std::unique_ptr<Gdiplus::Bitmap> copy;
		if (from_stream && from_stream->GetLastStatus() == Gdiplus::Ok)
		{
			copy.reset(from_stream->Clone(0, 0, from_stream->GetWidth(), from_stream->GetHeight(), PixelFormat32bppPARGB));
		}
		from_stream.reset();
		stream->Release();
		return copy;
	}

	// ------------------------------------------------------------------------------------------
	// Sound
	// ------------------------------------------------------------------------------------------

	// Menu sounds: a newer one cuts off the one still playing, like in the game's menus.
	// wait: play it to the end first (for a click right before the launcher closes).
	void PlaySfx(const int id, const bool wait = false)
	{
		const void* bytes;
		DWORD size;
		if (GetResourceBytes(id, &bytes, &size))
		{
			PlaySoundW(static_cast<LPCWSTR>(bytes), nullptr, SND_MEMORY | SND_NODEFAULT | (wait ? SND_SYNC : SND_ASYNC));
		}
	}

	// MCI can only play music from a file: write it to %TEMP% once, then loop it. When a play
	// finishes, MCI sends MM_MCINOTIFY to the window, which starts it again (see WndProc).
	void PlayMusicFromStart(const HWND hwnd)
	{
		const std::wstring cmd = std::wstring(L"play ") + kMusicAlias + L" from 0 notify";
		mciSendStringW(cmd.c_str(), nullptr, 0, hwnd);
	}

	void StartMusic(const HWND hwnd)
	{
		if (g_launcher.music_open || !g_launcher.settings.music)
		{
			return;
		}
		if (g_launcher.music_file.empty())
		{
			const void* bytes;
			DWORD size;
			wchar_t temp[MAX_PATH] = {};
			if (!GetResourceBytes(IDR_MUSIC, &bytes, &size) || !GetTempPathW(MAX_PATH, temp))
			{
				return;
			}
			const std::wstring file = std::wstring(temp) + L"MovieDuels-Launcher-music.mp3";
			const HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
			if (h == INVALID_HANDLE_VALUE)
			{
				return;
			}
			DWORD written = 0;
			const bool ok = WriteFile(h, bytes, size, &written, nullptr) && written == size;
			CloseHandle(h);
			if (!ok)
			{
				DeleteFileW(file.c_str());
				return;
			}
			g_launcher.music_file = file;
		}
		const std::wstring open = L"open \"" + g_launcher.music_file + L"\" type mpegvideo alias " + kMusicAlias;
		if (mciSendStringW(open.c_str(), nullptr, 0, nullptr) != 0)
		{
			return; // no MP3 playback on this PC: just no music
		}
		g_launcher.music_open = true;
		const std::wstring volume = std::wstring(L"setaudio ") + kMusicAlias + L" volume to " + std::to_wstring(kMusicVolume);
		mciSendStringW(volume.c_str(), nullptr, 0, nullptr);
		PlayMusicFromStart(hwnd);
	}

	void StopMusic()
	{
		if (g_launcher.music_open)
		{
			const std::wstring close = std::wstring(L"close ") + kMusicAlias;
			mciSendStringW(close.c_str(), nullptr, 0, nullptr);
			g_launcher.music_open = false;
		}
	}

	void DeleteMusicFile()
	{
		if (!g_launcher.music_file.empty())
		{
			DeleteFileW(g_launcher.music_file.c_str());
			g_launcher.music_file.clear();
		}
	}

	// ------------------------------------------------------------------------------------------
	// Settings (MovieDuels-Launcher.ini in Documents\My Games\MovieDuels-UD9, with the game's configs
	// and saves: a game installed under Program Files can't write next to its exe)
	// ------------------------------------------------------------------------------------------

	std::wstring DocumentsFolder()
	{
		wchar_t docs[MAX_PATH] = {};
		if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs)))
		{
			return L"";
		}
		return docs;
	}

	std::wstring IniPath()
	{
		static std::wstring path;
		if (path.empty())
		{
			const std::wstring docs = DocumentsFolder();
			if (!docs.empty())
			{
				std::wstring dir = docs + L"\\My Games";
				CreateDirectoryW(dir.c_str(), nullptr);
				const std::wstring old_ini = dir + L"\\" + kOldHomeFolder + L"\\" + kIniName;
				dir += L"\\" + std::wstring(kHomeFolder);
				CreateDirectoryW(dir.c_str(), nullptr);
				path = dir + L"\\" + kIniName;

				// the first time, take over the options from an ini next to the launcher (older launchers), else
				// from the WPF launcher's (in Update 9's My Games folder)
				const std::wstring launcher_ini = g_launcher.folder + L"\\" + kIniName;
				if (!PathFileExistsW(path.c_str()))
				{
					if (PathFileExistsW(launcher_ini.c_str()))
					{
						CopyFileW(launcher_ini.c_str(), path.c_str(), TRUE);
					}
					else if (PathFileExistsW(old_ini.c_str()))
					{
						CopyFileW(old_ini.c_str(), path.c_str(), TRUE);
					}
				}
			}
			else
			{
				path = g_launcher.folder + L"\\" + kIniName; // no Documents folder: next to the launcher
			}
		}
		return path;
	}

	void LoadGameConfig(); // below, with the other game config code

	void LoadSettings()
	{
		const std::wstring ini = IniPath();
		g_launcher.settings.rend2 = GetPrivateProfileIntW(L"Options", L"Rend2", 0, ini.c_str()) != 0;
		g_launcher.settings.windowed = GetPrivateProfileIntW(L"Options", L"Windowed", 0, ini.c_str()) != 0;
		g_launcher.settings.controller = GetPrivateProfileIntW(L"Options", L"Controller", 0, ini.c_str()) != 0;
		g_launcher.settings.music = GetPrivateProfileIntW(L"Options", L"Music", 1, ini.c_str()) != 0;
		LoadGameConfig(); // renderer, display, resolution: what the game's config says
	}

	void SaveSettings()
	{
		const std::wstring ini = IniPath();
		WritePrivateProfileStringW(L"Options", L"Rend2", g_launcher.settings.rend2 ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"Windowed", g_launcher.settings.windowed ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"Controller", g_launcher.settings.controller ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"Music", g_launcher.settings.music ? L"1" : L"0", ini.c_str());
	}

	// ------------------------------------------------------------------------------------------
	// The game's own configs: renderer, display and resolution are read from the config the game uses (the one in
	// Documents\My Games\MovieDuels-UD9\MD when it exists, else the one in the install's MD folder) and written back
	// into both the SP and the MP config: only those "seta" lines are replaced (or added), every other line stays as
	// it was.
	// ------------------------------------------------------------------------------------------

	std::wstring HomeModFolder()
	{
		const std::wstring docs = DocumentsFolder();
		return docs.empty() ? L"" : docs + L"\\My Games\\" + kHomeFolder + L"\\" + kModFolder;
	}

	// the config file of SP or MP: Documents first, then the install folder; when neither exists, the Documents one
	// (written on save; the game reads it when it starts)
	std::wstring ConfigPath(const bool multiplayer)
	{
		const wchar_t* const name = multiplayer ? kMPConfig : kSPConfig;
		const std::wstring home_dir = HomeModFolder();
		const std::wstring home = home_dir.empty() ? L"" : home_dir + L"\\" + name;
		const std::wstring install = g_launcher.folder + L"\\" + kModFolder + L"\\" + name;
		if (!home.empty() && PathFileExistsW(home.c_str()))
		{
			return home;
		}
		if (PathFileExistsW(install.c_str()))
		{
			return install;
		}
		return home.empty() ? install : home;
	}

	std::vector<std::string> ReadLines(const std::wstring& path)
	{
		std::vector<std::string> lines;
		FILE* f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
		{
			return lines;
		}
		std::string text;
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		{
			text.append(buf, n);
		}
		fclose(f);
		size_t start = 0;
		while (start < text.size())
		{
			size_t end = text.find('\n', start);
			if (end == std::string::npos)
			{
				end = text.size();
			}
			std::string line = text.substr(start, end - start);
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			lines.push_back(line);
			start = end + 1;
		}
		return lines;
	}

	// the cvar name of a "seta name value" / "set name value" line, else ""
	std::string CfgLineCvar(const std::string& line, std::string* value)
	{
		size_t i = line.find_first_not_of(" \t");
		if (i == std::string::npos)
		{
			return "";
		}
		size_t word_end = line.find_first_of(" \t", i);
		if (word_end == std::string::npos)
		{
			return "";
		}
		const std::string cmd = line.substr(i, word_end - i);
		if (_stricmp(cmd.c_str(), "seta") && _stricmp(cmd.c_str(), "set"))
		{
			return "";
		}
		i = line.find_first_not_of(" \t", word_end);
		if (i == std::string::npos)
		{
			return "";
		}
		word_end = line.find_first_of(" \t", i);
		const std::string name = line.substr(i, word_end == std::string::npos ? std::string::npos : word_end - i);
		if (value)
		{
			value->clear();
			if (word_end != std::string::npos)
			{
				std::string v = line.substr(line.find_first_not_of(" \t", word_end) == std::string::npos ? line.size()
					: line.find_first_not_of(" \t", word_end));
				if (v.size() >= 2 && v.front() == '"')
				{
					const size_t close = v.find('"', 1);
					v = v.substr(1, close == std::string::npos ? std::string::npos : close - 1);
				}
				*value = v;
			}
		}
		return name;
	}

	bool ReadCfgValue(const std::vector<std::string>& lines, const char* cvar, std::string& value)
	{
		bool found = false;
		for (const std::string& line : lines)
		{
			std::string v;
			if (!_stricmp(CfgLineCvar(line, &v).c_str(), cvar))
			{
				value = v; // the last one wins, as when the game execs it
				found = true;
			}
		}
		return found;
	}

	// replaces the lines of these cvars (or adds them at the end); the rest of the file is kept as it was
	void WriteCfgValues(const std::wstring& path, const std::vector<std::pair<std::string, std::string>>& values)
	{
		std::vector<std::string> lines = ReadLines(path);
		for (const auto& kv : values)
		{
			const std::string line = "seta " + kv.first + " \"" + kv.second + "\"";
			bool replaced = false;
			for (std::string& l : lines)
			{
				if (!_stricmp(CfgLineCvar(l, nullptr).c_str(), kv.first.c_str()))
				{
					l = line;
					replaced = true;
				}
			}
			if (!replaced)
			{
				// keep a last empty line (the file's final newline) last
				if (!lines.empty() && lines.back().empty())
				{
					lines.insert(lines.end() - 1, line);
				}
				else
				{
					lines.push_back(line);
				}
			}
		}
		const size_t slash = path.find_last_of(L"\\/");
		if (slash != std::wstring::npos)
		{
			SHCreateDirectoryExW(nullptr, path.substr(0, slash).c_str(), nullptr);
		}
		FILE* f = nullptr;
		if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
		{
			return; // read-only install folder or similar: the launcher still passes the values on the command line
		}
		for (size_t i = 0; i < lines.size(); i++)
		{
			fputs(lines[i].c_str(), f);
			if (i + 1 < lines.size() || !lines[i].empty())
			{
				fputs("\r\n", f);
			}
		}
		fclose(f);
	}

	// renderer, display and resolution from the game's config (MP's, else SP's); keeps the launcher's values when
	// there is no config yet
	void LoadGameConfig()
	{
		Settings& s = g_launcher.settings;
		std::vector<std::string> lines = ReadLines(ConfigPath(true));
		if (lines.empty())
		{
			lines = ReadLines(ConfigPath(false));
		}
		if (lines.empty())
		{
			return;
		}
		std::string v;
		if (ReadCfgValue(lines, "cl_renderer", v))
		{
			s.rend2 = v.find("rend2") != std::string::npos;
		}
		if (ReadCfgValue(lines, "r_fullscreen", v))
		{
			s.windowed = atoi(v.c_str()) == 0;
		}
		if (ReadCfgValue(lines, "r_mode", v))
		{
			const int mode = atoi(v.c_str());
			if (mode == -1)
			{
				std::string w, h;
				ReadCfgValue(lines, "r_customwidth", w);
				ReadCfgValue(lines, "r_customheight", h);
				s.resolution = { atoi(w.c_str()), atoi(h.c_str()) };
			}
			else if (mode >= 0 && mode < static_cast<int>(sizeof kRModes / sizeof kRModes[0]))
			{
				s.resolution = kRModes[mode];
			}
			else
			{
				s.resolution = { 0, 0 };
			}
			if (s.resolution.width <= 0 || s.resolution.height <= 0)
			{
				s.resolution = { 0, 0 };
			}
		}
	}

	const char* RendererName(const bool rend2, const bool multiplayer)
	{
		return multiplayer
			? (rend2 ? "MovieDuels-rdmp-rend2" : "MovieDuels-rdmp")
			: (rend2 ? "MovieDuels-rdsp-rend2" : "MovieDuels-rdsp");
	}

	// the cvars the OPTIONS page sets, for the SP or the MP config / command line
	std::vector<std::pair<std::string, std::string>> GameConfigValues(const bool multiplayer)
	{
		const Settings& s = g_launcher.settings;
		std::vector<std::pair<std::string, std::string>> values = {
			{ "cl_renderer", RendererName(s.rend2, multiplayer) },
			{ "com_rend2", s.rend2 ? "1" : "0" },
			{ "r_fullscreen", s.windowed ? "0" : "1" } };
		if (s.resolution.width > 0)
		{
			values.push_back({ "r_mode", "-1" });
			values.push_back({ "r_customwidth", std::to_string(s.resolution.width) });
			values.push_back({ "r_customheight", std::to_string(s.resolution.height) });
		}
		else
		{
			values.push_back({ "r_mode", "-2" });
		}
		return values;
	}

	void SaveGameConfig()
	{
		WriteCfgValues(ConfigPath(false), GameConfigValues(false));
		WriteCfgValues(ConfigPath(true), GameConfigValues(true));
	}

	// the next resolution of the list (from a resolution that is not in it: the first)
	void NextResolution()
	{
		Settings& s = g_launcher.settings;
		const int count = static_cast<int>(sizeof kResolutions / sizeof kResolutions[0]);
		int next = 0;
		for (int i = 0; i < count; i++)
		{
			if (kResolutions[i].width == s.resolution.width && kResolutions[i].height == s.resolution.height)
			{
				next = (i + 1) % count;
				break;
			}
		}
		s.resolution = kResolutions[next];
	}

	// ------------------------------------------------------------------------------------------
	// Which MovieDuels update the installed assets are, found the way the game finds it: the singleplayer main menu
	// (ui/main.menu) says it with "uiScript mdUpdate 9", which sets the game's read-only md_update cvar. A main menu
	// without that line is Update 9 (the game's default). The menu the game loads is the one in the last pk3 (in name
	// order) of the MD folder that has it.
	// ------------------------------------------------------------------------------------------

	// minizip opens files with fopen (the ANSI code page): a path with other characters goes as its short 8.3 name
	bool ToAnsi(const std::wstring& wide, std::string& ansi)
	{
		BOOL lossy = FALSE;
		const int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), -1, nullptr, 0, nullptr, &lossy);
		if (n <= 1 || lossy)
		{
			return false;
		}
		ansi.assign(static_cast<size_t>(n), '\0');
		if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), -1, &ansi[0], n, nullptr, &lossy) != n || lossy)
		{
			return false;
		}
		ansi.resize(static_cast<size_t>(n) - 1); // the terminator
		return true;
	}

	std::string ZipPath(const std::wstring& path)
	{
		std::string ansi;
		if (ToAnsi(path, ansi))
		{
			return ansi;
		}
		wchar_t short_path[MAX_PATH] = {};
		const DWORD len = GetShortPathNameW(path.c_str(), short_path, MAX_PATH);
		if (len > 0 && len < MAX_PATH && ToAnsi(short_path, ansi))
		{
			return ansi;
		}
		return "";
	}

	// The main menu text of a pk3, or false if it has none (or can't be read: not a zip, locked, ...).
	bool ReadMainMenu(const std::wstring& pk3, std::string& text)
	{
		const std::string path = ZipPath(pk3);
		if (path.empty())
		{
			return false;
		}
		const unzFile zip = unzOpen(path.c_str());
		if (!zip)
		{
			return false;
		}
		bool ok = false;
		// 2: case-insensitive; pk3s made on Windows can have backslashes in their names
		if (unzLocateFile(zip, "ui/main.menu", 2) == UNZ_OK || unzLocateFile(zip, "ui\\main.menu", 2) == UNZ_OK)
		{
			unz_file_info info = {};
			if (unzGetCurrentFileInfo(zip, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK
				&& info.uncompressed_size > 0 && info.uncompressed_size < 16 * 1024 * 1024
				&& unzOpenCurrentFile(zip) == UNZ_OK)
			{
				text.assign(static_cast<size_t>(info.uncompressed_size), '\0');
				const int read = unzReadCurrentFile(zip, &text[0], static_cast<unsigned>(text.size()));
				ok = read == static_cast<int>(text.size());
				unzCloseCurrentFile(zip);
			}
		}
		unzClose(zip);
		return ok;
	}

	int ParseUpdate(const std::string& menu)
	{
		static const std::regex md_update_line("uiScript\\s+\"?mdUpdate\"?\\s+\"?(\\d+)", std::regex::icase);
		size_t start = 0;
		while (start < menu.size())
		{
			size_t end = menu.find('\n', start);
			if (end == std::string::npos)
			{
				end = menu.size();
			}
			std::string code = menu.substr(start, end - start);
			const size_t comment = code.find("//");
			if (comment != std::string::npos)
			{
				code.resize(comment); // a commented-out line does not count
			}
			std::smatch m;
			if (std::regex_search(code, m, md_update_line))
			{
				const int update = atoi(m[1].str().c_str());
				if (update >= kDefaultUpdate)
				{
					return update;
				}
			}
			start = end + 1;
		}
		return kDefaultUpdate;
	}

	int DetectUpdate()
	{
		const std::wstring dir = g_launcher.folder + L"\\" + kModFolder;
		std::vector<std::wstring> pk3s;
		WIN32_FIND_DATAW fd;
		const HANDLE find = FindFirstFileW((dir + L"\\*.pk3").c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE)
		{
			return kDefaultUpdate;
		}
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !_wcsicmp(PathFindExtensionW(fd.cFileName), L".pk3"))
			{
				pk3s.emplace_back(fd.cFileName);
			}
		} while (FindNextFileW(find, &fd));
		FindClose(find);

		// the last pk3 wins, so look at them from the last one back (ordinal, ignoring case: as the game sorts them,
		// '_' after the letters)
		std::sort(pk3s.begin(), pk3s.end(), [](const std::wstring& a, const std::wstring& b)
		{
			return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_GREATER_THAN;
		});
		for (const std::wstring& name : pk3s)
		{
			std::string menu;
			if (ReadMainMenu(dir + L"\\" + name, menu))
			{
				return ParseUpdate(menu);
			}
		}
		return kDefaultUpdate;
	}

	// ------------------------------------------------------------------------------------------
	// Layout (units)
	// ------------------------------------------------------------------------------------------

	bool CardOpen()
	{
		return g_launcher.page != Page::Main;
	}

	// the bars on the info card: OPTIONS has five, MULTIPLAYER three
	int CardBarCount()
	{
		return g_launcher.page == Page::Options ? 5 : g_launcher.page == Page::Multiplayer ? 3 : 0;
	}

	Gdiplus::RectF CardBarRect(const int index)
	{
		return Gdiplus::RectF(kCardX, kCardBarY + index * kCardBarStep, kCardW, kCardBarH);
	}

	Gdiplus::RectF WindowButtonRect(const int hit)
	{
		const float x = hit == kHitMusic ? kMusicX : hit == kHitMinimise ? kMinimiseX : kCloseX;
		return Gdiplus::RectF(x, kWindowButtonY, kWindowButtonSize, kWindowButtonSize);
	}

	int HitTest(const Gdiplus::PointF& pt)
	{
		for (const int hit : { kHitMusic, kHitMinimise, kHitClose })
		{
			if (WindowButtonRect(hit).Contains(pt))
			{
				return hit;
			}
		}
		if (CardOpen())
		{
			for (int i = 0; i < CardBarCount(); i++)
			{
				if (CardBarRect(i).Contains(pt))
				{
					return kHitCard0 + i;
				}
			}
			return kHitNone; // the main page's buttons are covered by the info card
		}
		for (int i = 0; i < kMainButtonCount; i++)
		{
			if (kMainButtons[i].box.Contains(pt))
			{
				return i;
			}
		}
		return kHitNone;
	}

	// window pixels -> units
	Gdiplus::PointF ToUnits(const int x, const int y)
	{
		return Gdiplus::PointF(static_cast<float>(x) / g_launcher.scale, static_cast<float>(y) / g_launcher.scale);
	}

	int HitTestCursor()
	{
		POINT pt;
		if (!g_launcher.hwnd || !GetCursorPos(&pt) || !ScreenToClient(g_launcher.hwnd, &pt))
		{
			return kHitNone;
		}
		return HitTest(ToUnits(pt.x, pt.y));
	}

	// all buttons but the window buttons: they make the hover sound and show the hand
	bool IsMenuHit(const int hit)
	{
		return hit >= 0 && hit < kHitMusic;
	}

	std::wstring ResolutionText()
	{
		const Resolution& r = g_launcher.settings.resolution;
		return r.width > 0 ? std::to_wstring(r.width) + L"x" + std::to_wstring(r.height) : L"DESKTOP";
	}

	std::wstring CardBarLabel(const int index)
	{
		const Settings& s = g_launcher.settings;
		if (g_launcher.page == Page::Multiplayer)
		{
			switch (index)
			{
			case 0: return L"START MULTIPLAYER";
			case 1: return L"START SERVER";
			default: return L"BACK";
			}
		}
		switch (index)
		{
		case 0: return s.rend2 ? L"RENDERER: REND2" : L"RENDERER: VANILLA";
		case 1: return s.windowed ? L"DISPLAY: WINDOWED" : L"DISPLAY: FULLSCREEN";
		case 2: return L"RESOLUTION: " + ResolutionText();
		case 3: return s.controller ? L"CONTROLLER: ON" : L"CONTROLLER: OFF";
		default: return L"BACK";
		}
	}

	// ------------------------------------------------------------------------------------------
	// Art at window size
	// ------------------------------------------------------------------------------------------

	std::unique_ptr<Gdiplus::Bitmap> NewSurface(const int width, const int height)
	{
		return std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
	}

	void SetQuality(Gdiplus::Graphics& g)
	{
		g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
		g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
	}

	// Draws a whole image stretched over dest, at an opacity, optionally as black only (its alpha as a mask).
	void DrawStretched(Gdiplus::Graphics& g, Gdiplus::Bitmap* image, const Gdiplus::RectF& dest, const float opacity = 1.0f,
		const bool black = false)
	{
		if (!image)
		{
			return;
		}
		const float c = black ? 0.0f : 1.0f;
		Gdiplus::ColorMatrix cm = {
			c, 0, 0, 0, 0,
			0, c, 0, 0, 0,
			0, 0, c, 0, 0,
			0, 0, 0, opacity, 0,
			0, 0, 0, 0, 1 };
		Gdiplus::ImageAttributes attributes;
		attributes.SetColorMatrix(&cm);
		attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY); // no dark seams at the edges when scaling
		g.DrawImage(image, dest, 0.0f, 0.0f, static_cast<Gdiplus::REAL>(image->GetWidth()),
			static_cast<Gdiplus::REAL>(image->GetHeight()), Gdiplus::UnitPixel, &attributes);
	}

	// Draws a window-size (or glow) bitmap 1:1 at a pixel position, at an opacity.
	void DrawPixels(Gdiplus::Graphics& g, Gdiplus::Bitmap* image, const int x, const int y, const float opacity = 1.0f)
	{
		if (!image || opacity <= 0.0f)
		{
			return;
		}
		Gdiplus::Matrix units;
		g.GetTransform(&units);
		g.ResetTransform();
		const INT w = static_cast<INT>(image->GetWidth());
		const INT h = static_cast<INT>(image->GetHeight());
		const Gdiplus::InterpolationMode mode = g.GetInterpolationMode();
		g.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
		if (opacity >= 1.0f)
		{
			g.DrawImage(image, Gdiplus::Rect(x, y, w, h), 0, 0, w, h, Gdiplus::UnitPixel);
		}
		else
		{
			Gdiplus::ColorMatrix cm = {
				1, 0, 0, 0, 0,
				0, 1, 0, 0, 0,
				0, 0, 1, 0, 0,
				0, 0, 0, opacity, 0,
				0, 0, 0, 0, 1 };
			Gdiplus::ImageAttributes attributes;
			attributes.SetColorMatrix(&cm);
			g.DrawImage(image, Gdiplus::Rect(x, y, w, h), 0, 0, w, h, Gdiplus::UnitPixel, &attributes);
		}
		g.SetInterpolationMode(mode);
		g.SetTransform(&units);
	}

	// One pass of a box blur along rows (step 1) or columns (step = width); outside the image counts as 0.
	void BoxBlur(const std::vector<float>& src, std::vector<float>& dst, const int width, const int height,
		const int radius, const bool vertical)
	{
		const int lines = vertical ? width : height;
		const int length = vertical ? height : width;
		const size_t step = vertical ? static_cast<size_t>(width) : 1;
		const float norm = 1.0f / static_cast<float>(radius * 2 + 1);
		for (int line = 0; line < lines; line++)
		{
			const size_t first = vertical ? static_cast<size_t>(line) : static_cast<size_t>(line) * width;
			float sum = 0.0f;
			for (int i = 0; i <= radius && i < length; i++)
			{
				sum += src[first + i * step];
			}
			for (int i = 0; i < length; i++)
			{
				dst[first + i * step] = sum * norm;
				const int add = i + radius + 1;
				const int remove = i - radius;
				if (add < length)
				{
					sum += src[first + add * step];
				}
				if (remove >= 0)
				{
					sum -= src[first + remove * step];
				}
			}
		}
	}

	// The gold glow of a shape (its alpha blurred, like WPF's DropShadowEffect with no offset), same size as shape.
	std::unique_ptr<Gdiplus::Bitmap> MakeGlow(Gdiplus::Bitmap& shape)
	{
		const int w = static_cast<int>(shape.GetWidth());
		const int h = static_cast<int>(shape.GetHeight());
		if (w <= 0 || h <= 0)
		{
			return nullptr;
		}
		const size_t count = static_cast<size_t>(w) * h;
		std::vector<float> alpha(count), temp(count);

		Gdiplus::Rect rect(0, 0, w, h);
		Gdiplus::BitmapData data = {};
		if (shape.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) != Gdiplus::Ok)
		{
			return nullptr;
		}
		if (!data.Scan0)
		{
			shape.UnlockBits(&data);
			return nullptr;
		}
		for (int y = 0; y < h; y++)
		{
			const BYTE* row = static_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
			for (int x = 0; x < w; x++)
			{
				alpha[static_cast<size_t>(y) * w + x] = row[x * 4 + 3] / 255.0f;
			}
		}
		shape.UnlockBits(&data);

		// three box blurs make a gaussian; WPF's blur radius is about three standard deviations
		const int radius = (std::max)(1, static_cast<int>(kGlowBlur * g_launcher.scale / 3.0f + 0.5f));
		for (int pass = 0; pass < 3; pass++)
		{
			BoxBlur(alpha, temp, w, h, radius, false);
			BoxBlur(temp, alpha, w, h, radius, true);
		}

		std::unique_ptr<Gdiplus::Bitmap> glow = NewSurface(w, h);
		if (!glow || glow->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &data) != Gdiplus::Ok)
		{
			return nullptr;
		}
		if (!data.Scan0)
		{
			glow->UnlockBits(&data);
			return nullptr;
		}
		for (int y = 0; y < h; y++)
		{
			BYTE* row = static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
			for (int x = 0; x < w; x++)
			{
				const float a = (std::min)(1.0f, alpha[static_cast<size_t>(y) * w + x] * kGlowStrength);
				row[x * 4 + 0] = static_cast<BYTE>(kGlowB * a + 0.5f); // premultiplied BGRA
				row[x * 4 + 1] = static_cast<BYTE>(kGlowG * a + 0.5f);
				row[x * 4 + 2] = static_cast<BYTE>(kGlowR * a + 0.5f);
				row[x * 4 + 3] = static_cast<BYTE>(255.0f * a + 0.5f);
			}
		}
		glow->UnlockBits(&data);
		return glow;
	}

	// the icon's square in pixels (centred in its 32 x 32 button; 24 x 24, or 12% bigger under the mouse)
	Gdiplus::Rect IconRect(const int button, const bool big)
	{
		const Gdiplus::RectF& box = kMainButtons[button].box;
		const float size = (big ? 24.0f * 1.12f : 24.0f) * g_launcher.scale;
		const int px = static_cast<int>(size + 0.5f);
		const float cx = (box.X + box.Width / 2) * g_launcher.scale;
		const float cy = (box.Y + box.Height / 2) * g_launcher.scale;
		const float half = static_cast<float>(px) / 2.0f;
		return Gdiplus::Rect(static_cast<int>(std::floor(cx - half + 0.5f)), static_cast<int>(std::floor(cy - half + 0.5f)), px, px);
	}

	// Builds the art at the window's size, once (the window's scale does not change while it is open).
	void BuildArt()
	{
		const int w = g_launcher.width;
		const int h = g_launcher.height;
		const float s = g_launcher.scale;
		const Gdiplus::RectF full(0.0f, 0.0f, static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(h));

		std::unique_ptr<Gdiplus::Bitmap> background = LoadImageResource(IDR_IMG_BACKGROUND_PANEL);

		// base: the black backing (the team's Background Panel is glass, 81% opaque: the backing in the same rounded
		// shape, just inside the gold outline, makes the window solid), the background, the play background
		g_launcher.base = NewSurface(w, h);
		{
			Gdiplus::Graphics g(g_launcher.base.get());
			SetQuality(g);
			g.Clear(Gdiplus::Color(0, 0, 0, 0));
			g.ScaleTransform(s, s);
			Gdiplus::GraphicsPath backing;
			const Gdiplus::RectF r(2.3f, 4.6f, kUnitW - 2.3f - 1.2f, kUnitH - 4.6f - 4.0f);
			const float d = 34.5f * 2;
			backing.AddArc(r.X, r.Y, d, d, 180, 90);
			backing.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
			backing.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
			backing.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
			backing.CloseFigure();
			const Gdiplus::SolidBrush black(Gdiplus::Color(255, 0, 0, 0));
			g.FillPath(&black, &backing);
			g.ResetTransform();
			DrawStretched(g, background.get(), full);
			const std::unique_ptr<Gdiplus::Bitmap> play_background = LoadImageResource(IDR_IMG_PLAY_BACKGROUND);
			DrawStretched(g, play_background.get(), full);
		}

		const std::unique_ptr<Gdiplus::Bitmap> single = LoadImageResource(IDR_IMG_SINGLEPLAYER_GLOW);
		g_launcher.glow_single = NewSurface(w, h);
		{
			Gdiplus::Graphics g(g_launcher.glow_single.get());
			SetQuality(g);
			g.Clear(Gdiplus::Color(0, 0, 0, 0));
			DrawStretched(g, single.get(), full);
		}
		const std::unique_ptr<Gdiplus::Bitmap> multi = LoadImageResource(IDR_IMG_MULTIPLAYER_GLOW);
		g_launcher.glow_multi = NewSurface(w, h);
		{
			Gdiplus::Graphics g(g_launcher.glow_multi.get());
			SetQuality(g);
			g.Clear(Gdiplus::Color(0, 0, 0, 0));
			DrawStretched(g, multi.get(), full);
		}

		g_launcher.top = NewSurface(w, h);
		{
			Gdiplus::Graphics g(g_launcher.top.get());
			SetQuality(g);
			g.Clear(Gdiplus::Color(0, 0, 0, 0));
			const std::unique_ptr<Gdiplus::Bitmap> play_panel = LoadImageResource(IDR_IMG_PLAY_PANEL);
			DrawStretched(g, play_panel.get(), full);
			const std::unique_ptr<Gdiplus::Bitmap> logo_glow = LoadImageResource(IDR_IMG_LOGO_GLOW);
			DrawStretched(g, logo_glow.get(), full);
		}

		// the info card's pages: black at 43% in the background's shape dims the main page, the info panel on top
		g_launcher.card = NewSurface(w, h);
		{
			Gdiplus::Graphics g(g_launcher.card.get());
			SetQuality(g);
			g.Clear(Gdiplus::Color(0, 0, 0, 0));
			DrawStretched(g, background.get(), full, 0.43f, true);
			const std::unique_ptr<Gdiplus::Bitmap> info_panel = LoadImageResource(IDR_IMG_INFO_PANEL);
			DrawStretched(g, info_panel.get(), full);
		}

		// the icons at their two sizes, and the glow of the big one
		for (int i = 0; i < kMainButtonCount; i++)
		{
			const int index = kMainButtons[i].icon;
			if (index < 0)
			{
				continue;
			}
			const std::unique_ptr<Gdiplus::Bitmap> image = LoadImageResource(kIconResources[index]);
			for (const bool big : { false, true })
			{
				const Gdiplus::Rect r = IconRect(i, big);
				std::unique_ptr<Gdiplus::Bitmap> icon = NewSurface(r.Width, r.Height);
				{
					Gdiplus::Graphics g(icon.get());
					SetQuality(g);
					g.Clear(Gdiplus::Color(0, 0, 0, 0));
					DrawStretched(g, image.get(), Gdiplus::RectF(0.0f, 0.0f, static_cast<Gdiplus::REAL>(r.Width), static_cast<Gdiplus::REAL>(r.Height)));
				}
				(big ? g_launcher.icon_hover : g_launcher.icon)[index] = std::move(icon);
			}
			const Gdiplus::Rect r = IconRect(i, true);
			const int margin = static_cast<int>(std::ceil(kGlowBlur * s));
			Gdiplus::Bitmap shape(r.Width + margin * 2, r.Height + margin * 2, PixelFormat32bppPARGB);
			{
				Gdiplus::Graphics g(&shape);
				g.Clear(Gdiplus::Color(0, 0, 0, 0));
				DrawPixels(g, g_launcher.icon_hover[index].get(), margin, margin);
			}
			g_launcher.icon_glow[index].bitmap = MakeGlow(shape);
			g_launcher.icon_glow[index].x = r.X - margin;
			g_launcher.icon_glow[index].y = r.Y - margin;
		}
	}

	void FreeArt()
	{
		g_launcher.base.reset();
		g_launcher.glow_single.reset();
		g_launcher.glow_multi.reset();
		g_launcher.top.reset();
		g_launcher.card.reset();
		for (int i = 0; i < kIconCount; i++)
		{
			g_launcher.icon[i].reset();
			g_launcher.icon_hover[i].reset();
			g_launcher.icon_glow[i].bitmap.reset();
		}
		g_launcher.text_glows.clear();
		g_launcher.label_family.reset();
	}

	// ------------------------------------------------------------------------------------------
	// Drawing (in units: the graphics carry the window's scale)
	// ------------------------------------------------------------------------------------------

	const Gdiplus::FontFamily* LabelFamily()
	{
		if (!g_launcher.label_family)
		{
			// the WPF launcher's LabelFont: Palatino Linotype, Georgia, Times New Roman
			for (const wchar_t* name : { L"Palatino Linotype", L"Georgia", L"Times New Roman" })
			{
				auto family = std::make_unique<Gdiplus::FontFamily>(name);
				if (family->IsAvailable())
				{
					g_launcher.label_family = std::move(family);
					break;
				}
			}
		}
		return g_launcher.label_family ? g_launcher.label_family.get() : Gdiplus::FontFamily::GenericSerif();
	}

	// The glow of a lit label (made once per text and place, then kept).
	Glow* TextGlow(const Gdiplus::GraphicsPath& path, const std::wstring& key, const Gdiplus::RectF& box)
	{
		const auto found = g_launcher.text_glows.find(key);
		if (found != g_launcher.text_glows.end())
		{
			return &found->second;
		}
		const float s = g_launcher.scale;
		const float margin = kGlowBlur;
		const int x = static_cast<int>(std::floor((box.X - margin) * s));
		const int y = static_cast<int>(std::floor((box.Y - margin) * s));
		const int w = static_cast<int>(std::ceil((box.Width + margin * 2) * s)) + 1;
		const int h = static_cast<int>(std::ceil((box.Height + margin * 2) * s)) + 1;
		Gdiplus::Bitmap shape(w, h, PixelFormat32bppPARGB);
		{
			Gdiplus::Graphics g(&shape);
			SetQuality(g);
			g.Clear(Gdiplus::Color(0, 0, 0, 0));
			g.TranslateTransform(static_cast<Gdiplus::REAL>(-x), static_cast<Gdiplus::REAL>(-y));
			g.ScaleTransform(s, s);
			const Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
			g.FillPath(&white, &path);
		}
		Glow& glow = g_launcher.text_glows[key];
		glow.bitmap = MakeGlow(shape);
		glow.x = x;
		glow.y = y;
		return &glow;
	}

	// A label centred in box (WPF ContentPresenter, centred): white with a soft shadow, or gold with a gold glow when
	// lit; pressed moves it down a unit.
	void DrawLabel(Gdiplus::Graphics& g, const std::wstring& text, const Gdiplus::RectF& box, const float size,
		const bool lit, const bool down, const bool centre_vertically = true)
	{
		Gdiplus::StringFormat fmt;
		fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
		fmt.SetLineAlignment(centre_vertically ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
		Gdiplus::GraphicsPath path;
		path.AddString(text.c_str(), -1, LabelFamily(), Gdiplus::FontStyleRegular, size, box, &fmt);

		const float shift = down ? 1.0f : 0.0f;
		Gdiplus::Matrix offset;
		offset.Translate(0.0f, shift);
		path.Transform(&offset);

		if (lit)
		{
			const std::wstring key = text + L"|" + std::to_wstring(static_cast<int>(box.X)) + L"," + std::to_wstring(static_cast<int>(box.Y))
				+ L"," + std::to_wstring(static_cast<int>(size)) + (down ? L"d" : L"");
			const Glow* glow = TextGlow(path, key, box);
			if (glow && glow->bitmap)
			{
				DrawPixels(g, glow->bitmap.get(), glow->x, glow->y);
			}
		}
		else
		{
			// WPF DropShadowEffect: black, 1.5 units down-right, blur 2, 70%
			Gdiplus::Matrix shadow_offset;
			shadow_offset.Translate(1.06f, 1.06f);
			std::unique_ptr<Gdiplus::GraphicsPath> shadow(path.Clone());
			shadow->Transform(&shadow_offset);
			const Gdiplus::SolidBrush shadow_brush(Gdiplus::Color(140, 0, 0, 0));
			g.FillPath(&shadow_brush, shadow.get());
		}
		const Gdiplus::SolidBrush fill(lit ? kGoldText : kWhiteText);
		g.FillPath(&fill, &path);
	}

	void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, const Gdiplus::REAL radius)
	{
		const Gdiplus::REAL d = radius * 2;
		path.AddArc(r.X, r.Y, d, d, 180, 90);
		path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
		path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
		path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
		path.CloseFigure();
	}

	// A bar on the info card (WPF BarButton): dark fill, grey edge; gold edge and lit label under the mouse.
	void DrawBar(Gdiplus::Graphics& g, const Gdiplus::RectF& r, const std::wstring& label, const bool lit, const bool down)
	{
		Gdiplus::GraphicsPath fill_path;
		AddRoundRect(fill_path, r, 9.0f);
		const Gdiplus::SolidBrush fill(lit ? Gdiplus::Color(235, 48, 28, 40) : Gdiplus::Color(235, 20, 10, 20));
		g.FillPath(&fill, &fill_path);
		Gdiplus::GraphicsPath edge_path; // the border is inside the bar
		AddRoundRect(edge_path, Gdiplus::RectF(r.X + 0.75f, r.Y + 0.75f, r.Width - 1.5f, r.Height - 1.5f), 8.25f);
		const Gdiplus::Pen edge(lit ? Gdiplus::Color(255, 220, 160, 60) : Gdiplus::Color(255, 138, 138, 138), 1.5f);
		g.DrawPath(&edge, &edge_path);
		DrawLabel(g, label, r, 19.0f, lit, down);
	}

	void DrawMainPage(Gdiplus::Graphics& g)
	{
		for (int i = 0; i < kMainButtonCount; i++)
		{
			const MainButton& b = kMainButtons[i];
			const bool lit = !CardOpen() && g_launcher.hover == i;
			const bool down = lit && g_launcher.pressed == i;
			if (b.icon < 0)
			{
				DrawLabel(g, b.text, b.box, b.size, lit, down);
				continue;
			}
			// icons: 82% at rest; under the mouse full, 12% bigger, with the gold glow; pressed back to their size
			if (lit)
			{
				const Glow& glow = g_launcher.icon_glow[b.icon];
				DrawPixels(g, glow.bitmap.get(), glow.x, glow.y);
			}
			const bool big = lit && !down;
			const Gdiplus::Rect r = IconRect(i, big);
			DrawPixels(g, (big ? g_launcher.icon_hover : g_launcher.icon)[b.icon].get(), r.X, r.Y, lit ? 1.0f : 0.82f);
		}

		// the update of the game's assets, and the launcher's build (the game's win32/AutoVersion.h)
		Gdiplus::FontFamily ui_family(L"Segoe UI");
		const Gdiplus::Font version_font(ui_family.IsAvailable() ? &ui_family : Gdiplus::FontFamily::GenericSansSerif(), 11.0f,
			Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		const Gdiplus::SolidBrush grey(kVersionGrey);
		Gdiplus::StringFormat left(Gdiplus::StringFormat::GenericTypographic());
		const std::wstring version = L"Update " + std::to_wstring(g_launcher.md_update);
		g.DrawString(version.c_str(), -1, &version_font, Gdiplus::RectF(30.0f, 578.0f, 400.0f, 20.0f), &left, &grey);
		Gdiplus::StringFormat right(Gdiplus::StringFormat::GenericTypographic());
		right.SetAlignment(Gdiplus::StringAlignmentFar);
		g.DrawString(L"" VERSION_STRING, -1, &version_font, Gdiplus::RectF(kUnitW - 30.0f - 400.0f, 578.0f, 400.0f, 20.0f), &right, &grey);
	}

	void DrawCard(Gdiplus::Graphics& g)
	{
		DrawPixels(g, g_launcher.card.get(), 0, 0);
		const wchar_t* title = g_launcher.page == Page::Options ? L"OPTIONS" : L"MULTIPLAYER";
		DrawLabel(g, title, Gdiplus::RectF(kCardX, kCardTitleY, kCardW, 40.0f), 28.0f, true, false, false);
		for (int i = 0; i < CardBarCount(); i++)
		{
			const bool lit = g_launcher.hover == kHitCard0 + i;
			DrawBar(g, CardBarRect(i), CardBarLabel(i), lit, lit && g_launcher.pressed == kHitCard0 + i);
		}
	}

	void DrawWindowButtons(Gdiplus::Graphics& g)
	{
		const Gdiplus::SolidBrush back(Gdiplus::Color(150, 0, 0, 0));
		for (const int hit : { kHitMusic, kHitMinimise, kHitClose })
		{
			const Gdiplus::RectF r = WindowButtonRect(hit);
			const Gdiplus::Color colour = g_launcher.hover == hit ? kGoldBright : kGold;
			const float x = r.X;
			const float y = r.Y;
			// the ring is inside the button (WPF Ellipse, stroke 2)
			Gdiplus::Pen ring(colour, 2.0f);
			g.FillEllipse(&back, x + 1.0f, y + 1.0f, r.Width - 2.0f, r.Height - 2.0f);
			g.DrawEllipse(&ring, x + 1.0f, y + 1.0f, r.Width - 2.0f, r.Height - 2.0f);

			Gdiplus::Pen mark(colour, 1.5f);
			mark.SetLineJoin(Gdiplus::LineJoinRound);
			Gdiplus::Pen thick(colour, 2.0f);
			if (hit == kHitMusic)
			{
				// speaker: waves when the music is on, a cross when it is off
				const Gdiplus::PointF speaker[] = {
					{ x + 5.5f, y + 9.0f }, { x + 8.5f, y + 9.0f }, { x + 12.0f, y + 6.0f },
					{ x + 12.0f, y + 16.0f }, { x + 8.5f, y + 13.0f }, { x + 5.5f, y + 13.0f } };
				g.DrawPolygon(&mark, speaker, 6);
				if (g_launcher.settings.music)
				{
					// the WPF arcs (14,8.5)-(14,13.5) radius 3.5 and (15.5,6)-(15.5,16) radius 6.5
					g.DrawArc(&mark, x + 8.051f, y + 7.5f, 7.0f, 7.0f, -45.58f, 91.17f);
					g.DrawArc(&mark, x + 4.847f, y + 4.5f, 13.0f, 13.0f, -50.28f, 100.56f);
				}
				else
				{
					g.DrawLine(&mark, x + 14.0f, y + 9.0f, x + 18.0f, y + 13.0f);
					g.DrawLine(&mark, x + 18.0f, y + 9.0f, x + 14.0f, y + 13.0f);
				}
			}
			else if (hit == kHitMinimise)
			{
				g.DrawLine(&thick, x + 6.0f, y + 11.0f, x + 16.0f, y + 11.0f);
			}
			else
			{
				g.DrawLine(&thick, x + 6.5f, y + 6.5f, x + 15.5f, y + 15.5f);
				g.DrawLine(&thick, x + 15.5f, y + 6.5f, x + 6.5f, y + 15.5f);
			}
		}
	}

	// Renders the whole window into the DIB and hands it to Windows (layered window, per-pixel alpha).
	void Render()
	{
		if (!g_launcher.bits)
		{
			return;
		}
		{
			Gdiplus::Bitmap canvas(g_launcher.width, g_launcher.height, g_launcher.width * 4, PixelFormat32bppPARGB,
				static_cast<BYTE*>(g_launcher.bits));
			Gdiplus::Graphics g(&canvas);
			SetQuality(g);
			g.Clear(Gdiplus::Color(0, 0, 0, 0));

			DrawPixels(g, g_launcher.base.get(), 0, 0);
			DrawPixels(g, g_launcher.glow_single.get(), 0, 0, g_launcher.single_glow);
			DrawPixels(g, g_launcher.glow_multi.get(), 0, 0, g_launcher.multi_glow);
			DrawPixels(g, g_launcher.top.get(), 0, 0);

			g.ScaleTransform(g_launcher.scale, g_launcher.scale);
			DrawMainPage(g);
			if (CardOpen())
			{
				DrawCard(g);
			}
			DrawWindowButtons(g); // on top of all the pages
		}

		const HDC screen = GetDC(nullptr);
		POINT src = { 0, 0 };
		SIZE size = { g_launcher.width, g_launcher.height };
		BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		UpdateLayeredWindow(g_launcher.hwnd, screen, nullptr, &size, g_launcher.mem_dc, &src, 0, &blend, ULW_ALPHA);
		ReleaseDC(nullptr, screen);
	}

	bool CreateSurface()
	{
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = g_launcher.width;
		bi.bmiHeader.biHeight = -g_launcher.height; // top-down
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		const HDC screen = GetDC(nullptr);
		g_launcher.mem_dc = CreateCompatibleDC(screen);
		g_launcher.dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &g_launcher.bits, nullptr, 0);
		ReleaseDC(nullptr, screen);
		if (!g_launcher.mem_dc || !g_launcher.dib)
		{
			return false;
		}
		g_launcher.old_bitmap = SelectObject(g_launcher.mem_dc, g_launcher.dib);
		return true;
	}

	void FreeSurface()
	{
		if (g_launcher.mem_dc)
		{
			SelectObject(g_launcher.mem_dc, g_launcher.old_bitmap);
			DeleteDC(g_launcher.mem_dc);
			g_launcher.mem_dc = nullptr;
		}
		if (g_launcher.dib)
		{
			DeleteObject(g_launcher.dib);
			g_launcher.dib = nullptr;
		}
		g_launcher.bits = nullptr;
	}

	// The window's size: the WPF launcher's units at the screen's scale (100% = 96 dpi), made smaller if that does
	// not fit the screen.
	void ChooseScale()
	{
		const HDC screen = GetDC(nullptr);
		float scale = screen ? static_cast<float>(GetDeviceCaps(screen, LOGPIXELSX)) / 96.0f : 1.0f;
		if (screen)
		{
			ReleaseDC(nullptr, screen);
		}
		RECT work = {};
		if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
		{
			const float fit = (std::min)(static_cast<float>(work.right - work.left) / kUnitW,
				static_cast<float>(work.bottom - work.top) / kUnitH);
			scale = (std::min)(scale, fit);
		}
		g_launcher.scale = (std::max)(0.5f, scale);
		g_launcher.width = static_cast<int>(kUnitW * g_launcher.scale + 0.5f);
		g_launcher.height = static_cast<int>(kUnitH * g_launcher.scale + 0.5f);
	}

	// ------------------------------------------------------------------------------------------
	// The mode glow layers
	// ------------------------------------------------------------------------------------------

	// Hovering a mode lights up its side of the art: its glow layer fades in (only with no info card open).
	void UpdateGlow()
	{
		const float want_single = !CardOpen() && g_launcher.hover == kHitSingle ? 1.0f : 0.0f;
		const float want_multi = !CardOpen() && g_launcher.hover == kHitMulti ? 1.0f : 0.0f;
		const bool done = g_launcher.single_glow == want_single && g_launcher.multi_glow == want_multi;
		if (!done && !g_launcher.fading)
		{
			g_launcher.fading = true;
			g_launcher.fade_tick = GetTickCount64();
			SetTimer(g_launcher.hwnd, kFadeTimer, 15, nullptr);
		}
	}

	// moves each layer towards its wanted opacity; false when both are there
	bool StepGlow()
	{
		const ULONGLONG now = GetTickCount64();
		const float step = static_cast<float>(now - g_launcher.fade_tick) / kFadeMs;
		g_launcher.fade_tick = now;
		const float want_single = !CardOpen() && g_launcher.hover == kHitSingle ? 1.0f : 0.0f;
		const float want_multi = !CardOpen() && g_launcher.hover == kHitMulti ? 1.0f : 0.0f;
		const auto approach = [step](float& value, const float target)
		{
			value = value < target ? (std::min)(target, value + step) : (std::max)(target, value - step);
		};
		approach(g_launcher.single_glow, want_single);
		approach(g_launcher.multi_glow, want_multi);
		return g_launcher.single_glow != want_single || g_launcher.multi_glow != want_multi;
	}

	// ------------------------------------------------------------------------------------------
	// Actions
	// ------------------------------------------------------------------------------------------

	bool StartGame(const HWND hwnd, const bool multiplayer)
	{
		const Settings& s = g_launcher.settings;
		const std::wstring exe = g_launcher.folder + L"\\" + (multiplayer ? kMPExe : kSPExe);
		if (!PathFileExistsW(exe.c_str()))
		{
			const std::wstring msg = L"Could not find " + exe + L"\n\nThe launcher must be in the MovieDuels game folder.";
			MessageBoxW(hwnd, msg.c_str(), L"MovieDuels", MB_OK | MB_ICONERROR);
			return false;
		}

		// "+set" on the command line overrides the values in the configs (they are the configs' own values unless the
		// OPTIONS page changed them, and then it wrote them into the configs too)
		std::wstring cmd = L"\"" + exe + L"\"";
		for (const auto& kv : GameConfigValues(multiplayer))
		{
			cmd += L" +set " + std::wstring(kv.first.begin(), kv.first.end()) + L" " + std::wstring(kv.second.begin(), kv.second.end());
		}
		cmd += s.controller ? L" +set in_joystick 1" : L" +set in_joystick 0";

		SetEnvironmentVariableW(kLauncherEnv, kLauncherToken); // the game inherits it: lets a release build start

		STARTUPINFOW si = { sizeof(si) };
		PROCESS_INFORMATION pi = {};
		std::wstring cmd_buffer = cmd; // CreateProcessW may write into it
		if (!CreateProcessW(exe.c_str(), &cmd_buffer[0], nullptr, nullptr, FALSE, 0, nullptr, g_launcher.folder.c_str(), &si, &pi))
		{
			MessageBoxW(hwnd, (L"Could not start " + exe).c_str(), L"MovieDuels", MB_OK | MB_ICONERROR);
			return false;
		}
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
	}

	// Runs the FFA dedicated server batch file from the MD folder in a console window of its own. The batch file
	// restarts the server when it crashes; closing its window stops it.
	bool StartServer(const HWND hwnd)
	{
		const std::wstring mod = g_launcher.folder + L"\\" + kModFolder;
		const std::wstring bat = mod + L"\\" + kServerBat;
		if (!PathFileExistsW(bat.c_str()))
		{
			const std::wstring msg = L"Could not find " + bat + L"\n\nThe launcher must be in the MovieDuels game folder.";
			MessageBoxW(hwnd, msg.c_str(), L"MovieDuels", MB_OK | MB_ICONERROR);
			return false;
		}

		wchar_t comspec[MAX_PATH] = {};
		const DWORD len = GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
		if (len == 0 || len >= MAX_PATH)
		{
			comspec[0] = L'\0';
			if (GetSystemDirectoryW(comspec, MAX_PATH) == 0)
			{
				return false;
			}
			wcscat_s(comspec, L"\\cmd.exe");
		}
		// cmd /c ""<bat>"": the outer quotes are cmd's, the inner ones keep the spaces in the path
		std::wstring cmd = L"\"" + std::wstring(comspec) + L"\" /c \"\"" + bat + L"\"\"";

		SetEnvironmentVariableW(kLauncherEnv, kLauncherToken); // the server inherits it, as the game does

		STARTUPINFOW si = { sizeof(si) };
		PROCESS_INFORMATION pi = {};
		if (!CreateProcessW(comspec, &cmd[0], nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, mod.c_str(), &si, &pi))
		{
			MessageBoxW(hwnd, (L"Could not start " + bat).c_str(), L"MovieDuels", MB_OK | MB_ICONERROR);
			return false;
		}
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
	}

	void OpenLink(const HWND hwnd, const wchar_t* url)
	{
		PlaySfx(IDR_SND_CLICK);
		ShellExecuteW(hwnd, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
	}

	void SetPage(const Page page)
	{
		g_launcher.page = page;
		g_launcher.hover = HitTestCursor(); // what is under the mouse now (no sound: it did not move)
		UpdateGlow();
	}

	// SINGLEPLAYER MODE / START MULTIPLAYER: start the game and close
	void Play(const HWND hwnd, const bool multiplayer)
	{
		PlaySfx(IDR_SND_CLICK, true); // the launcher closes right after
		if (StartGame(hwnd, multiplayer))
		{
			DestroyWindow(hwnd);
		}
	}

	// an option changed: remember it (renderer, display, resolution also go into the game's configs)
	void OptionChanged(const bool game_config)
	{
		PlaySfx(IDR_SND_CLICK);
		SaveSettings();
		if (game_config)
		{
			SaveGameConfig();
		}
	}

	void OnCardClick(const HWND hwnd, const int index)
	{
		if (g_launcher.page == Page::Multiplayer)
		{
			switch (index)
			{
			case 0:
				Play(hwnd, true);
				return;
			case 1:
				PlaySfx(IDR_SND_CLICK);
				StartServer(hwnd); // the launcher stays open, to start the game and join
				break;
			default:
				PlaySfx(IDR_SND_BACK);
				SetPage(Page::Main);
				break;
			}
			return;
		}
		Settings& s = g_launcher.settings;
		switch (index)
		{
		case 0:
			s.rend2 = !s.rend2;
			OptionChanged(true);
			break;
		case 1:
			s.windowed = !s.windowed;
			OptionChanged(true);
			break;
		case 2:
			NextResolution();
			OptionChanged(true);
			break;
		case 3:
			s.controller = !s.controller;
			OptionChanged(false);
			break;
		default:
			PlaySfx(IDR_SND_BACK);
			SetPage(Page::Main);
			break;
		}
	}

	void OnClick(const HWND hwnd, const int hit)
	{
		if (hit >= kHitCard0 && hit < kHitCard0 + CardBarCount())
		{
			OnCardClick(hwnd, hit - kHitCard0);
		}
		else if (hit == kHitSingle)
		{
			Play(hwnd, false);
			return;
		}
		else if (hit == kHitMulti)
		{
			PlaySfx(IDR_SND_CLICK);
			if (g_launcher.md_update < 9)
			{
				// multiplayer is played with Update 9 and later
				const std::wstring msg = L"Multiplayer is not available yet for Update " + std::to_wstring(g_launcher.md_update)
					+ L".\n\nComing soon!";
				MessageBoxW(hwnd, msg.c_str(), L"MovieDuels", MB_OK | MB_ICONINFORMATION);
			}
			else
			{
				SetPage(Page::Multiplayer);
			}
		}
		else if (hit == kHitSettings)
		{
			PlaySfx(IDR_SND_CLICK);
			SetPage(Page::Options);
		}
		else if (hit >= 0 && hit < kMainButtonCount && kMainButtons[hit].url)
		{
			OpenLink(hwnd, kMainButtons[hit].url);
		}
		else if (hit == kHitClose)
		{
			PlaySfx(IDR_SND_BACK, true);
			DestroyWindow(hwnd);
			return;
		}
		else if (hit == kHitMinimise)
		{
			PlaySfx(IDR_SND_CLICK);
			ShowWindow(hwnd, SW_MINIMIZE);
		}
		else if (hit == kHitMusic)
		{
			PlaySfx(IDR_SND_CLICK);
			g_launcher.settings.music = !g_launcher.settings.music;
			SaveSettings();
			if (g_launcher.settings.music)
			{
				StartMusic(hwnd);
			}
			else
			{
				StopMusic();
			}
		}
		if (IsWindow(hwnd))
		{
			Render();
		}
	}

	// ------------------------------------------------------------------------------------------
	// Window
	// ------------------------------------------------------------------------------------------

	void SetHover(const int hit)
	{
		if (hit != g_launcher.hover)
		{
			if (IsMenuHit(hit))
			{
				PlaySfx(IDR_SND_HOVER); // moved onto another menu button
			}
			g_launcher.hover = hit;
			UpdateGlow();
			Render();
		}
	}

	LRESULT CALLBACK WndProc(const HWND hwnd, const UINT msg, const WPARAM wparam, const LPARAM lparam)
	{
		switch (msg)
		{
		case WM_NCHITTEST:
		{
			// drag the window by anything that is not a button
			POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
			ScreenToClient(hwnd, &pt);
			return HitTest(ToUnits(pt.x, pt.y)) == kHitNone ? HTCAPTION : HTCLIENT;
		}
		case WM_NCLBUTTONDBLCLK:
			return 0; // no maximise by double-clicking the art
		case WM_SETCURSOR:
			if (LOWORD(lparam) == HTCLIENT)
			{
				SetCursor(LoadCursorW(nullptr, IsMenuHit(HitTestCursor()) ? IDC_HAND : IDC_ARROW));
				return TRUE;
			}
			return DefWindowProcW(hwnd, msg, wparam, lparam);
		case WM_MOUSEMOVE:
			if (!g_launcher.tracking_mouse)
			{
				TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
				g_launcher.tracking_mouse = TrackMouseEvent(&tme) != FALSE;
			}
			SetHover(HitTest(ToUnits(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam))));
			return 0;
		case WM_MOUSELEAVE:
		case WM_NCMOUSEMOVE:
			g_launcher.tracking_mouse = false;
			g_launcher.pressed = kHitNone;
			SetHover(kHitNone);
			return 0;
		case WM_LBUTTONDOWN:
			g_launcher.pressed = HitTest(ToUnits(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)));
			SetCapture(hwnd);
			Render();
			return 0;
		case WM_LBUTTONUP:
		{
			ReleaseCapture();
			const int hit = HitTest(ToUnits(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)));
			const int pressed = g_launcher.pressed;
			g_launcher.pressed = kHitNone;
			if (hit != pressed || hit == kHitNone)
			{
				Render(); // released somewhere else
				return 0;
			}
			OnClick(hwnd, hit);
			return 0;
		}
		case WM_KEYDOWN:
			if (wparam == VK_ESCAPE)
			{
				if (CardOpen())
				{
					PlaySfx(IDR_SND_BACK);
					SetPage(Page::Main);
					Render();
				}
				else
				{
					PlaySfx(IDR_SND_BACK, true);
					DestroyWindow(hwnd);
				}
			}
			return 0;
		case WM_TIMER:
			if (wparam == kFadeTimer)
			{
				if (!StepGlow())
				{
					g_launcher.fading = false;
					KillTimer(hwnd, kFadeTimer);
				}
				Render();
			}
			return 0;
		case MM_MCINOTIFY:
			// the music reached its end: play it again (loop)
			if (wparam == MCI_NOTIFY_SUCCESSFUL && g_launcher.music_open)
			{
				PlayMusicFromStart(hwnd);
			}
			return 0;
		case WM_DESTROY:
			KillTimer(hwnd, kFadeTimer);
			StopMusic();
			PostQuitMessage(0);
			return 0;
		default:
			return DefWindowProcW(hwnd, msg, wparam, lparam);
		}
	}
}

int WINAPI wWinMain(_In_ const HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ const int show) // SAL as winbase.h (C28251)
{
	// the window is drawn at the screen's scale itself (as the WPF launcher was), not stretched by Windows
	SetProcessDPIAware();

	g_launcher.instance = instance;

	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	PathRemoveFileSpecW(path);
	g_launcher.folder = path;
	LoadSettings();
	g_launcher.md_update = DetectUpdate();

	Gdiplus::GdiplusStartupInput gdiplus_input;
	ULONG_PTR gdiplus_token = 0;
	Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);
	ChooseScale();
	BuildArt();

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = instance;
	wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_LAUNCHER));
	wc.hIconSm = wc.hIcon;
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.lpszClassName = L"MovieDuelsLauncher";
	RegisterClassExW(&wc);

	// centred on the work area, borderless and layered (the art has its own frame and window buttons)
	RECT work = {};
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
	const int x = work.left + (work.right - work.left - g_launcher.width) / 2;
	const int y = work.top + (work.bottom - work.top - g_launcher.height) / 2;
	g_launcher.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, wc.lpszClassName, L"MovieDuels",
		WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU, x, y, g_launcher.width, g_launcher.height, nullptr, nullptr, instance, nullptr);
	if (!g_launcher.hwnd || !CreateSurface())
	{
		FreeSurface();
		FreeArt();
		Gdiplus::GdiplusShutdown(gdiplus_token);
		return 1;
	}
	Render();
	ShowWindow(g_launcher.hwnd, show);
	StartMusic(g_launcher.hwnd);

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	DeleteMusicFile();
	FreeSurface();
	FreeArt(); // before GDI+ goes
	Gdiplus::GdiplusShutdown(gdiplus_token);
	return 0;
}
