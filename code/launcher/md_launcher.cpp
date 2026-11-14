/*
===========================================================================
MovieDuels launcher
===========================================================================

The team's launcher (MovieDuels-Launcher, a WPF program) built into the game's code, the way the SerenityJediEngine
launcher is: same art, same pages, same behaviour, but a plain Win32 + GDI+ exe that CMake builds with the game
(project "Launcher", MovieDuels-Launcher.exe). This is the team's new launcher of 2026-11-06 (the pages PLAY, NEWS,
ABOUT and SETTINGS, the update notice on the emblem, the news from MovieDuels-Launcher-News).

The window is the team's design (1920x1080 layers in art/) shown at 1088x612 units: the MovieDuels logo between two
bars, Vader on the left and Kylo Ren / Rey on the right, and a bottom bar with SINGLEPLAYER and MULTIPLAYER either side
of the emblem. The top bars hold the pages:

- PLAY: the art, SINGLEPLAYER and MULTIPLAYER either side of the emblem. Hovering a mode fades in its glow layer.
  SINGLEPLAYER starts MovieDuels-SP.x86_64.exe from the launcher's folder, passing the options as "+set" values, and
  closes the launcher. MULTIPLAYER opens the multiplayer card: Start multiplayer starts MovieDuels-MP.x86_64.exe the
  same way, Start server runs "Start MovieDuels-MP-FFA-Server.bat" from the MD folder in its own console (the launcher
  stays open, so the game can be started next to join it). Multiplayer needs Update 9 or later.
- NEWS: the team's articles from MovieDuels-Launcher-News (news.json), side by side: drag them, use the mouse wheel or
  the arrows; a click opens one. What was read is kept in a cache folder, so the news also show offline.
- ABOUT: links, About (the mod), FAQ, Team (texts/), and all Credits (the game's MD\ReadMe\ReadMe_Credits.txt,
  searchable).
- SETTINGS: Renderer, Display, Resolution, Controller (read from the config the game wrote last and written to the
  game's own configs); the launcher's Music and Check for updates (MovieDuels-Launcher.ini in Documents\My Games\
  MovieDuels-UD9); the installation (Jedi Academy, every game file of texts/GameFiles.txt, no outdated file of
  texts/OldGameFiles.txt, installed in Jedi Academy's GameData and not in base or Jedi Outcast, the MD folder's pk3s
  can be read), the add-ons, Open folder and Open config folder. The right column scrolls when it is long.
- The emblem: amber when the installation is wrong (the wrong folder, an outdated file, a missing game file; looked at
  each time the launcher opens; click: SETTINGS, which says what to do), red when a newer update is out (latest.json of
  MovieDuels-Launcher-News; click: what's new), else a click looks for an update. When a newer update than last time
  is installed, its files are checked once: the bars either side of the emblem fill up. The launcher downloads nothing.
- Which update the game's assets are: md_version.txt in a pk3 of MD ("9.1" for a re-upload with fixes), else the way
  the game does it (ui/main.menu "uiScript mdUpdate N"). Update numbers compare part by part (9 < 9.1 < 9.10 < 10).

The window is a layered window with per-pixel alpha: its shape (the rounded corners) is the art's own alpha. It is
DPI aware: everything is laid out in the WPF launcher's units (1/96 inch) and drawn at the screen's scale.
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
#include <winhttp.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <thread>
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
	using Gdiplus::Color;
	using Gdiplus::RectF;
	using Gdiplus::PointF;

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
	const wchar_t* const kOldHomeFolder = L"MovieDuels";  // where the WPF launcher kept its ini
	const wchar_t* const kSPConfig = L"MovieDuels-SP.cfg";
	const wchar_t* const kMPConfig = L"MovieDuels-MP.cfg";

	const wchar_t* const kUrlModDB = L"https://www.moddb.com/mods/movie-duels";
	const wchar_t* const kUrlDiscord = L"https://discord.com/invite/PhQPy7j";
	const wchar_t* const kUrlYouTube = L"https://www.youtube.com/@MovieDuels";
	const wchar_t* const kUrlJKHub = L"https://jkhub.org/files/file/3860-star-wars-movie-duels/";
	const wchar_t* const kUrlNexus = L"https://www.nexusmods.com/starwarsjediknightjediacademy/mods/45";

	// what the team publishes for the launcher (MovieDuels/MovieDuels-Launcher-News): latest.json, news.json and the
	// articles; the launcher reads them, it downloads nothing else
	const wchar_t* const kNewsHost = L"raw.githubusercontent.com";
	const wchar_t* const kNewsPath = L"/MovieDuels/MovieDuels-Launcher-News/main/";
	constexpr DWORD kNetTimeoutMs = 8000;
	constexpr ULONGLONG kNewsRefreshMs = 5 * 60 * 1000; // a file read less than 5 minutes ago comes from the cache

	// background music: MCI plays the MP3 (from a temp file), PlaySound the menu sounds alongside
	const wchar_t* const kMusicAlias = L"md_launcher_music";
	constexpr int kMusicVolume = 600; // 0..1000

	// the MovieDuels update of assets without "uiScript mdUpdate" in their main menu
	constexpr int kDefaultUpdate = 9;

	// A MovieDuels update number: "9", or "9.1", "9.2" for a re-upload with fixes. Compared part by part, so 9.10 comes
	// after 9.9. The game itself only uses the whole number; the launcher reads the decimal one from md_version.txt
	// (DetectUpdate) and from latest.json.
	struct UpdateNumber
	{
		int major = 0;
		int minor = 0;
		std::wstring text = L"0"; // as written ("9.10" stays "9.10")

		static UpdateNumber Of(const int whole)
		{
			UpdateNumber n;
			n.major = whole;
			n.text = std::to_wstring(whole);
			return n;
		}

		// "9", "9.1" (a comma is taken as a point too); false when it is not a number like that
		static bool Parse(std::wstring s, UpdateNumber& out)
		{
			const size_t first = s.find_first_not_of(L" \t\r\n");
			const size_t last = s.find_last_not_of(L" \t\r\n");
			if (first == std::wstring::npos)
			{
				return false;
			}
			s = s.substr(first, last - first + 1);
			std::replace(s.begin(), s.end(), L',', L'.');
			const size_t dot = s.find(L'.');
			const std::wstring major_part = s.substr(0, dot);
			const std::wstring minor_part = dot == std::wstring::npos ? L"" : s.substr(dot + 1);
			const auto digits = [](const std::wstring& part)
				{
					return !part.empty() && part.size() <= 6 && std::all_of(part.begin(), part.end(), [](const wchar_t c) { return c >= L'0' && c <= L'9'; });
				};
			if (!digits(major_part) || (dot != std::wstring::npos && !digits(minor_part)))
			{
				return false;
			}
			out.major = std::stoi(major_part);
			out.minor = minor_part.empty() ? 0 : std::stoi(minor_part);
			out.text = out.minor > 0 ? std::to_wstring(out.major) + L"." + minor_part : std::to_wstring(out.major);
			return true;
		}

		// what the launcher shows: only the whole number ("Update 9" for 9.1 too; DH 2026-11-11)
		std::wstring Shown() const { return std::to_wstring(major); }

		bool operator<(const UpdateNumber& o) const { return major != o.major ? major < o.major : minor < o.minor; }
		bool operator>(const UpdateNumber& o) const { return o < *this; }
		bool operator==(const UpdateNumber& o) const { return major == o.major && minor == o.minor; }
		bool operator!=(const UpdateNumber& o) const { return !(*this == o); }
	};

	// colours from the art (the WPF launcher's brushes)
	const Color kWhiteText(255, 249, 249, 249);
	const Color kGoldText(255, 255, 204, 102);
	const Color kGold(230, 253, 178, 42);
	const Color kGoldBright(255, 255, 214, 120);
	const Color kSoftText(255, 201, 201, 205);
	const Color kDimText(255, 160, 160, 165);
	const Color kLine(26, 255, 255, 255);
	const Color kLine2(46, 255, 255, 255);
	const Color kCardBrush(240, 22, 22, 25);
	const Color kRed(255, 224, 38, 46);
	const Color kVersionGrey(55, 200, 200, 200); // #B8C8C8C8 at 30%

	// the gold glow under the mouse (WPF DropShadowEffect #FFAA28, blur radius 12)
	constexpr BYTE kGlowR = 255, kGlowG = 170, kGlowB = 40;
	constexpr float kGlowBlur = 12.0f;
	constexpr float kGlowStrength = 1.5f;

	// everything that moves runs on one timer while something moves
	constexpr UINT_PTR kAnimTimer = 1;

	// the bars either side of the emblem (the fill while new files are checked), and a news card with its gap
	constexpr float kBarLeft = 132.0f, kBarLeftEnd = 505.0f, kBarRight = 583.0f, kBarRightEnd = 956.0f;
	constexpr float kBarTop = 541.0f, kBarHeight = 33.0f;
	constexpr float kCardStep = 566.0f;

	// window messages from the worker threads (lparam: a heap result the window takes over)
	constexpr UINT kMsgLatest = WM_APP + 1;
	constexpr UINT kMsgNews = WM_APP + 2;
	constexpr UINT kMsgImage = WM_APP + 3;
	constexpr UINT kMsgArticle = WM_APP + 4;
	constexpr UINT kMsgCheckProgress = WM_APP + 5;
	constexpr UINT kMsgCheckDone = WM_APP + 6;

	enum class Page { Play, News, Article, About, Settings, Multiplayer };
	enum class Emblem { Idle, Checking, Available, Verify, Ready, Warning }; // Warning: game files are missing
	enum class Tab { Mod, Faq, Team, Credits };

	// resolutions SETTINGS cycles through (0 x 0 = the desktop resolution, r_mode -2)
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
		bool windowed = true;
		bool controller = false; // in_joystick
		bool music = true;       // launcher background music (speaker button)
		Resolution resolution = { 0, 0 }; // { 0, 0 } = desktop
		bool check_updates = true; // look for a new update (latest.json) when the launcher opens
		UpdateNumber last_update;  // the update the launcher last saw installed (0 = never ran): a higher one gets checked
		std::wstring seen_news;    // ids of the news articles already shown, comma separated
	};

	// the optional add-ons SETTINGS lists under ADDONS: installed when all their files are there
	struct Addon
	{
		const wchar_t* name;
		const wchar_t* page; // where to get it
		std::vector<const wchar_t*> files; // relative to the game folder
	};
	const Addon kAddons[] = {
		{ L"Jedi Outcast", L"https://www.moddb.com/mods/movie-duels/downloads/movie-duels-jedi-outcast-addon",
		// the add-on, and the player's own Jedi Outcast files copied into base under new names
		{ L"MD\\r_outcastaddon.pk3", L"base\\0-assets0.pk3", L"base\\0-assets1.pk3", L"base\\0-assets2.pk3", L"base\\0-assets5.pk3" } },
	{ L"TOTG S1: Dark Forces 2", L"https://www.moddb.com/mods/movie-duels/downloads/tales-of-the-galaxy-dark-forces-ii-duels",
		{ L"MD\\TOTG_DF2.pk3" } },
	};

	// what latest.json says
	struct Latest
	{
		UpdateNumber update; // "update": 9, or "9.1" for a fix release (a string keeps "9.10" as written)
		std::wstring title;
		std::vector<std::wstring> whats_new;
		std::wstring moddb = L"https://www.moddb.com/mods/movie-duels/downloads";
		std::wstring nexus = L"https://www.nexusmods.com/starwarsjediknightjediacademy/mods/45?tab=files";
		std::wstring article; // id of a news article about it
	};

	// an article of news.json
	struct Article
	{
		std::wstring id, tag, date, title, summary, image, file;
	};

	// the installation check
	// where Movie Duels was installed: the launcher sits next to the game exes and the MD folder, which belong in Jedi
	// Academy's GameData folder
	enum class Place
	{
		Ok,
		InsideBase,   // extracted into GameData\base instead of GameData
		JediOutcast,  // installed into Jedi Outcast's GameData
		NoJediAcademy // no Jedi Academy next to it (base\assets0-3.pk3 missing)
	};

	struct CheckResult
	{
		bool jedi_academy = false;
		int pk3_count = 0;
		Place place = Place::Ok;
		std::vector<std::wstring> old;     // outdated files (texts/OldGameFiles.txt) that are still there
		std::vector<std::wstring> missing; // game files (texts/GameFiles.txt) that are not there
		std::vector<std::wstring> broken;  // pk3s that can't be read
	};

	// the texts of ABOUT
	struct FaqItem
	{
		std::wstring question;
		std::vector<std::wstring> answer;
		bool open = false;
	};
	struct FaqGroup
	{
		std::wstring name;
		std::vector<FaqItem> items;
	};
	struct TeamGroup
	{
		std::wstring name;
		std::vector<std::pair<std::wstring, std::wstring>> people; // name, role (or empty)
	};
	struct CreditEntry
	{
		std::wstring name;
		std::wstring note; // "/Special thanks .../": shown as a note, not a card
		std::vector<std::pair<std::wstring, std::wstring>> fields;
		std::vector<std::wstring> links;
	};
	struct CreditGroup
	{
		std::wstring name;
		std::vector<CreditEntry> entries;
	};
	struct CreditSection
	{
		std::wstring name;
		std::vector<CreditGroup> groups;
	};

	// a value that moves to its target over a time (ms for the whole way)
	struct Fader
	{
		float value = 0.0f;
		float target = 0.0f;
		float ms = 200.0f;

		bool Moving() const { return value != target; }
		void Set(const float t, const float time) { target = t; ms = time; }
		void Snap(const float t) { value = target = t; }
		void Step(const float dt)
		{
			const float step = ms > 0.0f ? dt / ms : 1.0f;
			value = value < target ? (std::min)(target, value + step) : (std::max)(target, value - step);
		}
	};

	// a bitmap drawn 1:1 at a pixel position (glows, the art's layers cut to what they cover)
	struct Glow
	{
		std::unique_ptr<Gdiplus::Bitmap> bitmap;
		int x = 0;
		int y = 0;
	};

	// something under the mouse, collected while drawing (window units)
	struct HotRegion
	{
		RectF box;
		std::wstring key;
		std::function<void()> click;
		std::wstring tip;
		bool hover_sound = true;
	};

	// a scrolling part of a page
	struct Scroller
	{
		float offset = 0.0f;
		float target = 0.0f; // the wheel moves the target; the offset follows
		float max = 0.0f;
		RectF view;          // window units, set while drawing
		bool vertical = true;
	};

	// a run of text for the text layout: one look
	enum FontId { kFontLabel, kFontUI, kFontUISemi, kFontUIBold };
	struct TextRun
	{
		std::wstring text;
		FontId font = kFontUI;
		float size = 14.0f;
		Color color = Color(255, 217, 217, 220);
		bool italic = false;
		std::wstring link; // https address
		Color back = Color(0, 0, 0, 0); // a marked search match
	};
	// a word (or part of one) placed by the layout
	struct Placed
	{
		float x, y, w;
		size_t run;
		std::wstring text;
	};
	struct TextBox
	{
		std::vector<TextRun> runs;
		std::vector<Placed> words;
		float width = 0.0f;  // the widest line
		float height = 0.0f;
		float line_height = 0.0f;
	};

	// a block of an article (its HTML, simplified)
	struct ArticleBlock
	{
		enum Kind { kText, kHeading2, kHeading3, kNote, kItem, kImage, kVideo } kind = kText;
		int list_depth = 0;
		int number = 0;  // an <ol> item's number (0: a bullet)
		TextBox text;
		std::wstring image; // kImage: the picture's path; kVideo: the video's picture (img.youtube.com)
		std::wstring video; // kVideo: the YouTube video's id
		float video_width = 480.0f; // kVideo: the width the iframe asks for (240-640)
		float y = 0.0f, h = 0.0f;
	};

	struct Launcher
	{
		HINSTANCE instance = nullptr;
		HWND hwnd = nullptr;
		std::wstring folder; // where the launcher (and the game exes) are
		Settings settings;
		UpdateNumber md_update = UpdateNumber::Of(kDefaultUpdate); // the MovieDuels update of the game's assets (9, 9.1, ...)
		ULONGLONG start_tick = 0;

		std::wstring music_file; // the music written to %TEMP% for MCI
		bool music_open = false;

		// window size in pixels, and pixels per unit
		float scale = 1.0f;
		int width = 0;
		int height = 0;

		// the art at window size
		std::unique_ptr<Gdiplus::Bitmap> base; // the black backing and the background panel
		Glow art, glow_single, glow_multi, play_panel, logo, red_emblem, teal_emblem;
		std::vector<BYTE> bar_mask; // the play panel's alpha over the bars (the fill is cut to it)
		Gdiplus::Rect bar_rect;     // the bars' pixels
		std::unique_ptr<Gdiplus::Bitmap> icon[5], icon_big[5]; // ModDB, Nexus, Discord, YouTube, JKHub
		Glow icon_glow[5];
		std::map<std::wstring, Glow> text_glows;
		std::unique_ptr<Gdiplus::PrivateFontCollection> fonts; // Albertus, from the exe
		std::unique_ptr<Gdiplus::FontFamily> label_family, ui_family, ui_semi_family;
		std::map<std::wstring, std::unique_ptr<Gdiplus::Font>> font_cache;
		std::map<std::wstring, float> width_cache;

		// the layered window's pixels (32 bpp premultiplied DIB)
		HDC mem_dc = nullptr;
		HBITMAP dib = nullptr;
		HGDIOBJ old_bitmap = nullptr;
		void* bits = nullptr;

		// the mouse
		std::vector<HotRegion> hot;      // collected by the last frame
		std::wstring hover;              // key of the region under the mouse
		std::wstring pressed;
		bool tracking_mouse = false;
		PointF mouse = PointF(-1.0f, -1.0f);
		ULONGLONG hover_since = 0;       // for the tooltip
		std::wstring tip;

		// the pages
		Page page = Page::Play;
		Fader art_fade;      // the art: 1 on PLAY, 0.14 on the other pages
		Fader play_fade;     // the bottom bar and its controls
		Fader page_fade;     // the open page fades in and moves up
		Fader glow_single_fade, glow_multi_fade;
		bool multiplayer_side = false; // the mode the mouse was on last stays lit

		// updates
		Emblem emblem = Emblem::Idle;
		bool have_latest = false;
		Latest latest;
		bool looking_for_update = false;
		bool update_asked = false;
		ULONGLONG look_started = 0;
		bool chip_dismissed = false;
		bool sheet_open = false;
		Fader sheet_fade;
		Fader red_fade, teal_fade;
		ULONGLONG emblem_since = 0; // the pulses and ripples run from here
		int ready_pulses = 0;
		ULONGLONG shock_start = 0;

		// the new files check (the bars) and the installation check (SETTINGS)
		bool checking_files = false;  // the bars are running
		bool check_running = false;   // a check thread runs
		bool check_for_bars = false;
		float fill_shown = 0.0f, fill_target = 0.0f;
		int checked_files = 0, total_files = 0;
		std::wstring checked_name;
		bool have_check = false;
		CheckResult last_check;
		std::vector<std::wstring> game_files;    // the pk3s a complete install has in MD (texts/GameFiles.txt)
		std::vector<std::wstring> missing_files; // game files not in MD: the emblem warns on PLAY
		std::vector<std::wstring> old_game_files; // outdated pk3s that must not be in MD (texts/OldGameFiles.txt)
		std::vector<std::wstring> old_files;     // outdated files still in MD: the emblem warns too
		Place place = Place::Ok;                 // installed in the wrong folder: the emblem warns too
		bool bars_done = false;
		Color blade = Color(255, 12, 145, 128);
		Color blade_glow = Color(166, 40, 230, 200);
		Fader fill_fade;    // the fill layer
		Fader modes_fade;   // SINGLEPLAYER / MULTIPLAYER come back
		std::wstring status_left, status_right;
		bool settings_check_shown = false; // the progress row of SETTINGS
		float settings_check_bar = 0.0f;
		std::wstring settings_check_text;
		ULONGLONG settings_check_started = 0;

		// news
		std::vector<Article> articles;
		std::set<std::wstring> fresh_news; // articles not seen when the news were read (they say NEW)
		bool news_loading = false;
		bool news_failed = false;
		bool news_loaded = false;
		std::map<std::wstring, std::unique_ptr<Gdiplus::Bitmap>> pictures; // by path; null: could not be read
		std::set<std::wstring> pictures_asked;
		Scroller news_scroll;
		bool news_down = false, news_dragged = false;
		float news_start_x = 0.0f, news_start_offset = 0.0f, news_last_x = 0.0f, news_speed = 0.0f;
		ULONGLONG news_last_time = 0;
		bool news_animating = false;
		float news_from = 0.0f, news_to = 0.0f;
		ULONGLONG news_anim_start = 0;
		float wheel_sum = 0.0f;
		ULONGLONG wheel_lock = 0;

		// the open article
		int article_index = -1;
		int article_request = 0;
		bool article_loading = false;
		bool article_failed = false;
		std::vector<ArticleBlock> article_blocks;
		float article_height = 0.0f;
		Scroller article_scroll;

		// ABOUT
		Tab tab = Tab::Mod;
		std::vector<std::wstring> about_paragraphs, about_bullets;
		std::wstring about_small;
		std::vector<FaqGroup> faq;
		std::vector<TeamGroup> team;
		bool credits_loaded = false;
		std::vector<CreditSection> credits;
		std::wstring credits_query, credits_shown_query;
		ULONGLONG credits_typed = 0;
		bool search_focus = false;
		Scroller mod_scroll, faq_scroll, team_scroll, credits_scroll, credits_nav_scroll;
		Scroller settings_scroll;            // the right column of SETTINGS, when the installation has a lot to report
		float settings_right_height = 0.0f;  // its content, as the last frame drew it

		// the delayed things (WPF's DispatcherTimers)
		std::vector<std::pair<ULONGLONG, std::function<void()>>> later;
		bool timer_on = false;
		ULONGLONG last_tick = 0;

		// a short message
		std::wstring toast;
		Fader toast_fade;
		int toast_id = 0;

		// a scroll bar thumb being dragged
		Scroller* thumb_drag = nullptr;
		float thumb_grab = 0.0f;
	};

	Launcher g_launcher;

	void Render();
	void StartTimer();
	void ShowToast(const std::wstring& text);

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
		g_launcher.settings.check_updates = GetPrivateProfileIntW(L"Options", L"CheckUpdates", 1, ini.c_str()) != 0;
		wchar_t last[32] = {};
		GetPrivateProfileStringW(L"Options", L"LastUpdate", L"0", last, 32, ini.c_str());
		if (!UpdateNumber::Parse(last, g_launcher.settings.last_update))
		{
			g_launcher.settings.last_update = UpdateNumber();
		}
		std::vector<wchar_t> seen(8192);
		GetPrivateProfileStringW(L"Options", L"SeenNews", L"", seen.data(), static_cast<DWORD>(seen.size()), ini.c_str());
		g_launcher.settings.seen_news = seen.data();
		LoadGameConfig(); // renderer, display, resolution, controller: what the game's config says
	}

	void SaveSettings()
	{
		const std::wstring ini = IniPath();
		WritePrivateProfileStringW(L"Options", L"Rend2", g_launcher.settings.rend2 ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"Windowed", g_launcher.settings.windowed ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"Controller", g_launcher.settings.controller ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"Music", g_launcher.settings.music ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"CheckUpdates", g_launcher.settings.check_updates ? L"1" : L"0", ini.c_str());
		WritePrivateProfileStringW(L"Options", L"LastUpdate", g_launcher.settings.last_update.text.c_str(), ini.c_str());
		WritePrivateProfileStringW(L"Options", L"SeenNews", g_launcher.settings.seen_news.c_str(), ini.c_str());
	}

	// the launcher's own files (the news cache) live next to the ini
	std::wstring DataFolder()
	{
		std::wstring dir = IniPath();
		const size_t slash = dir.find_last_of(L'\\');
		return slash == std::wstring::npos ? g_launcher.folder : dir.substr(0, slash);
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

	ULONGLONG LastWrite(const std::wstring& path)
	{
		WIN32_FILE_ATTRIBUTE_DATA data = {};
		if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
		{
			return 0;
		}
		return (static_cast<ULONGLONG>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
	}

	// renderer, display, resolution and controller from the game's config that was written last (SP's or MP's: the
	// game that ran last, which writes its config when it quits). Without a config (a new install) they start as
	// Vanilla, windowed, desktop resolution and no controller.
	void LoadGameConfig()
	{
		Settings& s = g_launcher.settings;
		const std::wstring sp = ConfigPath(false), mp = ConfigPath(true);
		const bool mp_first = LastWrite(mp) > LastWrite(sp);
		std::vector<std::string> lines = ReadLines(mp_first ? mp : sp);
		if (lines.empty())
		{
			lines = ReadLines(mp_first ? sp : mp);
		}
		if (lines.empty())
		{
			s.rend2 = false;
			s.windowed = true;
			s.resolution = { 0, 0 };
			s.controller = false;
			return;
		}
		std::string v;
		if (ReadCfgValue(lines, "in_joystick", v))
		{
			s.controller = atoi(v.c_str()) != 0;
		}
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

	// the cvars SETTINGS sets, for the SP or the MP config / command line
	std::vector<std::pair<std::string, std::string>> GameConfigValues(const bool multiplayer)
	{
		const Settings& s = g_launcher.settings;
		std::vector<std::pair<std::string, std::string>> values = {
			{ "cl_renderer", RendererName(s.rend2, multiplayer) },
			{ "com_rend2", s.rend2 ? "1" : "0" },
			{ "r_fullscreen", s.windowed ? "0" : "1" },
			{ "in_joystick", s.controller ? "1" : "0" } };
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

	// the resolution before this one in the list
	void PreviousResolution()
	{
		Settings& s = g_launcher.settings;
		const int count = static_cast<int>(sizeof kResolutions / sizeof kResolutions[0]);
		int previous = 0;
		for (int i = 0; i < count; i++)
		{
			if (kResolutions[i].width == s.resolution.width && kResolutions[i].height == s.resolution.height)
			{
				previous = (i + count - 1) % count;
				break;
			}
		}
		s.resolution = kResolutions[previous];
	}

	// ------------------------------------------------------------------------------------------
	// Which MovieDuels version the installed assets are:
	//
	// 1. md_version.txt in a pk3 of the MD folder (in MD_Assets.pk3; a later patch pk3 can bring a newer one; one below 9
	//    is the MD 8 assets' and is skipped, like the game skips "mdUpdate 8"): the
	//    version as text, for example "9.1" for a re-upload of Update 9 with fixes. The first number in the file counts,
	//    so "Update 9.1" works too, and lines starting with "#" or "//" are notes.
	// 2. Without that file, the way the game finds it: the singleplayer main menu (ui/main.menu) says it with
	//    "uiScript mdUpdate 9", which sets the game's read-only md_update cvar. A main menu without that line is
	//    Update 9 (the game's default).
	//
	// Like the game, the file in the last pk3 (in name order) of the MD folder that has it is the one that counts.
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

	// A file's text from a pk3 (name with '/'), or false if it has none (or can't be read: not a zip, locked, ...).
	bool ReadEntry(const std::wstring& pk3, const char* name, std::string& text)
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
		std::string backslashed = name;
		std::replace(backslashed.begin(), backslashed.end(), '/', '\\');
		if (unzLocateFile(zip, name, 2) == UNZ_OK || (backslashed != name && unzLocateFile(zip, backslashed.c_str(), 2) == UNZ_OK))
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

	// md_version.txt: the first number that isn't on a note line; false when there is none
	bool ParseVersionFile(const std::string& text, UpdateNumber& version)
	{
		static const std::regex number("\\d+(?:\\.\\d+)?");
		size_t start = 0;
		while (start < text.size())
		{
			size_t end = text.find('\n', start);
			if (end == std::string::npos)
			{
				end = text.size();
			}
			std::string line = text.substr(start, end - start);
			start = end + 1;
			const size_t first = line.find_first_not_of(" \t\r");
			if (first == std::string::npos)
			{
				continue;
			}
			line = line.substr(first);
			if (line[0] == '#' || line.compare(0, 2, "//") == 0)
			{
				continue;
			}
			std::smatch m;
			if (std::regex_search(line, m, number))
			{
				const std::string found = m.str();
				if (UpdateNumber::Parse(std::wstring(found.begin(), found.end()), version))
				{
					return true;
				}
			}
		}
		return false;
	}

	UpdateNumber ParseUpdate(const std::string& menu)
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
					return UpdateNumber::Of(update);
				}
			}
			start = end + 1;
		}
		return UpdateNumber::Of(kDefaultUpdate);
	}

	UpdateNumber DetectUpdate()
	{
		const std::wstring dir = g_launcher.folder + L"\\" + kModFolder;
		std::vector<std::wstring> pk3s;
		WIN32_FIND_DATAW fd;
		const HANDLE find = FindFirstFileW((dir + L"\\*.pk3").c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE)
		{
			return UpdateNumber::Of(kDefaultUpdate);
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
			std::string text;
			UpdateNumber version;
			// below 9 it is the MD 8 assets' file (MD 9 ships MD_Assets.pk3 with "8.1"): ignored, as the game ignores
			// "uiScript mdUpdate 8"
			if (ReadEntry(dir + L"\\" + name, "md_version.txt", text) && ParseVersionFile(text, version)
				&& version.major >= kDefaultUpdate)
			{
				return version;
			}
		}
		for (const std::wstring& name : pk3s)
		{
			std::string menu;
			if (ReadEntry(dir + L"\\" + name, "ui/main.menu", menu))
			{
				return ParseUpdate(menu);
			}
		}
		return UpdateNumber::Of(kDefaultUpdate);
	}

	// ------------------------------------------------------------------------------------------
	// Text helpers
	// ------------------------------------------------------------------------------------------

	std::wstring FromUtf8(const std::string& s)
	{
		if (s.empty())
		{
			return L"";
		}
		const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
		std::wstring w(static_cast<size_t>(n), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
		return w;
	}

	// UTF-8 (without its BOM), or code page 1250 when it isn't UTF-8 (the installed ReadMe_Credits.txt is)
	std::wstring DecodeText(const std::string& bytes)
	{
		std::string s = bytes;
		if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB
			&& static_cast<unsigned char>(s[2]) == 0xBF)
		{
			s.erase(0, 3);
		}
		if (s.empty())
		{
			return L"";
		}
		const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
		if (n > 0)
		{
			std::wstring w(static_cast<size_t>(n), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
			return w;
		}
		const int m = MultiByteToWideChar(1250, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
		std::wstring w(static_cast<size_t>((std::max)(m, 0)), L'\0');
		if (m > 0)
		{
			MultiByteToWideChar(1250, 0, s.data(), static_cast<int>(s.size()), &w[0], m);
		}
		return w;
	}

	std::wstring Trim(const std::wstring& s)
	{
		const size_t a = s.find_first_not_of(L" \t\r\n");
		if (a == std::wstring::npos)
		{
			return L"";
		}
		const size_t b = s.find_last_not_of(L" \t\r\n");
		return s.substr(a, b - a + 1);
	}

	bool StartsWith(const std::wstring& s, const wchar_t* prefix, const bool ignore_case = false)
	{
		const size_t n = wcslen(prefix);
		if (s.size() < n)
		{
			return false;
		}
		return ignore_case ? _wcsnicmp(s.c_str(), prefix, n) == 0 : s.compare(0, n, prefix) == 0;
	}

	std::wstring Upper(std::wstring s)
	{
		if (!s.empty())
		{
			CharUpperBuffW(&s[0], static_cast<DWORD>(s.size()));
		}
		return s;
	}

	// where query is in text, ignoring case (-1: not there)
	int FindNoCase(const std::wstring& text, const std::wstring& query)
	{
		if (query.empty() || query.size() > text.size())
		{
			return -1;
		}
		const int at = FindStringOrdinal(FIND_FROMSTART, text.c_str(), static_cast<int>(text.size()), query.c_str(),
			static_cast<int>(query.size()), TRUE);
		return at;
	}

	std::vector<std::wstring> SplitLines(const std::wstring& text)
	{
		std::vector<std::wstring> lines;
		size_t start = 0;
		while (start <= text.size())
		{
			size_t end = text.find(L'\n', start);
			if (end == std::wstring::npos)
			{
				end = text.size();
			}
			std::wstring line = text.substr(start, end - start);
			if (!line.empty() && line.back() == L'\r')
			{
				line.pop_back();
			}
			lines.push_back(line);
			start = end + 1;
		}
		return lines;
	}

	bool ReadFileBytes(const std::wstring& path, std::string& data)
	{
		const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE)
		{
			return false;
		}
		LARGE_INTEGER size = {};
		bool ok = GetFileSizeEx(h, &size) && size.QuadPart < 64 * 1024 * 1024;
		if (ok)
		{
			data.assign(static_cast<size_t>(size.QuadPart), '\0');
			DWORD read = 0;
			ok = data.empty() || (ReadFile(h, &data[0], static_cast<DWORD>(data.size()), &read, nullptr) && read == data.size());
		}
		CloseHandle(h);
		return ok;
	}

	bool WriteFileBytes(const std::wstring& path, const std::string& data)
	{
		const HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE)
		{
			return false;
		}
		DWORD written = 0;
		const bool ok = data.empty() || (WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size());
		CloseHandle(h);
		return ok;
	}

	// the text of one of the exe's RCDATA resources (texts/)
	std::wstring ResourceText(const int id)
	{
		const void* bytes;
		DWORD size;
		if (!GetResourceBytes(id, &bytes, &size))
		{
			return L"";
		}
		return DecodeText(std::string(static_cast<const char*>(bytes), size));
	}

	// ------------------------------------------------------------------------------------------
	// A small JSON reader (latest.json, news.json)
	// ------------------------------------------------------------------------------------------

	struct Json
	{
		enum Type { kNull, kBool, kNumber, kString, kArray, kObject } type = kNull;
		double number = 0.0;
		std::wstring text;
		std::vector<Json> items;
		std::vector<std::pair<std::wstring, Json>> members;

		const Json* Get(const wchar_t* key) const
		{
			for (const auto& m : members)
			{
				if (m.first == key)
				{
					return &m.second;
				}
			}
			return nullptr;
		}

		// a member as text (a number too), trimmed; empty when missing
		std::wstring Str(const wchar_t* key) const
		{
			const Json* v = Get(key);
			if (!v)
			{
				return L"";
			}
			if (v->type == kString)
			{
				return Trim(v->text);
			}
			if (v->type == kNumber)
			{
				wchar_t buf[64];
				swprintf(buf, 64, L"%g", v->number);
				return buf;
			}
			return L"";
		}
	};

	class JsonParser
	{
	public:
		explicit JsonParser(const std::wstring& text) : s_(text) {}

		bool Parse(Json& out)
		{
			Skip();
			if (!Value(out, 0))
			{
				return false;
			}
			Skip();
			return pos_ == s_.size();
		}

	private:
		const std::wstring& s_;
		size_t pos_ = 0;

		void Skip()
		{
			while (pos_ < s_.size() && (s_[pos_] == L' ' || s_[pos_] == L'\t' || s_[pos_] == L'\r' || s_[pos_] == L'\n' || s_[pos_] == 0xFEFF))
			{
				pos_++;
			}
		}

		bool Word(const wchar_t* w)
		{
			const size_t n = wcslen(w);
			if (s_.compare(pos_, n, w) != 0)
			{
				return false;
			}
			pos_ += n;
			return true;
		}

		bool String(std::wstring& out)
		{
			if (pos_ >= s_.size() || s_[pos_] != L'"')
			{
				return false;
			}
			pos_++;
			while (pos_ < s_.size())
			{
				const wchar_t c = s_[pos_++];
				if (c == L'"')
				{
					return true;
				}
				if (c != L'\\')
				{
					out += c;
					continue;
				}
				if (pos_ >= s_.size())
				{
					return false;
				}
				const wchar_t e = s_[pos_++];
				switch (e)
				{
				case L'n': out += L'\n'; break;
				case L't': out += L'\t'; break;
				case L'r': out += L'\r'; break;
				case L'b': out += L'\b'; break;
				case L'f': out += L'\f'; break;
				case L'u':
					if (pos_ + 4 > s_.size())
					{
						return false;
					}
					out += static_cast<wchar_t>(wcstoul(s_.substr(pos_, 4).c_str(), nullptr, 16));
					pos_ += 4;
					break;
				default: out += e; break;
				}
			}
			return false;
		}

		bool Value(Json& out, const int depth)
		{
			if (depth > 32 || pos_ >= s_.size())
			{
				return false;
			}
			const wchar_t c = s_[pos_];
			if (c == L'{')
			{
				out.type = Json::kObject;
				pos_++;
				Skip();
				if (pos_ < s_.size() && s_[pos_] == L'}')
				{
					pos_++;
					return true;
				}
				for (;;)
				{
					Skip();
					std::wstring key;
					if (!String(key))
					{
						return false;
					}
					Skip();
					if (pos_ >= s_.size() || s_[pos_] != L':')
					{
						return false;
					}
					pos_++;
					Skip();
					Json value;
					if (!Value(value, depth + 1))
					{
						return false;
					}
					out.members.emplace_back(key, std::move(value));
					Skip();
					if (pos_ < s_.size() && s_[pos_] == L',')
					{
						pos_++;
						continue;
					}
					if (pos_ < s_.size() && s_[pos_] == L'}')
					{
						pos_++;
						return true;
					}
					return false;
				}
			}
			if (c == L'[')
			{
				out.type = Json::kArray;
				pos_++;
				Skip();
				if (pos_ < s_.size() && s_[pos_] == L']')
				{
					pos_++;
					return true;
				}
				for (;;)
				{
					Skip();
					Json value;
					if (!Value(value, depth + 1))
					{
						return false;
					}
					out.items.push_back(std::move(value));
					Skip();
					if (pos_ < s_.size() && s_[pos_] == L',')
					{
						pos_++;
						continue;
					}
					if (pos_ < s_.size() && s_[pos_] == L']')
					{
						pos_++;
						return true;
					}
					return false;
				}
			}
			if (c == L'"')
			{
				out.type = Json::kString;
				return String(out.text);
			}
			if (Word(L"true") || Word(L"false"))
			{
				out.type = Json::kBool;
				out.number = s_[pos_ - 1] == L'e' && s_[pos_ - 2] == L'u' ? 1.0 : 0.0;
				return true;
			}
			if (Word(L"null"))
			{
				out.type = Json::kNull;
				return true;
			}
			wchar_t* end = nullptr;
			out.number = wcstod(s_.c_str() + pos_, &end);
			if (end == s_.c_str() + pos_)
			{
				return false;
			}
			out.type = Json::kNumber;
			pos_ = static_cast<size_t>(end - s_.c_str());
			return true;
		}
	};

	bool ParseJson(const std::wstring& text, Json& out)
	{
		if (Trim(text).empty())
		{
			return false;
		}
		JsonParser parser(text);
		return parser.Parse(out) && out.type == Json::kObject; // a typo in the file: as if there was none
	}

	bool ParseLatest(const std::wstring& json, Latest& latest)
	{
		Json o;
		if (!ParseJson(json, o))
		{
			return false;
		}
		UpdateNumber update;
		if (!UpdateNumber::Parse(o.Str(L"update"), update))
		{
			return false;
		}
		latest = Latest();
		latest.update = update;
		latest.title = o.Str(L"title");
		if (latest.title.empty())
		{
			latest.title = L"Update " + latest.update.Shown();
		}
		latest.article = o.Str(L"article");
		if (!o.Str(L"moddb").empty())
		{
			latest.moddb = o.Str(L"moddb");
		}
		if (!o.Str(L"nexus").empty())
		{
			latest.nexus = o.Str(L"nexus");
		}
		const Json* lines = o.Get(L"whatsNew");
		if (lines && lines->type == Json::kArray)
		{
			for (const Json& line : lines->items)
			{
				const std::wstring text = Trim(line.text);
				if (line.type == Json::kString && !text.empty())
				{
					latest.whats_new.push_back(text);
				}
			}
		}
		return true;
	}

	std::vector<Article> ParseNews(const std::wstring& json)
	{
		std::vector<Article> articles;
		Json o;
		if (!ParseJson(json, o))
		{
			return articles;
		}
		const Json* items = o.Get(L"articles");
		if (!items || items->type != Json::kArray)
		{
			return articles;
		}
		for (const Json& a : items->items)
		{
			if (a.type != Json::kObject || a.Str(L"id").empty() || a.Str(L"title").empty())
			{
				continue;
			}
			Article article;
			article.id = a.Str(L"id");
			article.tag = a.Str(L"tag").empty() ? L"News" : a.Str(L"tag");
			article.date = a.Str(L"date");
			article.title = a.Str(L"title");
			article.summary = a.Str(L"summary");
			article.image = a.Str(L"image");
			article.file = a.Str(L"file");
			articles.push_back(article);
		}
		return articles;
	}

	// ------------------------------------------------------------------------------------------
	// Online: the files of MovieDuels-Launcher-News, with a cache next to the ini (called off the window's thread)
	// ------------------------------------------------------------------------------------------

	// a path of the news repository as host + path; a full https address stays as it is. Only https, and nothing
	// outside the news repository.
	bool ResolveUrl(const std::wstring& path, std::wstring& host, std::wstring& url_path)
	{
		if (path.empty())
		{
			return false;
		}
		if (StartsWith(path, L"https://", true))
		{
			const size_t slash = path.find(L'/', 8);
			host = path.substr(8, slash == std::wstring::npos ? std::wstring::npos : slash - 8);
			url_path = slash == std::wstring::npos ? L"/" : path.substr(slash);
			return !host.empty();
		}
		if (StartsWith(path, L"http:", true) || path.find(L"..") != std::wstring::npos)
		{
			return false;
		}
		host = kNewsHost;
		std::wstring p = path;
		while (!p.empty() && p[0] == L'/')
		{
			p.erase(0, 1);
		}
		url_path = std::wstring(kNewsPath) + p;
		return true;
	}

	std::wstring CacheFolder()
	{
		return DataFolder() + L"\\LauncherNews";
	}

	std::wstring CachePath(const std::wstring& host, const std::wstring& url_path)
	{
		std::wstring name;
		for (const wchar_t c : host + url_path)
		{
			name += iswalnum(c) || c == L'.' || c == L'-' ? c : L'_';
		}
		return CacheFolder() + L"\\" + name;
	}

	bool Download(const std::wstring& host, const std::wstring& url_path, std::string& data)
	{
		bool ok = false;
		const HINTERNET session = WinHttpOpen(L"MovieDuels-Launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session)
		{
			return false;
		}
		WinHttpSetTimeouts(session, kNetTimeoutMs, kNetTimeoutMs, kNetTimeoutMs, kNetTimeoutMs);
		const HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
		const HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", url_path.c_str(), nullptr, WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
		if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
			&& WinHttpReceiveResponse(request, nullptr))
		{
			DWORD status = 0, size = sizeof(status);
			WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
				&status, &size, WINHTTP_NO_HEADER_INDEX);
			if (status == 200)
			{
				ok = true;
				for (;;)
				{
					DWORD available = 0;
					if (!WinHttpQueryDataAvailable(request, &available))
					{
						ok = false;
						break;
					}
					if (available == 0)
					{
						break;
					}
					std::string chunk(available, '\0');
					DWORD read = 0;
					if (!WinHttpReadData(request, &chunk[0], available, &read))
					{
						ok = false;
						break;
					}
					data.append(chunk.data(), read);
					if (data.size() > 32 * 1024 * 1024)
					{
						ok = false;
						break;
					}
				}
			}
		}
		if (request)
		{
			WinHttpCloseHandle(request);
		}
		if (connect)
		{
			WinHttpCloseHandle(connect);
		}
		WinHttpCloseHandle(session);
		return ok;
	}

	// A file of the news repository. A file read less than 5 minutes ago comes from the cache (opening the launcher
	// again and again doesn't ask GitHub each time); without a connection the cache is used however old it is.
	// prefer_cache: pictures don't change, the cache comes first.
	bool GetBytes(const std::wstring& path, std::string& data, const bool prefer_cache = false)
	{
		std::wstring host, url_path;
		if (!ResolveUrl(path, host, url_path))
		{
			return false;
		}
		const std::wstring cached = CachePath(host, url_path);
		const ULONGLONG written = LastWrite(cached);
		if (written)
		{
			FILETIME now_ft;
			GetSystemTimeAsFileTime(&now_ft);
			const ULONGLONG now = (static_cast<ULONGLONG>(now_ft.dwHighDateTime) << 32) | now_ft.dwLowDateTime;
			const bool fresh = now > written && (now - written) / 10000 < kNewsRefreshMs;
			if ((prefer_cache || fresh) && ReadFileBytes(cached, data))
			{
				return true;
			}
		}
		data.clear();
		if (Download(host, url_path, data))
		{
			CreateDirectoryW(CacheFolder().c_str(), nullptr);
			WriteFileBytes(cached, data); // a failed read isn't cached, so "Try again" still asks
			return true;
		}
		data.clear();
		return written && ReadFileBytes(cached, data);
	}

	bool GetText(const std::wstring& path, std::wstring& text)
	{
		std::string data;
		if (!GetBytes(path, data))
		{
			return false;
		}
		text = DecodeText(data);
		return true;
	}

	// a picture from bytes (on the window's thread: GDI+ bitmaps stay on it)
	std::unique_ptr<Gdiplus::Bitmap> DecodePicture(const std::string& data)
	{
		if (data.empty())
		{
			return nullptr;
		}
		IStream* stream = SHCreateMemStream(reinterpret_cast<const BYTE*>(data.data()), static_cast<UINT>(data.size()));
		if (!stream)
		{
			return nullptr;
		}
		std::unique_ptr<Gdiplus::Bitmap> from_stream(Gdiplus::Bitmap::FromStream(stream));
		std::unique_ptr<Gdiplus::Bitmap> copy;
		if (from_stream && from_stream->GetLastStatus() == Gdiplus::Ok && from_stream->GetWidth() > 0)
		{
			copy.reset(from_stream->Clone(0, 0, from_stream->GetWidth(), from_stream->GetHeight(), PixelFormat32bppPARGB));
		}
		from_stream.reset();
		stream->Release();
		return copy;
	}

	// ------------------------------------------------------------------------------------------
	// The installation: Jedi Academy's base\assets0-3.pk3, and every pk3 of the MD folder opens as a zip (its file
	// list can be read), so a broken or half-copied download shows up. Nothing is changed.
	// ------------------------------------------------------------------------------------------

	bool HasJediAcademy()
	{
		for (const wchar_t* name : { L"assets0.pk3", L"assets1.pk3", L"assets2.pk3", L"assets3.pk3" })
		{
			if (!PathFileExistsW((g_launcher.folder + L"\\base\\" + name).c_str()))
			{
				return false;
			}
		}
		return true;
	}

	std::vector<std::wstring> Pk3Files()
	{
		std::vector<std::wstring> files;
		WIN32_FIND_DATAW fd;
		const HANDLE find = FindFirstFileW((g_launcher.folder + L"\\" + kModFolder + L"\\*.pk3").c_str(), &fd);
		if (find == INVALID_HANDLE_VALUE)
		{
			return files;
		}
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !_wcsicmp(PathFindExtensionW(fd.cFileName), L".pk3"))
			{
				files.emplace_back(fd.cFileName);
			}
		} while (FindNextFileW(find, &fd));
		FindClose(find);
		std::sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b)
			{
				return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
			});
		return files;
	}

	std::vector<std::wstring> MissingAddonFiles(const Addon& addon)
	{
		std::vector<std::wstring> missing;
		for (const wchar_t* file : addon.files)
		{
			if (!PathFileExistsW((g_launcher.folder + L"\\" + file).c_str()))
			{
				missing.emplace_back(file);
			}
		}
		return missing;
	}

	// texts/GameFiles.txt: a name per line, "#" lines are notes; ".pk3" may be left out
	std::vector<std::wstring> ParseGameFiles(const std::wstring& text)
	{
		std::vector<std::wstring> names;
		size_t start = 0;
		while (start <= text.size())
		{
			size_t end = text.find(L'\n', start);
			if (end == std::wstring::npos)
			{
				end = text.size();
			}
			std::wstring line = text.substr(start, end - start);
			start = end + 1;
			const size_t first = line.find_first_not_of(L" \t\r\xFEFF");
			const size_t last = line.find_last_not_of(L" \t\r");
			if (first == std::wstring::npos || line[first] == L'#')
			{
				continue;
			}
			line = line.substr(first, last - first + 1);
			if (line.size() < 4 || _wcsicmp(line.c_str() + line.size() - 4, L".pk3"))
			{
				line += L".pk3";
			}
			names.push_back(line);
		}
		return names;
	}

	// the game files that are not in the MD folder (a quick look: only whether each file is there)
	std::vector<std::wstring> MissingGameFiles(const std::wstring& folder, const std::vector<std::wstring>& game_files)
	{
		std::vector<std::wstring> missing;
		for (const std::wstring& file : game_files)
		{
			if (!PathFileExistsW((folder + L"\\" + kModFolder + L"\\" + file).c_str()))
			{
				missing.push_back(file);
			}
		}
		return missing;
	}

	// the outdated files (texts/OldGameFiles.txt) that are still in the MD folder
	std::vector<std::wstring> FoundOldGameFiles(const std::wstring& folder, const std::vector<std::wstring>& old_game_files)
	{
		std::vector<std::wstring> found;
		for (const std::wstring& file : old_game_files)
		{
			if (PathFileExistsW((folder + L"\\" + kModFolder + L"\\" + file).c_str()))
			{
				found.push_back(file);
			}
		}
		return found;
	}

	// where Movie Duels was installed (folder: the launcher's, next to the game exes and the MD folder)
	Place CheckPlace(const std::wstring& folder)
	{
		const auto has = [&folder](const wchar_t* file) { return PathFileExistsW((folder + L"\\" + file).c_str()) != FALSE; };
		if (!_wcsicmp(PathFindFileNameW(folder.c_str()), L"base") || has(L"assets0.pk3"))
		{
			return Place::InsideBase;
		}
		if (has(L"jk2sp.exe") || has(L"jk2mp.exe") || (has(L"base\\assets5.pk3") && !has(L"base\\assets3.pk3")))
		{
			return Place::JediOutcast;
		}
		return HasJediAcademy() ? Place::Ok : Place::NoJediAcademy;
	}

	// progress posted as kMsgCheckProgress (wparam done, lparam total; the name in a heap string), the result as
	// kMsgCheckDone
	void RunFilesCheck(const HWND hwnd, const std::wstring folder, const std::vector<std::wstring> game_files,
		const std::vector<std::wstring> old_game_files)
	{
		auto* result = new CheckResult;
		result->jedi_academy = HasJediAcademy();
		result->place = CheckPlace(folder);
		result->old = FoundOldGameFiles(folder, old_game_files);
		result->missing = MissingGameFiles(folder, game_files);
		const std::vector<std::wstring> files = Pk3Files();
		result->pk3_count = static_cast<int>(files.size());
		for (size_t i = 0; i < files.size(); i++)
		{
			PostMessageW(hwnd, kMsgCheckProgress, i, reinterpret_cast<LPARAM>(new std::pair<int, std::wstring>(
				static_cast<int>(files.size()), files[i])));
			const std::string path = ZipPath(folder + L"\\" + kModFolder + L"\\" + files[i]);
			const unzFile zip = path.empty() ? nullptr : unzOpen(path.c_str());
			unz_global_info info = {};
			const bool ok = zip && unzGetGlobalInfo(zip, &info) == UNZ_OK && info.number_entry > 0;
			if (zip)
			{
				unzClose(zip);
			}
			if (!ok)
			{
				result->broken.push_back(files[i]); // not a zip, cut short, locked ...
			}
		}
		PostMessageW(hwnd, kMsgCheckProgress, files.size(), reinterpret_cast<LPARAM>(new std::pair<int, std::wstring>(
			static_cast<int>(files.size()), L"")));
		if (!PostMessageW(hwnd, kMsgCheckDone, 0, reinterpret_cast<LPARAM>(result)))
		{
			delete result;
		}
	}

	// ------------------------------------------------------------------------------------------
	// The texts of ABOUT, in the plain formats the team writes them in:
	//   texts/About.txt  a paragraph per line, "- " lines are a list, the last paragraph is the small print
	//   texts/FAQ.txt    "## Group", "Q: question", "A: answer" (more lines belong to the answer)
	//   texts/Team.txt   a heading line, then "- Name - Role" (team) or "- Name" (thanks)
	//   MD/ReadMe/ReadMe_Credits.txt (the game's own): **Section**, ==Group==, /Entry/, "Key: value" lines
	// Lines starting with "# " are notes for whoever edits the file and are not shown.
	// ------------------------------------------------------------------------------------------

	std::vector<std::wstring> ContentLines(const std::wstring& text)
	{
		std::vector<std::wstring> lines;
		for (const std::wstring& raw : SplitLines(text))
		{
			const std::wstring line = Trim(raw);
			if (!StartsWith(line, L"# ") && line != L"#")
			{
				lines.push_back(line);
			}
		}
		return lines;
	}

	void ParseAbout(const std::wstring& text)
	{
		Launcher& l = g_launcher;
		for (const std::wstring& line : ContentLines(text))
		{
			if (line.empty())
			{
				continue;
			}
			if (StartsWith(line, L"- "))
			{
				l.about_bullets.push_back(Trim(line.substr(2)));
			}
			else
			{
				l.about_paragraphs.push_back(line);
			}
		}
		// the last paragraph after the list is the small print (the disclaimer)
		if (l.about_paragraphs.size() > 1)
		{
			l.about_small = l.about_paragraphs.back();
			l.about_paragraphs.pop_back();
		}
	}

	void ParseFaq(const std::wstring& text)
	{
		std::vector<FaqGroup>& groups = g_launcher.faq;
		FaqItem* item = nullptr;
		for (const std::wstring& line : ContentLines(text))
		{
			if (StartsWith(line, L"## "))
			{
				groups.push_back(FaqGroup());
				groups.back().name = Trim(line.substr(3));
				item = nullptr;
			}
			else if (StartsWith(line, L"Q:"))
			{
				if (groups.empty())
				{
					groups.push_back(FaqGroup());
				}
				groups.back().items.push_back(FaqItem());
				item = &groups.back().items.back();
				item->question = Trim(line.substr(2));
			}
			else if (StartsWith(line, L"A:") && item)
			{
				item->answer.push_back(Trim(line.substr(2)));
			}
			else if (!line.empty() && item)
			{
				item->answer.push_back(line);
			}
		}
	}

	void ParseTeam(const std::wstring& text)
	{
		std::vector<TeamGroup>& groups = g_launcher.team;
		for (const std::wstring& line : ContentLines(text))
		{
			if (line.empty())
			{
				continue;
			}
			if (StartsWith(line, L"- "))
			{
				if (groups.empty())
				{
					groups.push_back(TeamGroup());
				}
				const std::wstring item = Trim(line.substr(2));
				const size_t dash = item.find(L" - ");
				if (dash != std::wstring::npos && dash > 0)
				{
					groups.back().people.emplace_back(Trim(item.substr(0, dash)), Trim(item.substr(dash + 3)));
				}
				else
				{
					groups.back().people.emplace_back(item, L"");
				}
			}
			else
			{
				groups.push_back(TeamGroup());
				groups.back().name = line;
			}
		}
		groups.erase(std::remove_if(groups.begin(), groups.end(), [](const TeamGroup& g) { return g.people.empty(); }), groups.end());
	}

	void ParseCredits(const std::wstring& text)
	{
		std::vector<CreditSection>& sections = g_launcher.credits;
		CreditEntry* entry = nullptr;
		for (const std::wstring& raw : SplitLines(text))
		{
			const std::wstring l = Trim(raw);
			if (l.empty())
			{
				entry = nullptr;
				continue;
			}
			if (l.size() > 4 && StartsWith(l, L"**") && l.compare(l.size() - 2, 2, L"**") == 0)
			{
				sections.push_back(CreditSection());
				sections.back().name = Trim(l.substr(2, l.size() - 4));
				sections.back().groups.push_back(CreditGroup());
				entry = nullptr;
				continue;
			}
			if (sections.empty())
			{
				continue; // the title line
			}
			CreditSection& section = sections.back();
			if (l.size() >= 4 && StartsWith(l, L"==") && l.compare(l.size() - 2, 2, L"==") == 0)
			{
				section.groups.push_back(CreditGroup());
				section.groups.back().name = Trim(l.substr(2, l.size() - 4));
				entry = nullptr;
				continue;
			}
			if (l.size() > 2 && l[0] == L'/' && l.back() == L'/')
			{
				const std::wstring name = Trim(l.substr(1, l.size() - 2));
				section.groups.back().entries.push_back(CreditEntry());
				if (StartsWith(name, L"Special thanks", true))
				{
					section.groups.back().entries.back().note = name;
					entry = nullptr;
				}
				else
				{
					entry = &section.groups.back().entries.back();
					entry->name = name;
				}
				continue;
			}
			if (!entry)
			{
				continue;
			}
			if (StartsWith(l, L"https://", true) || StartsWith(l, L"http://", true))
			{
				entry->links.push_back(l);
				continue;
			}
			const size_t colon = l.find(L':');
			if (colon != std::wstring::npos && colon > 0)
			{
				const std::wstring key = Trim(l.substr(0, colon)), value = Trim(l.substr(colon + 1));
				if (StartsWith(key, L"Link", true))
				{
					if (!value.empty())
					{
						entry->links.push_back(value);
					}
				}
				else if (!value.empty())
				{
					entry->fields.emplace_back(key, value);
				}
				continue;
			}
			entry->fields.emplace_back(L"", l);
		}
		for (CreditSection& s : sections)
		{
			s.groups.erase(std::remove_if(s.groups.begin(), s.groups.end(), [](const CreditGroup& g) { return g.entries.empty(); }), s.groups.end());
		}
		sections.erase(std::remove_if(sections.begin(), sections.end(), [](const CreditSection& s) { return s.groups.empty(); }), sections.end());
	}

	// the name of a link for its button: the site
	std::wstring LinkName(const std::wstring& url)
	{
		std::wstring host = url;
		const size_t scheme = host.find(L"://");
		if (scheme != std::wstring::npos)
		{
			host = host.substr(scheme + 3);
		}
		if (StartsWith(host, L"www.", true))
		{
			host = host.substr(4);
		}
		host = host.substr(0, host.find(L'/'));
		CharLowerBuffW(&host[0], static_cast<DWORD>(host.size()));
		if (host == L"jkhub.org")
		{
			return L"JKHub";
		}
		if (host == L"community.moviebattles.org")
		{
			return L"Movie Battles II";
		}
		if (host == L"mrwonko.de")
		{
			return L"JK3Files archive";
		}
		return host;
	}

	// "MODELS / SKINS [Prequel Trilogy]" -> group "Models / Skins", name "Prequel Trilogy"
	void SectionLabel(const std::wstring& name, std::wstring& group, std::wstring& label)
	{
		const size_t open = name.rfind(L'[');
		if (name.size() > 2 && name.back() == L']' && open != std::wstring::npos)
		{
			group = L"Models / Skins";
			label = name.substr(open + 1, name.size() - open - 2);
			return;
		}
		if (StartsWith(name, L"Weapons (") && name.back() == L')')
		{
			group = L"Weapons";
			label = name.substr(9, name.size() - 10);
			const std::wstring hilts = L"Lightsaber Hilts/Melee Weapons";
			const size_t at = label.find(hilts);
			if (at != std::wstring::npos)
			{
				label.replace(at, hilts.size(), L"Hilts and melee");
			}
			return;
		}
		if (!_wcsicmp(name.c_str(), L"Maps") || !_wcsicmp(name.c_str(), L"Missions"))
		{
			group = L"Maps & missions";
			label = name;
			return;
		}
		group = L"Other";
		label = name;
		const std::wstring source = L"Movie Duels Source Code";
		const size_t at = label.find(source);
		if (at != std::wstring::npos)
		{
			label.replace(at, source.size(), L"Source code");
		}
	}

	void OpenUrl(const std::wstring& url); // the links (below, with the actions)

	// ------------------------------------------------------------------------------------------
	// Art at window size
	// ------------------------------------------------------------------------------------------

	std::unique_ptr<Gdiplus::Bitmap> NewSurface(const int width, const int height)
	{
		return std::make_unique<Gdiplus::Bitmap>((std::max)(1, width), (std::max)(1, height), PixelFormat32bppPARGB);
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
	void DrawStretched(Gdiplus::Graphics& g, Gdiplus::Image* image, const RectF& dest, const float opacity = 1.0f,
		const bool black = false)
	{
		if (!image || opacity <= 0.0f)
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

	// Draws a pixel-size bitmap 1:1 at a pixel position, at an opacity (the units' transform is set aside).
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

	void DrawLayer(Gdiplus::Graphics& g, const Glow& layer, const float opacity = 1.0f)
	{
		DrawPixels(g, layer.bitmap.get(), layer.x, layer.y, opacity);
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
		if (glow->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &data) != Gdiplus::Ok)
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

	// A 1920x1080 layer of the design at window size, cut to the part it covers (drawing it then costs less).
	Glow WindowLayer(const int resource)
	{
		Glow layer;
		const std::unique_ptr<Gdiplus::Bitmap> image = LoadImageResource(resource);
		std::unique_ptr<Gdiplus::Bitmap> full = NewSurface(g_launcher.width, g_launcher.height);
		{
			Gdiplus::Graphics g(full.get());
			SetQuality(g);
			g.Clear(Color(0, 0, 0, 0));
			DrawStretched(g, image.get(), RectF(0.0f, 0.0f, static_cast<float>(g_launcher.width), static_cast<float>(g_launcher.height)));
		}
		Gdiplus::Rect rect(0, 0, g_launcher.width, g_launcher.height);
		Gdiplus::BitmapData data = {};
		int x0 = g_launcher.width, y0 = g_launcher.height, x1 = -1, y1 = -1;
		if (full->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) == Gdiplus::Ok)
		{
			for (int y = 0; data.Scan0 && y < g_launcher.height; y++)
			{
				const BYTE* row = static_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
				for (int x = 0; x < g_launcher.width; x++)
				{
					if (row[x * 4 + 3])
					{
						x0 = (std::min)(x0, x);
						x1 = (std::max)(x1, x);
						y0 = (std::min)(y0, y);
						y1 = (std::max)(y1, y);
					}
				}
			}
			full->UnlockBits(&data);
		}
		if (x1 < x0)
		{
			return layer;
		}
		layer.x = x0;
		layer.y = y0;
		layer.bitmap.reset(full->Clone(x0, y0, x1 - x0 + 1, y1 - y0 + 1, PixelFormat32bppPARGB));
		return layer;
	}

	// Builds the art at the window's size, once (the window's scale does not change while it is open).
	void BuildArt()
	{
		Launcher& l = g_launcher;
		const int w = l.width;
		const int h = l.height;
		const float s = l.scale;
		const RectF full(0.0f, 0.0f, static_cast<Gdiplus::REAL>(w), static_cast<Gdiplus::REAL>(h));

		// base: the black backing (the team's Background Panel is glass, 81% opaque: the backing in the same rounded
		// shape, just inside the gold outline, makes the window solid) and the background panel
		l.base = NewSurface(w, h);
		{
			Gdiplus::Graphics g(l.base.get());
			SetQuality(g);
			g.Clear(Color(0, 0, 0, 0));
			g.ScaleTransform(s, s);
			Gdiplus::GraphicsPath backing;
			const RectF r(2.3f, 4.6f, kUnitW - 2.3f - 1.2f, kUnitH - 4.6f - 4.0f);
			const float d = 34.5f * 2;
			backing.AddArc(r.X, r.Y, d, d, 180, 90);
			backing.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
			backing.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
			backing.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
			backing.CloseFigure();
			const Gdiplus::SolidBrush black(Color(255, 0, 0, 0));
			g.FillPath(&black, &backing);
			g.ResetTransform();
			const std::unique_ptr<Gdiplus::Bitmap> background = LoadImageResource(IDR_IMG_BACKGROUND_PANEL);
			DrawStretched(g, background.get(), full);
		}
		l.art = WindowLayer(IDR_IMG_PLAY_BACKGROUND);
		l.glow_single = WindowLayer(IDR_IMG_SINGLEPLAYER_GLOW);
		l.glow_multi = WindowLayer(IDR_IMG_MULTIPLAYER_GLOW);
		l.play_panel = WindowLayer(IDR_IMG_PLAY_PANEL);
		l.logo = WindowLayer(IDR_IMG_LOGO_GLOW);
		l.red_emblem = WindowLayer(IDR_IMG_DOWNLOAD);
		l.teal_emblem = WindowLayer(IDR_IMG_DOWNLOAD_READY);

		// the play panel's alpha over the bars: the fill is cut to the bars' own shape, without the emblem's circle
		l.bar_rect = Gdiplus::Rect(static_cast<int>(kBarLeft * s), static_cast<int>(kBarTop * s),
			static_cast<int>((kBarRightEnd - kBarLeft) * s) + 1, static_cast<int>(kBarHeight * s) + 1);
		l.bar_mask.assign(static_cast<size_t>(l.bar_rect.Width) * l.bar_rect.Height, 0);
		if (l.play_panel.bitmap)
		{
			Gdiplus::Bitmap* panel = l.play_panel.bitmap.get();
			Gdiplus::Rect rect(0, 0, static_cast<INT>(panel->GetWidth()), static_cast<INT>(panel->GetHeight()));
			Gdiplus::BitmapData data = {};
			if (panel->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) == Gdiplus::Ok)
			{
				for (int y = 0; data.Scan0 && y < l.bar_rect.Height; y++)
				{
					const int py = l.bar_rect.Y + y - l.play_panel.y;
					for (int x = 0; x < l.bar_rect.Width; x++)
					{
						const int px = l.bar_rect.X + x - l.play_panel.x;
						if (px >= 0 && py >= 0 && px < rect.Width && py < rect.Height)
						{
							l.bar_mask[static_cast<size_t>(y) * l.bar_rect.Width + x] =
								static_cast<const BYTE*>(data.Scan0)[static_cast<ptrdiff_t>(py) * data.Stride + static_cast<long long>(px) * 4 + 3];
						}
					}
				}
				panel->UnlockBits(&data);
			}
		}

		// the icons top left at their two sizes, and the glow of the big one
		const int icons[5] = { IDR_ICON_MODDB, IDR_ICON_NEXUS, IDR_ICON_DISCORD, IDR_ICON_YOUTUBE, IDR_ICON_JKHUB };
		for (int i = 0; i < 5; i++)
		{
			l.icon[i] = LoadImageResource(icons[i]);
			const float cx = (32.0f + 30.0f * i + 15.0f) * s;
			const float cy = 27.0f * s;
			const int px = static_cast<int>(24.0f * 1.14f * s + 0.5f);
			l.icon_big[i] = NewSurface(px, px);
			{
				Gdiplus::Graphics g(l.icon_big[i].get());
				SetQuality(g);
				g.Clear(Color(0, 0, 0, 0));
				DrawStretched(g, l.icon[i].get(), RectF(0.0f, 0.0f, static_cast<float>(px), static_cast<float>(px)));
			}
			const int margin = static_cast<int>(std::ceil(kGlowBlur * s));
			Gdiplus::Bitmap shape(px + margin * 2, px + margin * 2, PixelFormat32bppPARGB);
			{
				Gdiplus::Graphics g(&shape);
				g.Clear(Color(0, 0, 0, 0));
				DrawPixels(g, l.icon_big[i].get(), margin, margin);
			}
			l.icon_glow[i].bitmap = MakeGlow(shape);
			l.icon_glow[i].x = static_cast<int>(cx - px / 2.0f) - margin;
			l.icon_glow[i].y = static_cast<int>(cy - px / 2.0f) - margin;
		}
	}

	void FreeArt()
	{
		Launcher& l = g_launcher;
		l.base.reset();
		for (Glow* layer : { &l.art, &l.glow_single, &l.glow_multi, &l.play_panel, &l.logo, &l.red_emblem, &l.teal_emblem })
		{
			layer->bitmap.reset();
		}
		for (int i = 0; i < 5; i++)
		{
			l.icon[i].reset();
			l.icon_big[i].reset();
			l.icon_glow[i].bitmap.reset();
		}
		l.pictures.clear();
		l.text_glows.clear();
		l.font_cache.clear();
		l.label_family.reset();
		l.ui_family.reset();
		l.ui_semi_family.reset();
		l.fonts.reset();
	}

	// ------------------------------------------------------------------------------------------
	// Fonts and text: Albertus (the design's label font, in the exe) and Segoe UI
	// ------------------------------------------------------------------------------------------

	void LoadFonts()
	{
		Launcher& l = g_launcher;
		l.fonts = std::make_unique<Gdiplus::PrivateFontCollection>();
		const void* bytes;
		DWORD size;
		if (GetResourceBytes(IDR_FONT_LABEL, &bytes, &size))
		{
			// the exe's resource stays while it runs; the collection's own family (looking it up by name doesn't
			// always find a memory font)
			if (l.fonts->AddMemoryFont(bytes, static_cast<INT>(size)) == Gdiplus::Ok && l.fonts->GetFamilyCount() > 0)
			{
				Gdiplus::FontFamily family;
				INT found = 0;
				if (l.fonts->GetFamilies(1, &family, &found) == Gdiplus::Ok && found == 1 && family.IsAvailable())
				{
					l.label_family.reset(family.Clone());
				}
			}
		}
		for (const wchar_t* name : { L"Palatino Linotype", L"Georgia", L"Times New Roman" })
		{
			if (l.label_family && l.label_family->IsAvailable())
			{
				break;
			}
			l.label_family = std::make_unique<Gdiplus::FontFamily>(name);
		}
		if (!l.label_family->IsAvailable())
		{
			l.label_family.reset(Gdiplus::FontFamily::GenericSerif()->Clone());
		}
		for (const wchar_t* name : { L"Segoe UI", L"Tahoma", L"Arial" })
		{
			l.ui_family = std::make_unique<Gdiplus::FontFamily>(name);
			if (l.ui_family->IsAvailable())
			{
				break;
			}
		}
		if (!l.ui_family->IsAvailable())
		{
			l.ui_family.reset(Gdiplus::FontFamily::GenericSansSerif()->Clone());
		}
		l.ui_semi_family = std::make_unique<Gdiplus::FontFamily>(L"Segoe UI Semibold");
		if (!l.ui_semi_family->IsAvailable())
		{
			l.ui_semi_family.reset();
		}
	}

	const Gdiplus::FontFamily* Family(const FontId font, int& style)
	{
		Launcher& l = g_launcher;
		style = Gdiplus::FontStyleRegular;
		switch (font)
		{
		case kFontLabel:
			style = l.label_family->IsStyleAvailable(Gdiplus::FontStyleBold) ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular;
			return l.label_family.get();
		case kFontUISemi:
			if (l.ui_semi_family)
			{
				return l.ui_semi_family.get();
			}
			style = Gdiplus::FontStyleBold;
			return l.ui_family.get();
		case kFontUIBold:
			style = Gdiplus::FontStyleBold;
			return l.ui_family.get();
		default:
			return l.ui_family.get();
		}
	}

	Gdiplus::Font* GetFont(const FontId font, const float size, const bool italic = false)
	{
		const std::wstring key = std::to_wstring(font) + L"|" + std::to_wstring(static_cast<int>(size * 100)) + (italic ? L"i" : L"");
		std::unique_ptr<Gdiplus::Font>& f = g_launcher.font_cache[key];
		if (!f)
		{
			int style;
			const Gdiplus::FontFamily* family = Family(font, style);
			if (italic)
			{
				style |= Gdiplus::FontStyleItalic;
			}
			f = std::make_unique<Gdiplus::Font>(family, size, style, Gdiplus::UnitPixel);
		}
		return f.get();
	}

	// the height of a line of a font, in units
	float LineSpacing(const FontId font, const float size)
	{
		int style;
		const Gdiplus::FontFamily* family = Family(font, style);
		const float em = family->GetEmHeight(style);
		return em > 0 ? size * family->GetLineSpacing(style) / em : size * 1.33f;
	}

	const Gdiplus::StringFormat* Typographic()
	{
		static Gdiplus::StringFormat* format = nullptr;
		if (!format)
		{
			format = Gdiplus::StringFormat::GenericTypographic()->Clone();
			format->SetFormatFlags(format->GetFormatFlags() | Gdiplus::StringFormatFlagsMeasureTrailingSpaces | Gdiplus::StringFormatFlagsNoWrap);
		}
		return format;
	}

	Gdiplus::Graphics* MeasureGraphics()
	{
		static Gdiplus::Bitmap* bitmap = nullptr;
		static Gdiplus::Graphics* g = nullptr;
		if (!g)
		{
			bitmap = new Gdiplus::Bitmap(1, 1, PixelFormat32bppPARGB);
			g = new Gdiplus::Graphics(bitmap);
			g->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
		}
		return g;
	}

	float TextWidth(const FontId font, const float size, const std::wstring& text, const bool italic = false)
	{
		if (text.empty())
		{
			return 0.0f;
		}
		const std::wstring key = std::to_wstring(font) + L"|" + std::to_wstring(static_cast<int>(size * 100)) + (italic ? L"i|" : L"|") + text;
		const auto found = g_launcher.width_cache.find(key);
		if (found != g_launcher.width_cache.end())
		{
			return found->second;
		}
		RectF box;
		MeasureGraphics()->MeasureString(text.c_str(), static_cast<INT>(text.size()), GetFont(font, size, italic), PointF(0, 0), Typographic(), &box);
		if (g_launcher.width_cache.size() > 20000)
		{
			g_launcher.width_cache.clear();
		}
		g_launcher.width_cache[key] = box.Width;
		return box.Width;
	}

	TextRun Run(const std::wstring& text, const FontId font, const float size, const Color color)
	{
		TextRun r;
		r.text = text;
		r.font = font;
		r.size = size;
		r.color = color;
		return r;
	}

	// Lays the runs out in lines of at most width (words that don't fit go to the next line; "\n" starts one).
	// line_height 0: the first run's font's own.
	void LayoutText(TextBox& box, const float width, const float line_height = 0.0f, const bool centre = false)
	{
		box.words.clear();
		box.width = 0.0f;
		box.line_height = line_height > 0.0f ? line_height
			: box.runs.empty() ? 16.0f : LineSpacing(box.runs[0].font, box.runs[0].size);
		float x = 0.0f, y = 0.0f;
		size_t line_start = 0;
		const auto end_line = [&]()
			{
				// trailing spaces don't count; centred lines move to the middle
				float right = 0.0f;
				for (size_t i = line_start; i < box.words.size(); i++)
				{
					const Placed& p = box.words[i];
					std::wstring t = p.text;
					while (!t.empty() && t.back() == L' ')
					{
						t.pop_back();
					}
					const TextRun& r = box.runs[p.run];
					right = (std::max)(right, p.x + TextWidth(r.font, r.size, t, r.italic));
				}
				if (centre && right < width)
				{
					for (size_t i = line_start; i < box.words.size(); i++)
					{
						box.words[i].x += (width - right) / 2.0f;
					}
				}
				box.width = (std::max)(box.width, right);
				line_start = box.words.size();
			};
		for (size_t r = 0; r < box.runs.size(); r++)
		{
			const TextRun& run = box.runs[r];
			size_t i = 0;
			while (i < run.text.size())
			{
				if (run.text[i] == L'\n')
				{
					end_line();
					x = 0.0f;
					y += box.line_height;
					i++;
					continue;
				}
				// a word and the spaces after it
				size_t j = i;
				while (j < run.text.size() && run.text[j] != L' ' && run.text[j] != L'\n')
				{
					j++;
				}
				while (j < run.text.size() && run.text[j] == L' ')
				{
					j++;
				}
				const std::wstring word = run.text.substr(i, j - i);
				std::wstring bare = word;
				while (!bare.empty() && bare.back() == L' ')
				{
					bare.pop_back();
				}
				const float w = TextWidth(run.font, run.size, word, run.italic);
				const float bare_w = TextWidth(run.font, run.size, bare, run.italic);
				if (x > 0.0f && x + bare_w > width)
				{
					end_line();
					x = 0.0f;
					y += box.line_height;
				}
				if (!(x == 0.0f && bare.empty())) // no spaces at the start of a line
				{
					box.words.push_back({ x, y, w, r, word });
				}
				x += w;
				i = j;
			}
		}
		end_line();
		box.height = box.words.empty() && box.runs.empty() ? 0.0f : y + box.line_height;
	}

	TextBox MakeText(const std::wstring& text, const FontId font, const float size, const Color color, const float width,
		const float line_height = 0.0f, const bool centre = false)
	{
		TextBox box;
		box.runs.push_back(Run(text, font, size, color));
		LayoutText(box, width, line_height, centre);
		return box;
	}

	// ------------------------------------------------------------------------------------------
	// Drawing (in units: the graphics carry the window's scale). The page being drawn can be moved (g_dy) and
	// clipped (g_clip): the mouse regions follow.
	// ------------------------------------------------------------------------------------------

	float g_dx = 0.0f, g_dy = 0.0f;
	RectF g_clip(0.0f, 0.0f, kUnitW, kUnitH);
	float g_alpha = 1.0f; // the page's opacity

	Color Alpha(const Color& c, const float a)
	{
		return Color(static_cast<BYTE>((std::max)(0.0f, (std::min)(1.0f, a)) * c.GetA()), c.GetR(), c.GetG(), c.GetB());
	}

	Color Mix(const Color& a, const Color& b, const float k)
	{
		return Color(static_cast<BYTE>(a.GetA() + (b.GetA() - a.GetA()) * k), static_cast<BYTE>(a.GetR() + (b.GetR() - a.GetR()) * k),
			static_cast<BYTE>(a.GetG() + (b.GetG() - a.GetG()) * k), static_cast<BYTE>(a.GetB() + (b.GetB() - a.GetB()) * k));
	}

	RectF Moved(const RectF& r)
	{
		return RectF(r.X + g_dx, r.Y + g_dy, r.Width, r.Height);
	}

	bool Contains(const RectF& r, const PointF& p)
	{
		return p.X >= r.X && p.Y >= r.Y && p.X < r.X + r.Width && p.Y < r.Y + r.Height;
	}

	// A region under the mouse (in the page's coordinates); returns whether the mouse is on it.
	bool Hot(const RectF& r, const std::wstring& key, std::function<void()> click = nullptr, const std::wstring& tip = L"",
		const bool hover_sound = true)
	{
		RectF box = Moved(r);
		RectF clipped;
		if (!RectF::Intersect(clipped, box, g_clip))
		{
			return false;
		}
		g_launcher.hot.push_back({ clipped, key, std::move(click), tip, hover_sound });
		return g_launcher.hover == key;
	}

	bool IsHover(const std::wstring& key)
	{
		return g_launcher.hover == key;
	}

	bool IsDown(const std::wstring& key)
	{
		return g_launcher.pressed == key && g_launcher.hover == key;
	}

	void AddRoundRect(Gdiplus::GraphicsPath& path, const RectF& r, Gdiplus::REAL radius)
	{
		radius = (std::min)(radius, (std::min)(r.Width, r.Height) / 2.0f);
		if (radius <= 0.1f)
		{
			path.AddRectangle(r);
			return;
		}
		const Gdiplus::REAL d = radius * 2;
		path.AddArc(r.X, r.Y, d, d, 180, 90);
		path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
		path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
		path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
		path.CloseFigure();
	}

	void FillRound(Gdiplus::Graphics& g, const RectF& r, const float radius, const Color& c)
	{
		if (c.GetA() == 0)
		{
			return;
		}
		Gdiplus::GraphicsPath path;
		AddRoundRect(path, Moved(r), radius);
		const Gdiplus::SolidBrush brush(Alpha(c, g_alpha));
		g.FillPath(&brush, &path);
	}

	void FillRoundGradient(Gdiplus::Graphics& g, const RectF& r, const float radius, const Color& top, const Color& bottom)
	{
		Gdiplus::GraphicsPath path;
		const RectF m = Moved(r);
		AddRoundRect(path, m, radius);
		const Gdiplus::LinearGradientBrush brush(RectF(m.X, m.Y - 0.5f, m.Width, m.Height + 1.0f), Alpha(top, g_alpha), Alpha(bottom, g_alpha),
			Gdiplus::LinearGradientModeVertical);
		g.FillPath(&brush, &path);
	}

	void StrokeRound(Gdiplus::Graphics& g, const RectF& r, const float radius, const Color& c, const float width = 1.0f)
	{
		if (c.GetA() == 0)
		{
			return;
		}
		Gdiplus::GraphicsPath path;
		const RectF m = Moved(r);
		AddRoundRect(path, RectF(m.X + width / 2, m.Y + width / 2, m.Width - width, m.Height - width), radius - width / 2);
		const Gdiplus::Pen pen(Alpha(c, g_alpha), width);
		g.DrawPath(&pen, &path);
	}

	void FillRect(Gdiplus::Graphics& g, const RectF& r, const Color& c)
	{
		const Gdiplus::SolidBrush brush(Alpha(c, g_alpha));
		g.FillRectangle(&brush, Moved(r));
	}

	// a soft glow or shadow around a rounded shape: rings of fading colour
	void SoftRound(Gdiplus::Graphics& g, const RectF& r, const float radius, const Color& c, const float blur, const float dy = 0.0f)
	{
		const int rings = 6;
		for (int i = rings; i >= 1; i--)
		{
			const float grow = blur * i / rings;
			const float a = c.GetA() / 255.0f * (1.0f - static_cast<float>(i - 1) / rings) / rings * 1.6f;
			FillRound(g, RectF(r.X - grow, r.Y - grow + dy, r.Width + grow * 2, r.Height + grow * 2), radius + grow,
				Color(static_cast<BYTE>((std::min)(255.0f, a * 255.0f)), c.GetR(), c.GetG(), c.GetB()));
		}
	}

	// a page's card: dark glass, a thin edge and a shadow
	void DrawCardBack(Gdiplus::Graphics& g, const RectF& r, const float radius = 24.0f)
	{
		SoftRound(g, r, radius, Color(115, 0, 0, 0), 30.0f, 10.0f);
		FillRound(g, r, radius, kCardBrush);
		StrokeRound(g, r, radius, kLine);
	}

	void HLine(Gdiplus::Graphics& g, const float x, const float y, const float w, const Color& c = kLine)
	{
		FillRect(g, RectF(x, y, w, 1.0f), c);
	}

	// one line of text, cut with "..." when it doesn't fit; align 0 left, 1 centre, 2 right
	void DrawLine(Gdiplus::Graphics& g, const std::wstring& text, const FontId font, const float size, const Color& c,
		const RectF& r, const int align = 0, const bool shadow = false)
	{
		Gdiplus::StringFormat fmt(Gdiplus::StringFormat::GenericTypographic());
		fmt.SetFormatFlags(fmt.GetFormatFlags() | Gdiplus::StringFormatFlagsNoWrap);
		fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
		fmt.SetAlignment(align == 1 ? Gdiplus::StringAlignmentCenter : align == 2 ? Gdiplus::StringAlignmentFar : Gdiplus::StringAlignmentNear);
		fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		RectF m = Moved(r);
		if (shadow)
		{
			const Gdiplus::SolidBrush dark(Color(static_cast<BYTE>(140 * g_alpha), 0, 0, 0));
			g.DrawString(text.c_str(), -1, GetFont(font, size), RectF(m.X + 1.0f, m.Y + 1.0f, m.Width, m.Height), &fmt, &dark);
		}
		const Gdiplus::SolidBrush brush(Alpha(c, g_alpha));
		g.DrawString(text.c_str(), -1, GetFont(font, size), m, &fmt, &brush);
	}

	// A laid out text at x, y; its links are mouse regions (key: prefix + word).
	void DrawText(Gdiplus::Graphics& g, const TextBox& box, const float x, const float y, const std::wstring& key = L"",
		const float alpha = 1.0f)
	{
		const float top_clip = g_clip.Y - g_dy, bottom_clip = g_clip.Y + g_clip.Height - g_dy;
		for (size_t i = 0; i < box.words.size(); i++)
		{
			const Placed& p = box.words[i];
			if (y + p.y + box.line_height < top_clip || y + p.y > bottom_clip)
			{
				continue;
			}
			const TextRun& r = box.runs[p.run];
			const float lh = LineSpacing(r.font, r.size);
			const float ty = y + p.y + (box.line_height - lh) / 2.0f;
			if (r.back.GetA())
			{
				std::wstring bare = p.text;
				while (!bare.empty() && bare.back() == L' ')
				{
					bare.pop_back();
				}
				FillRect(g, RectF(x + p.x, ty, TextWidth(r.font, r.size, bare, r.italic), lh), r.back);
			}
			Color c = r.color;
			if (!r.link.empty())
			{
				const std::wstring url = r.link;
				const bool hover = Hot(RectF(x + p.x, ty, p.w, lh), key + L"link" + std::to_wstring(i), [url]()
					{
						OpenUrl(url);
					}, url, false);
				if (hover)
				{
					c = kWhiteText;
				}
			}
			const Gdiplus::SolidBrush brush(Alpha(c, g_alpha * alpha));
			g.DrawString(p.text.c_str(), static_cast<INT>(p.text.size()), GetFont(r.font, r.size, r.italic),
				PointF(x + p.x + g_dx, ty + g_dy), Typographic(), &brush);
		}
	}

	// The glow of a lit label (made once per text and place, then kept).
	Glow* TextGlow(const Gdiplus::GraphicsPath& path, const std::wstring& key, const RectF& box)
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
			g.Clear(Color(0, 0, 0, 0));
			g.TranslateTransform(static_cast<Gdiplus::REAL>(-x), static_cast<Gdiplus::REAL>(-y));
			g.ScaleTransform(s, s);
			const Gdiplus::SolidBrush white(Color(255, 255, 255, 255));
			g.FillPath(&white, &path);
		}
		Glow& glow = g_launcher.text_glows[key];
		glow.bitmap = MakeGlow(shape);
		glow.x = x;
		glow.y = y;
		return &glow;
	}

	// A label in the design's font centred in box (WPF ContentPresenter): white with a soft shadow, or gold with a
	// gold glow when lit; pressed moves it down a unit. colour: the unlit colour.
	void DrawLabel(Gdiplus::Graphics& g, const std::wstring& text, const RectF& box_in, const float size, const bool lit,
		const bool down, const Color& colour = kWhiteText, const bool glow_when_lit = true)
	{
		const RectF box = Moved(box_in);
		Gdiplus::StringFormat fmt;
		fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
		fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
		Gdiplus::GraphicsPath path;
		int style;
		const Gdiplus::FontFamily* family = Family(kFontLabel, style);
		path.AddString(text.c_str(), -1, family, style, size, box, &fmt);
		if (down)
		{
			Gdiplus::Matrix offset;
			offset.Translate(0.0f, 1.0f);
			path.Transform(&offset);
		}
		if (lit && glow_when_lit)
		{
			const std::wstring key = text + L"|" + std::to_wstring(static_cast<int>(box.X)) + L"," + std::to_wstring(static_cast<int>(box.Y))
				+ L"," + std::to_wstring(static_cast<int>(size)) + (down ? L"d" : L"");
			const Glow* glow = TextGlow(path, key, box);
			if (glow && glow->bitmap)
			{
				DrawPixels(g, glow->bitmap.get(), glow->x, glow->y, g_alpha);
			}
		}
		else if (!lit)
		{
			// WPF DropShadowEffect: black, 1.5 units down-right, blur 2, 70%
			Gdiplus::Matrix shadow_offset;
			shadow_offset.Translate(1.06f, 1.06f);
			std::unique_ptr<Gdiplus::GraphicsPath> shadow(path.Clone());
			shadow->Transform(&shadow_offset);
			const Gdiplus::SolidBrush shadow_brush(Color(static_cast<BYTE>(140 * g_alpha), 0, 0, 0));
			g.FillPath(&shadow_brush, shadow.get());
		}
		const Gdiplus::SolidBrush fill(Alpha(lit ? kGoldText : colour, g_alpha));
		g.FillPath(&fill, &path);
	}

	// ------------------------------------------------------------------------------------------
	// Buttons of the cards
	// ------------------------------------------------------------------------------------------

	// (the gold PrimaryButton went with the update window's ModDB / Nexus buttons, team commit eb86c26)

	// outlined pill
	bool GhostButton(Gdiplus::Graphics& g, const RectF& r, const std::wstring& text, const std::wstring& key, std::function<void()> click,
		const std::wstring& tip = L"", const bool enabled = true, const float size = 13.0f)
	{
		const bool hover = Hot(r, key, enabled ? std::move(click) : nullptr, tip) && enabled;
		const float a = g_alpha;
		if (!enabled)
		{
			g_alpha *= 0.45f;
		}
		FillRound(g, r, r.Height / 2, Color(89, 0, 0, 0));
		StrokeRound(g, r, r.Height / 2, hover ? kGoldText : kLine2);
		if (!text.empty())
		{
			DrawLine(g, text, kFontUISemi, size, hover ? kGoldText : kWhiteText, r, 1);
		}
		g_alpha = a;
		return hover;
	}

	// a small arrow up and to the right (an outside link), drawn: not every font has the character
	void DrawOutArrow(Gdiplus::Graphics& g, const float x, const float y, const Color& c)
	{
		Gdiplus::Pen pen(Alpha(c, g_alpha), 1.2f);
		pen.SetStartCap(Gdiplus::LineCapRound);
		pen.SetEndCap(Gdiplus::LineCapRound);
		pen.SetLineJoin(Gdiplus::LineJoinRound);
		const float ox = x + g_dx, oy = y + g_dy;
		g.DrawLine(&pen, ox, oy + 6.0f, ox + 6.0f, oy);
		const PointF head[] = { { ox + 2.0f, oy },{ ox + 6.0f, oy },{ ox + 6.0f, oy + 4.0f } };
		g.DrawLines(&pen, head, 3);
	}

	// a small framed link button ("Get it", a credit's site) with the outside-link arrow; with click it does that
	// instead of opening url (the tooltip), and has no arrow
	bool LinkChip(Gdiplus::Graphics& g, const RectF& r, const std::wstring& text, const std::wstring& key, const std::wstring& url,
		std::function<void()> click = nullptr)
	{
		const bool arrow = !click;
		const bool hover = Hot(r, key, click ? std::move(click) : std::function<void()>([url]() { OpenUrl(url); }), url);
		FillRound(g, r, 5.0f, hover ? Color(31, 255, 204, 102) : Color(89, 0, 0, 0));
		StrokeRound(g, r, 5.0f, hover ? kGoldText : kLine2);
		DrawLine(g, text, kFontUISemi, 11.0f, kGoldBright, RectF(r.X + 8.0f, r.Y, r.Width - 16.0f - (arrow ? 10.0f : 0.0f), r.Height));
		if (arrow)
		{
			DrawOutArrow(g, r.X + r.Width - 8.0f - 7.0f, r.Y + r.Height / 2.0f - 3.5f, kGoldBright);
		}
		return hover;
	}

	float LinkChipWidth(const std::wstring& text, const bool arrow = true)
	{
		return TextWidth(kFontUISemi, 11.0f, text) + 16.0f + 2.0f + (arrow ? 10.0f : 0.0f);
	}

	// a two-way switch of SETTINGS: the chosen half is gold
	void Switch(Gdiplus::Graphics& g, const float right, const float cy, const wchar_t* a, const wchar_t* b, const bool second,
		const std::wstring& key, std::function<void(bool)> choose)
	{
		const float wa = (std::max)(78.0f, TextWidth(kFontUISemi, 12.5f, a) + 20.0f);
		const float wb = (std::max)(78.0f, TextWidth(kFontUISemi, 12.5f, b) + 20.0f);
		const RectF box(right - wa - wb - 8.0f, cy - 17.0f, wa + wb + 8.0f, 34.0f);
		FillRound(g, box, 17.0f, Color(115, 0, 0, 0));
		StrokeRound(g, box, 17.0f, kLine2);
		for (int i = 0; i < 2; i++)
		{
			const RectF r(box.X + 4.0f + (i ? wa : 0.0f), box.Y + 4.0f, i ? wb : wa, 26.0f);
			const bool on = (i == 1) == second;
			const bool hover = Hot(r, key + (i ? L"b" : L"a"), [choose, i]() { choose(i == 1); });
			if (on)
			{
				FillRoundGradient(g, r, 13.0f, Color(255, 255, 220, 138), Color(255, 242, 173, 54));
			}
			DrawLine(g, i ? b : a, kFontUISemi, 12.5f, on ? Color(255, 28, 18, 4) : hover ? kGoldText : Color(255, 200, 200, 204), r, 1);
		}
	}

	// ------------------------------------------------------------------------------------------
	// Scrolling: a part of a page that the wheel (or its thumb) moves
	// ------------------------------------------------------------------------------------------

	std::vector<Scroller*> g_scrollers; // collected by the frame, for the wheel

	// starts drawing a scrolled part: clip to view; returns the content's top (view top - offset)
	float BeginScroll(Gdiplus::Graphics& g, Scroller& s, const RectF& view, const float content, RectF& saved_clip)
	{
		s.view = Moved(view);
		s.max = (std::max)(0.0f, content - view.Height);
		s.target = (std::max)(0.0f, (std::min)(s.target, s.max));
		s.offset = (std::max)(0.0f, (std::min)(s.offset, s.max));
		g_scrollers.push_back(&s);
		saved_clip = g_clip;
		RectF::Intersect(g_clip, g_clip, s.view);
		g.SetClip(g_clip);
		return view.Y - s.offset;
	}

	void EndScroll(Gdiplus::Graphics& g, Scroller& s, const RectF& saved_clip, const std::wstring& key)
	{
		g_clip = saved_clip;
		g.SetClip(g_clip);
		if (s.max <= 0.0f)
		{
			return;
		}
		// the slim thumb on the right
		const RectF view(s.view.X - g_dx, s.view.Y - g_dy, s.view.Width, s.view.Height);
		const float h = (std::max)(24.0f, view.Height * view.Height / (view.Height + s.max));
		const float y = view.Y + (view.Height - h) * (s.offset / s.max);
		const RectF thumb(view.X + view.Width - 7.0f, y, 7.0f, h);
		const bool hover = Hot(RectF(thumb.X - 3.0f, thumb.Y, 10.0f, thumb.Height), key + L"thumb", nullptr, L"", false);
		FillRound(g, thumb, 3.5f, hover || g_launcher.thumb_drag == &s ? Color(85, 255, 255, 255) : Color(51, 255, 255, 255));
	}

	// moves the scrollers towards their targets; true while one moves
	bool StepScrollers(const float dt)
	{
		bool moving = false;
		for (Scroller* s : { &g_launcher.article_scroll, &g_launcher.mod_scroll, &g_launcher.faq_scroll, &g_launcher.team_scroll,
			&g_launcher.credits_scroll, &g_launcher.credits_nav_scroll, &g_launcher.settings_scroll })
		{
			if (s->offset != s->target)
			{
				const float k = (std::min)(1.0f, dt / 70.0f);
				s->offset += (s->target - s->offset) * k;
				if (std::fabs(s->target - s->offset) < 0.5f)
				{
					s->offset = s->target;
				}
				moving = true;
			}
		}
		return moving;
	}

	// ------------------------------------------------------------------------------------------
	// Time, toasts, links
	// ------------------------------------------------------------------------------------------

	ULONGLONG Now()
	{
		return GetTickCount64();
	}

	void After(const ULONGLONG ms, std::function<void()> action)
	{
		g_launcher.later.emplace_back(Now() + ms, std::move(action));
		StartTimer();
	}

	void ShowToast(const std::wstring& text)
	{
		Launcher& l = g_launcher;
		const int id = ++l.toast_id;
		l.toast = text;
		l.toast_fade.Set(1.0f, 200.0f);
		After(3400, [id]()
			{
				if (id == g_launcher.toast_id)
				{
					g_launcher.toast_fade.Set(0.0f, 250.0f);
				}
			});
		StartTimer();
	}

	// only web pages: a link from the news can't start anything else
	void OpenUrl(const std::wstring& url)
	{
		if (url.empty() || !StartsWith(url, L"https://", true))
		{
			return;
		}
		PlaySfx(IDR_SND_CLICK);
		ShellExecuteW(g_launcher.hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	void OpenFolder(const std::wstring& path)
	{
		ShellExecuteW(g_launcher.hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}

	// multiplayer is played with Update 9 and later
	bool HasMultiplayer()
	{
		return g_launcher.md_update.major >= 9;
	}

	// ------------------------------------------------------------------------------------------
	// Pages
	// ------------------------------------------------------------------------------------------

	void MarkNewsRead();
	void ShowUpdate();
	void FindMissingFiles();

	void SetPage(const Page next, const bool quiet = false)
	{
		Launcher& l = g_launcher;
		if (!quiet)
		{
			PlaySfx(next == Page::Play || (next == Page::News && l.page == Page::Article) ? IDR_SND_BACK : IDR_SND_CLICK);
		}
		const bool changed = next != l.page;
		l.page = next;
		l.sheet_open = false;
		l.search_focus = false;
		if (changed && next != Page::Play)
		{
			l.page_fade.Snap(0.0f);
			l.page_fade.Set(1.0f, 240.0f); // the page fades in and moves up a little
		}
		// the art is the PLAY page; the other pages dim it and leave the bottom bar out
		const bool play = next == Page::Play;
		l.art_fade.Set(play ? 1.0f : 0.14f, 300.0f * 0.86f);
		l.play_fade.Set(play ? 1.0f : 0.0f, 240.0f);
		if (next == Page::News || next == Page::Article)
		{
			MarkNewsRead();
		}
		if (next == Page::Settings || next == Page::Play)
		{
			FindMissingFiles(); // SETTINGS lists them; back on PLAY the warning goes once they are there
		}
		ShowUpdate();
		StartTimer();
	}

	// ------------------------------------------------------------------------------------------
	// Updates: the emblem, its window, the bars
	// ------------------------------------------------------------------------------------------

	bool UpdateAvailable()
	{
		const Launcher& l = g_launcher;
		return l.have_latest && l.latest.update > l.md_update && l.settings.check_updates && !l.checking_files;
	}

	void SetEmblem(const Emblem state)
	{
		Launcher& l = g_launcher;
		if (l.emblem != state)
		{
			l.emblem_since = Now();
		}
		l.emblem = state;
		l.red_fade.Set(state == Emblem::Available || state == Emblem::Warning ? 1.0f : 0.0f, 350.0f);
		l.teal_fade.Set(state == Emblem::Verify || state == Emblem::Ready ? 1.0f : 0.0f, 350.0f);
		StartTimer();
	}

	std::wstring MissingText()
	{
		const size_t n = g_launcher.missing_files.size();
		return std::to_wstring(n) + (n == 1 ? L" game file is missing" : L" game files are missing");
	}

	std::wstring OldText()
	{
		const size_t n = g_launcher.old_files.size();
		return std::to_wstring(n) + (n == 1 ? L" old file needs to be removed" : L" old files need to be removed");
	}

	// the most important problem with the installation, or "": the wrong folder, then outdated files, then missing ones
	std::wstring ProblemText()
	{
		const Launcher& l = g_launcher;
		switch (l.place)
		{
		case Place::InsideBase: return L"Movie Duels is inside the base folder";
		case Place::JediOutcast: return L"Movie Duels is installed in Jedi Outcast";
		case Place::NoJediAcademy: return L"Jedi Academy wasn't found next to Movie Duels";
		case Place::Ok: break;
		}
		return !l.old_files.empty() ? OldText() : !l.missing_files.empty() ? MissingText() : L"";
	}

	// something is wrong with the installation (not while the bars check the new files): the emblem warns
	bool FilesMissing()
	{
		return !g_launcher.checking_files && !ProblemText().empty();
	}

	// what the reminder and the tooltip add after the problem
	std::wstring ProblemClick()
	{
		return g_launcher.place != Place::Ok ? L". Click for details." : L". Click to see which.";
	}

	// looks at the installation again without reading the pk3s (milliseconds: each time the launcher opens, on
	// SETTINGS, after a check): the folder it is in, outdated files and missing game files
	void FindMissingFiles()
	{
		Launcher& l = g_launcher;
		l.place = CheckPlace(l.folder);
		l.old_files = FoundOldGameFiles(l.folder, l.old_game_files);
		l.missing_files = MissingGameFiles(l.folder, l.game_files);
	}

	std::wstring EmblemTip()
	{
		const Launcher& l = g_launcher;
		switch (l.emblem)
		{
		case Emblem::Checking: return L"Checking for updates...";
		case Emblem::Available: return L""; // the reminder says it
		case Emblem::Verify: return L"Checking the new files...";
		case Emblem::Ready: return L"Ready!";
		case Emblem::Warning: return ProblemText() + L". Click for details.";
		default:
			return l.settings.check_updates ? L"Update " + l.md_update.Shown() + L": you're up to date. Click to check again."
				: L"Click to check for updates";
		}
	}

	// the emblem from the update state (the rest of ShowUpdate is drawn from the state)
	void ShowUpdate()
	{
		Launcher& l = g_launcher;
		if (l.emblem != Emblem::Checking && l.emblem != Emblem::Ready)
		{
			SetEmblem(l.checking_files ? Emblem::Verify : FilesMissing() ? Emblem::Warning : UpdateAvailable() ? Emblem::Available
				: Emblem::Idle);
		}
	}

	struct LatestResult
	{
		bool ok = false;
		Latest latest;
	};

	void LookForUpdate(const bool asked)
	{
		Launcher& l = g_launcher;
		if (l.looking_for_update)
		{
			return;
		}
		l.looking_for_update = true;
		l.update_asked = asked;
		l.look_started = Now();
		// the gold ring turns while it looks, unless the bars are running (then the emblem is theirs)
		if (!l.checking_files && l.emblem != Emblem::Ready)
		{
			SetEmblem(Emblem::Checking);
		}
		const HWND hwnd = l.hwnd;
		std::thread([hwnd]()
			{
				auto* result = new LatestResult;
				std::wstring json;
				result->ok = GetText(L"latest.json", json) && ParseLatest(json, result->latest);
				if (!PostMessageW(hwnd, kMsgLatest, 0, reinterpret_cast<LPARAM>(result)))
				{
					delete result;
				}
			}).detach();
	}

	void LatestArrived(LatestResult* result)
	{
		Launcher& l = g_launcher;
		const bool ok = result->ok;
		if (ok)
		{
			l.latest = result->latest;
			l.have_latest = true;
		}
		delete result;
		const auto finish = [ok]()
			{
				Launcher& m = g_launcher;
				m.looking_for_update = false;
				if (m.emblem == Emblem::Checking)
				{
					m.emblem = Emblem::Idle;
				}
				ShowUpdate();
				if (UpdateAvailable())
				{
					PlaySfx(IDR_SND_HOVER);
				}
				else if (m.update_asked)
				{
					ShowToast(!ok ? L"Failed to check for updates."
						: L"Update " + m.md_update.Shown() + L" is the latest. You're up to date.");
				}
				Render();
			};
		// the ring shows long enough to be seen
		const ULONGLONG spent = Now() - l.look_started;
		if (l.emblem == Emblem::Checking && spent < 800)
		{
			After(800 - spent, finish);
		}
		else
		{
			finish();
		}
	}

	void OpenArticle(int index);
	int ArticleById(const std::wstring& id)
	{
		for (size_t i = 0; i < g_launcher.articles.size(); i++)
		{
			if (!id.empty() && g_launcher.articles[i].id == id)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	void EmblemClick()
	{
		Launcher& l = g_launcher;
		if (l.emblem == Emblem::Warning)
		{
			SetPage(Page::Settings); // what is wrong and what to do
		}
		else if (l.emblem == Emblem::Available)
		{
			PlaySfx(IDR_SND_CLICK);
			if (l.sheet_open)
			{
				l.sheet_open = false;
			}
			else
			{
				l.sheet_open = true;
				l.sheet_fade.Snap(0.0f);
				l.sheet_fade.Set(1.0f, 220.0f);
				l.chip_dismissed = true;
				StartTimer();
			}
		}
		else if (l.emblem == Emblem::Idle)
		{
			PlaySfx(IDR_SND_CLICK);
			LookForUpdate(true);
		}
	}

	// ---- the bars: they fill from the emblem outwards while the new files are checked ----

	void StartFilesCheck(const bool for_bars)
	{
		Launcher& l = g_launcher;
		l.check_running = true;
		l.check_for_bars = for_bars;
		const HWND hwnd = l.hwnd;
		const std::wstring folder = l.folder;
		const std::vector<std::wstring> game_files = l.game_files;
		const std::vector<std::wstring> old_game_files = l.old_game_files;
		std::thread([hwnd, folder, game_files, old_game_files]() { RunFilesCheck(hwnd, folder, game_files, old_game_files); }).detach();
	}

	// a newer update than last time is installed: check its files once, with the bars
	void CheckNewFiles()
	{
		Launcher& l = g_launcher;
		l.checking_files = true;
		l.bars_done = false;
		l.fill_shown = l.fill_target = 0.0f;
		l.checked_files = l.total_files = 0;
		l.checked_name.clear();
		l.blade = Color(255, 12, 145, 128);
		l.blade_glow = Color(166, 40, 230, 200);
		l.fill_fade.Snap(1.0f);
		l.modes_fade.Snap(0.0f);
		l.have_check = false;
		ShowUpdate();
		StartFilesCheck(true);
		StartTimer();
	}

	void FilesChecked()
	{
		Launcher& l = g_launcher;
		l.bars_done = true;
		const bool unlocked = HasMultiplayer() && l.settings.last_update.major < 9;
		l.settings.last_update = l.md_update;
		SaveSettings();
		l.missing_files = l.last_check.missing;
		l.old_files = l.last_check.old;
		l.place = l.last_check.place;
		const std::wstring problem = ProblemText();
		const bool missing = !problem.empty();
		const size_t broken = l.last_check.broken.size() + l.missing_files.size() + l.old_files.size() + (l.place != Place::Ok ? 1 : 0);
		l.status_left = broken == 0 ? L"UPDATE " + l.md_update.Shown() + L" IS READY"
			: std::to_wstring(broken) + (broken == 1 ? L" FILE" : L" FILES") + L" TO LOOK AT";
		l.status_right = broken == 0 ? L"All files checked" : missing ? problem + L". See Settings" : L"See Settings";
		if (broken > 0)
		{
			l.blade = Color(255, 85, 85, 90);
			l.blade_glow = Color(0, 0, 0, 0);
		}
		else
		{
			SetEmblem(Emblem::Ready);
			l.shock_start = Now();
		}
		PlaySfx(IDR_SND_CLICK);

		// the bars fade, the modes come back
		After(2300, [unlocked, broken, missing]()
			{
				Launcher& m = g_launcher;
				m.fill_fade.Set(0.0f, 500.0f);
				m.checking_files = false;
				if (m.emblem == Emblem::Ready)
				{
					After(3600, []()
						{
							if (g_launcher.emblem == Emblem::Ready)
							{
								g_launcher.emblem = Emblem::Idle;
								ShowUpdate();
							}
						});
				}
				ShowUpdate();
				m.modes_fade.Snap(0.0f);
				m.modes_fade.Set(1.0f, 550.0f);
				const std::wstring n = m.md_update.Shown();
				ShowToast(missing ? ProblemText() + L". Settings shows what to do."
					: broken > 0 ? std::to_wstring(broken) + L" of the new files could not be read. Settings shows which."
					: unlocked ? L"Update " + n + L" is ready. Multiplayer is now available!" : L"Update " + n + L" is ready. Have fun!");
			});
	}

	// the shown fill follows the check, but takes at least 2.5 s, so the bars can be seen filling
	void StepFill(const float dt)
	{
		Launcher& l = g_launcher;
		if (!l.checking_files || l.bars_done)
		{
			return;
		}
		l.fill_shown = (std::min)(l.fill_target, l.fill_shown + dt / 2500.0f);
		l.status_left = L"CHECKING THE NEW FILES   " + std::to_wstring(static_cast<int>(l.fill_shown * 100)) + L" %";
		l.status_right = !l.checked_name.empty() ? std::wstring(kModFolder) + L"/" + l.checked_name
			: l.total_files > 0 ? std::to_wstring(l.checked_files) + L" of " + std::to_wstring(l.total_files) + L" files" : L"";
		if (l.fill_shown >= 1.0f && l.have_check)
		{
			FilesChecked();
		}
	}

	void CheckProgress(const int done, std::pair<int, std::wstring>* info)
	{
		Launcher& l = g_launcher;
		const int total = info->first;
		const std::wstring name = info->second;
		delete info;
		if (l.check_for_bars)
		{
			l.checked_files = done;
			l.total_files = total;
			l.checked_name = name;
			l.fill_target = total > 0 ? static_cast<float>(done) / total : 1.0f;
		}
		else
		{
			l.settings_check_bar = total > 0 ? static_cast<float>(done) / total : 1.0f;
			l.settings_check_text = L"CHECKING FILES  " + std::to_wstring(total > 0 ? done * 100 / total : 100) + L" %   \x00B7   "
				+ (name.empty() ? std::to_wstring(total) + L" of " + std::to_wstring(total) : name);
		}
	}

	void CheckDone(CheckResult* result)
	{
		Launcher& l = g_launcher;
		l.last_check = *result;
		delete result;
		l.have_check = true;
		l.check_running = false;
		if (l.check_for_bars)
		{
			l.fill_target = 1.0f;
			return;
		}
		const auto finish = []()
			{
				Launcher& m = g_launcher;
				m.settings_check_bar = 1.0f;
				const size_t broken = m.last_check.broken.size();
				const size_t missing = m.last_check.missing.size();
				std::wstring list;
				for (const std::wstring& file : m.last_check.missing)
				{
					list += (list.empty() ? L"" : L", ") + file;
				}
				std::wstring old;
				for (const std::wstring& file : m.last_check.old)
				{
					old += (old.empty() ? L"" : L", ") + file;
				}
				const size_t olds = m.last_check.old.size();
				m.settings_check_text = m.last_check.place == Place::InsideBase
					? L"Movie Duels is inside the base folder. Move the launcher, the game exes and the MD folder into GameData."
					: m.last_check.place == Place::JediOutcast
					? L"Movie Duels is installed in Jedi Outcast. Install it into Jedi Academy's GameData folder."
					: !m.last_check.jedi_academy ? L"Jedi Academy was not found next to the MD folder."
					: olds > 0 ? L"Remove " + std::wstring(olds == 1 ? L"this old file" : L"these old files") + L" from the MD folder: " + old
					+ L". They clash with the current version."
					: missing > 0 ? std::to_wstring(missing) + (missing == 1 ? L" game file is" : L" game files are") + L" missing: " + list
					+ L". Install the update again from ModDB."
					: broken > 0 ? std::to_wstring(broken) + (broken == 1 ? L" file" : L" files") + L" can't be read. Get them again from ModDB."
					: L"Everything is installed: all " + std::to_wstring(m.game_files.size()) + L" game files found and readable.";
				m.missing_files = m.last_check.missing;
				m.old_files = m.last_check.old;
				m.place = m.last_check.place;
				PlaySfx(IDR_SND_CLICK);
				ShowUpdate();
				Render();
			};
		const ULONGLONG spent = Now() - l.settings_check_started;
		if (spent < 1000)
		{
			After(1000 - spent, finish);
		}
		else
		{
			finish();
		}
	}

	void CheckInstallation()
	{
		Launcher& l = g_launcher;
		if (l.checking_files)
		{
			ShowToast(L"The new files are being checked already.");
			return;
		}
		if (l.check_running)
		{
			return;
		}
		PlaySfx(IDR_SND_CLICK);
		l.settings_check_shown = true;
		l.settings_check_bar = 0.0f;
		l.settings_check_text = L"Checking the files...";
		l.settings_check_started = Now();
		StartFilesCheck(false);
	}

	// ------------------------------------------------------------------------------------------
	// NEWS
	// ------------------------------------------------------------------------------------------

	struct TextResult
	{
		bool ok = false;
		int request = 0;
		std::wstring text;
	};

	struct ImageResult
	{
		std::wstring path;
		std::string bytes;
	};

	std::set<std::wstring> SeenNews()
	{
		std::set<std::wstring> seen;
		size_t start = 0;
		const std::wstring& s = g_launcher.settings.seen_news;
		while (start < s.size())
		{
			size_t end = s.find(L',', start);
			if (end == std::wstring::npos)
			{
				end = s.size();
			}
			if (end > start)
			{
				seen.insert(s.substr(start, end - start));
			}
			start = end + 1;
		}
		return seen;
	}

	int UnreadNews()
	{
		const std::set<std::wstring> seen = SeenNews();
		int unread = 0;
		for (const Article& a : g_launcher.articles)
		{
			unread += seen.count(a.id) ? 0 : 1;
		}
		return unread;
	}

	void MarkNewsRead()
	{
		Launcher& l = g_launcher;
		if (l.articles.empty() || UnreadNews() == 0)
		{
			return;
		}
		// keep the ids that are still published, so the list doesn't grow forever
		std::wstring ids;
		for (const Article& a : l.articles)
		{
			ids += (ids.empty() ? L"" : L",") + a.id;
		}
		l.settings.seen_news = ids;
		SaveSettings();
	}

	// the pictures, one after the other, in their own thread
	void LoadPictures(const std::vector<std::wstring>& paths)
	{
		Launcher& l = g_launcher;
		std::vector<std::wstring> wanted;
		for (const std::wstring& p : paths)
		{
			if (!p.empty() && !l.pictures_asked.count(p))
			{
				l.pictures_asked.insert(p);
				wanted.push_back(p);
			}
		}
		if (wanted.empty())
		{
			return;
		}
		const HWND hwnd = l.hwnd;
		std::thread([hwnd, wanted]()
			{
				for (const std::wstring& path : wanted)
				{
					auto* result = new ImageResult;
					result->path = path;
					GetBytes(path, result->bytes, true);
					if (!PostMessageW(hwnd, kMsgImage, 0, reinterpret_cast<LPARAM>(result)))
					{
						delete result;
					}
				}
			}).detach();
	}

	void LoadNews()
	{
		Launcher& l = g_launcher;
		if (l.news_loading)
		{
			return;
		}
		l.news_loading = true;
		l.news_failed = false;
		const HWND hwnd = l.hwnd;
		std::thread([hwnd]()
			{
				auto* result = new TextResult;
				result->ok = GetText(L"news.json", result->text);
				if (!PostMessageW(hwnd, kMsgNews, 0, reinterpret_cast<LPARAM>(result)))
				{
					delete result;
				}
			}).detach();
	}

	void NewsArrived(TextResult* result)
	{
		Launcher& l = g_launcher;
		l.news_loading = false;
		l.news_loaded = true;
		l.news_failed = !result->ok;
		l.articles = ParseNews(result->text);
		delete result;
		const std::set<std::wstring> seen = SeenNews();
		l.fresh_news.clear();
		for (const Article& a : l.articles)
		{
			if (!seen.count(a.id))
			{
				l.fresh_news.insert(a.id);
			}
		}
		if (l.page == Page::News)
		{
			MarkNewsRead();
		}
		std::vector<std::wstring> images;
		for (const Article& a : l.articles)
		{
			images.push_back(a.image);
		}
		LoadPictures(images);
		ShowUpdate(); // "What's new" in the update window needs the articles
		StartTimer();  // the NEWS button flashes for unread articles
	}

	float NewsMax()
	{
		const size_t n = g_launcher.articles.size();
		const float content = n == 0 ? 0.0f : 28.0f + n * kCardStep - 18.0f + 10.0f;
		return (std::max)(0.0f, content - 1080.0f);
	}

	float NearestCard(const float offset)
	{
		const float max = NewsMax();
		float best = 0.0f;
		for (size_t i = 0; i < g_launcher.articles.size(); i++)
		{
			const float stop = (std::min)(max, i * kCardStep);
			if (std::fabs(stop - offset) < std::fabs(best - offset))
			{
				best = stop;
			}
		}
		return best;
	}

	void ScrollNewsTo(const float offset)
	{
		Launcher& l = g_launcher;
		l.news_from = l.news_scroll.offset;
		l.news_to = offset;
		l.news_anim_start = Now();
		l.news_animating = true;
		StartTimer();
	}

	void StepNewsScroll()
	{
		Launcher& l = g_launcher;
		if (!l.news_animating)
		{
			return;
		}
		const float q = (std::min)(1.0f, static_cast<float>(Now() - l.news_anim_start) / 340.0f);
		const float ease = 1.0f - std::pow(1.0f - q, 3.0f);
		l.news_scroll.offset = l.news_from + (l.news_to - l.news_from) * ease;
		if (q >= 1.0f)
		{
			l.news_animating = false;
		}
	}

	void StepNews(const int direction)
	{
		Launcher& l = g_launcher;
		const float from = l.news_animating ? l.news_to : l.news_scroll.offset;
		const float to = NearestCard(from + direction * kCardStep);
		if (std::fabs(to - from) < 1.0f)
		{
			return;
		}
		PlaySfx(IDR_SND_HOVER);
		ScrollNewsTo(to);
	}

	// ---- an article: its HTML, simplified. Only the simple parts are used, so an article is quick to write and
	// nothing in it can run: <h2> <h3> <p> <p class="note"> <ul>/<ol> <li> <b>/<strong> <i>/<em> <a href> <br>
	// <img src>. Other tags are skipped (their text stays), <script> and <style> are dropped with their content.

	std::wstring HtmlDecode(const std::wstring& s)
	{
		std::wstring out;
		for (size_t i = 0; i < s.size(); i++)
		{
			if (s[i] != L'&')
			{
				out += s[i];
				continue;
			}
			const size_t semi = s.find(L';', i);
			if (semi == std::wstring::npos || semi - i > 10)
			{
				out += s[i];
				continue;
			}
			const std::wstring name = s.substr(i + 1, semi - i - 1);
			wchar_t c = 0;
			if (name == L"amp") c = L'&';
			else if (name == L"lt") c = L'<';
			else if (name == L"gt") c = L'>';
			else if (name == L"quot") c = L'"';
			else if (name == L"apos") c = L'\'';
			else if (name == L"nbsp") c = L' ';
			else if (name == L"mdash") c = 0x2014;
			else if (name == L"ndash") c = 0x2013;
			else if (name == L"hellip") c = 0x2026;
			else if (name == L"rsquo") c = 0x2019;
			else if (name == L"lsquo") c = 0x2018;
			else if (name == L"rdquo") c = 0x201D;
			else if (name == L"ldquo") c = 0x201C;
			else if (name == L"middot") c = 0x00B7;
			else if (!name.empty() && name[0] == L'#')
			{
				c = static_cast<wchar_t>(name.size() > 1 && (name[1] == L'x' || name[1] == L'X') ? wcstoul(name.c_str() + 2, nullptr, 16)
					: wcstoul(name.c_str() + 1, nullptr, 10));
			}
			if (!c)
			{
				out += s[i];
				continue;
			}
			out += c;
			i = semi;
		}
		return out;
	}

	std::wstring HtmlAttribute(const std::wstring& attrs, const wchar_t* name)
	{
		static const std::wregex attr(L"([a-zA-Z-]+)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))");
		for (std::wsregex_iterator it(attrs.begin(), attrs.end(), attr), end; it != end; ++it)
		{
			if (!_wcsicmp((*it)[1].str().c_str(), name))
			{
				const std::wstring v = (*it)[2].matched ? (*it)[2].str() : (*it)[3].matched ? (*it)[3].str() : (*it)[4].str();
				return Trim(HtmlDecode(v));
			}
		}
		return L"";
	}

	const Color kArticleText(255, 220, 220, 223);

	// the id of a YouTube video address (embed/, watch?v=, shorts/, youtu.be/; www., m., youtube-nocookie), or ""
	std::wstring YouTubeId(const std::wstring& src)
	{
		static const std::wregex youtube(L"^https://(?:www\\.|m\\.)?(?:youtube(?:-nocookie)?\\.com/(?:embed/|watch\\?v=|shorts/)|youtu\\.be/)([A-Za-z0-9_-]{11})",
			std::regex::icase);
		std::wsmatch m;
		return std::regex_search(src, m, youtube) ? m[1].str() : L"";
	}

	std::wstring YouTubePicture(const std::wstring& id)
	{
		return L"https://img.youtube.com/vi/" + id + L"/maxresdefault.jpg";
	}

	std::wstring YouTubeWatch(const std::wstring& id)
	{
		return L"https://www.youtube.com/watch?v=" + id;
	}

	// the article's blocks from its HTML; the text is laid out later (LayoutArticle)
	std::vector<ArticleBlock> ParseArticle(const std::wstring& html)
	{
		std::vector<ArticleBlock> blocks;
		ArticleBlock* block = nullptr;
		std::vector<int> lists; // >0: the next number of an <ol>, 0: <ul>
		bool bold = false, italic = false;
		std::wstring link;
		std::wstring skip;
		const auto new_block = [&](const ArticleBlock::Kind kind) -> ArticleBlock*
			{
				blocks.push_back(ArticleBlock());
				ArticleBlock* b = &blocks.back();
				b->kind = kind;
				b->list_depth = static_cast<int>(lists.size());
				if (kind == ArticleBlock::kItem && !lists.empty() && lists.back() > 0)
				{
					b->number = lists.back()++;
				}
				bold = italic = false;
				link.clear();
				return b;
			};
		const auto add_text = [&](std::wstring text)
			{
				if (!block)
				{
					if (Trim(text).empty())
					{
						return; // the space between blocks
					}
					block = new_block(lists.empty() ? ArticleBlock::kText : ArticleBlock::kItem);
				}
				if (block->text.runs.empty())
				{
					const size_t first = text.find_first_not_of(L' ');
					text = first == std::wstring::npos ? L"" : text.substr(first);
				}
				if (text.empty())
				{
					return;
				}
				TextRun r;
				r.text = text;
				const bool heading = block->kind == ArticleBlock::kHeading2 || block->kind == ArticleBlock::kHeading3;
				r.font = heading ? kFontLabel : bold ? kFontUIBold : kFontUI;
				r.size = block->kind == ArticleBlock::kHeading2 ? 24.0f : block->kind == ArticleBlock::kHeading3 ? 19.0f
					: block->kind == ArticleBlock::kNote ? 13.0f : 15.0f;
				r.color = heading ? kWhiteText : kArticleText;
				r.italic = italic;
				if (!link.empty())
				{
					r.link = link;
					r.color = kGoldBright;
				}
				block->text.runs.push_back(r);
			};

		static const std::wregex token(L"<!--[\\s\\S]*?-->|<(/?)([a-zA-Z][a-zA-Z0-9]*)([^>]*)>|([^<]+)");
		for (std::wsregex_iterator it(html.begin(), html.end(), token), end; it != end; ++it)
		{
			const std::wsmatch& m = *it;
			if (StartsWith(m.str(), L"<!--"))
			{
				continue;
			}
			const bool closing = m[1].length() > 0;
			std::wstring tag = m[2].str();
			CharLowerBuffW(tag.empty() ? nullptr : &tag[0], static_cast<DWORD>(tag.size()));
			if (!skip.empty())
			{
				if (closing && tag == skip)
				{
					skip.clear();
				}
				continue;
			}
			if (m[4].matched)
			{
				// whitespace collapses to one space
				std::wstring text, raw = HtmlDecode(m[4].str());
				for (const wchar_t c : raw)
				{
					const bool space = c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
					if (space)
					{
						if (text.empty() || text.back() != L' ')
						{
							text += L' ';
						}
					}
					else
					{
						text += c;
					}
				}
				add_text(text);
				continue;
			}
			const std::wstring attrs = m[3].str();
			if (tag == L"script" || tag == L"style")
			{
				if (!closing && (attrs.empty() || attrs.back() != L'/'))
				{
					skip = tag;
				}
			}
			else if (tag == L"p" || tag == L"div" || tag == L"h1" || tag == L"h2" || tag == L"h3" || tag == L"h4")
			{
				if (closing)
				{
					block = nullptr;
				}
				else
				{
					ArticleBlock::Kind kind = tag == L"h1" || tag == L"h2" ? ArticleBlock::kHeading2
						: tag == L"h3" || tag == L"h4" ? ArticleBlock::kHeading3 : ArticleBlock::kText;
					if (tag == L"p" && HtmlAttribute(attrs, L"class").find(L"note") != std::wstring::npos)
					{
						kind = ArticleBlock::kNote;
					}
					if (kind == ArticleBlock::kText && !lists.empty())
					{
						kind = ArticleBlock::kItem;
					}
					block = new_block(kind);
				}
			}
			else if (tag == L"ul" || tag == L"ol")
			{
				if (closing)
				{
					if (!lists.empty())
					{
						lists.pop_back();
					}
				}
				else
				{
					lists.push_back(tag == L"ol" ? 1 : 0);
				}
				block = nullptr;
			}
			else if (tag == L"li")
			{
				block = closing ? nullptr : new_block(ArticleBlock::kItem);
			}
			else if (tag == L"br")
			{
				add_text(L"\n");
			}
			else if (tag == L"b" || tag == L"strong")
			{
				bold = !closing;
			}
			else if (tag == L"i" || tag == L"em")
			{
				italic = !closing;
			}
			else if (tag == L"a")
			{
				const std::wstring href = closing ? L"" : HtmlAttribute(attrs, L"href");
				link = StartsWith(href, L"https://", true) ? href : L"";
			}
			else if (tag == L"img" && !closing)
			{
				const std::wstring src = HtmlAttribute(attrs, L"src");
				if (!src.empty())
				{
					ArticleBlock* b = new_block(ArticleBlock::kImage);
					b->image = src;
					block = nullptr;
				}
			}
			else if (tag == L"iframe" && !closing)
			{
				// a YouTube embed (the code of YouTube's Share button): the video's picture with a play button, a click
				// opens it in the browser (playing it here would need a browser engine). Other iframes: a link.
				const std::wstring src = HtmlAttribute(attrs, L"src");
				const std::wstring id = YouTubeId(src);
				if (!id.empty())
				{
					ArticleBlock* b = new_block(ArticleBlock::kVideo);
					b->video = id;
					b->image = YouTubePicture(id);
					const int width = _wtoi(HtmlAttribute(attrs, L"width").c_str());
					b->video_width = static_cast<float>((std::max)(240, (std::min)(640, width > 0 ? width : 480)));
					block = nullptr;
				}
				else if (StartsWith(src, L"https://", true))
				{
					block = new_block(ArticleBlock::kText);
					link = src;
					add_text(L"Open the embedded page \x2197");
					block = nullptr;
				}
			}
		}
		if (blocks.empty())
		{
			blocks.push_back(ArticleBlock());
			blocks.back().text.runs.push_back(Run(L"This article is empty.", kFontUI, 15.0f, kArticleText));
		}
		return blocks;
	}

	constexpr float kArticleWidth = 1014.0f - 96.0f; // the card less its padding (48 each side)

	// lays the blocks out one under the other (pictures: their size, at most 320 high)
	void LayoutArticle()
	{
		Launcher& l = g_launcher;
		float y = 0.0f;
		for (ArticleBlock& b : l.article_blocks)
		{
			float top = 0.0f, bottom = 10.0f, indent = 0.0f;
			switch (b.kind)
			{
			case ArticleBlock::kHeading2:
			case ArticleBlock::kHeading3:
				top = 6.0f;
				bottom = 6.0f;
				LayoutText(b.text, kArticleWidth, b.kind == ArticleBlock::kHeading2 ? 32.0f : 26.0f);
				break;
			case ArticleBlock::kNote:
				LayoutText(b.text, kArticleWidth - 24.0f, 19.5f);
				b.text.height += 16.0f; // its padding
				break;
			case ArticleBlock::kItem:
				indent = 22.0f * (std::max)(1, b.list_depth);
				bottom = 4.0f;
				LayoutText(b.text, kArticleWidth - indent, 22.5f);
				break;
			case ArticleBlock::kImage:
			{
				top = 4.0f;
				bottom = 12.0f;
				const auto found = l.pictures.find(b.image);
				float h = 0.0f;
				if (found != l.pictures.end() && found->second)
				{
					const float iw = static_cast<float>(found->second->GetWidth()), ih = static_cast<float>(found->second->GetHeight());
					const float k = (std::min)({ 1.0f, kArticleWidth / iw, 320.0f / ih });
					h = ih * k;
				}
				b.text.height = h;
				if (h == 0.0f)
				{
					top = bottom = 0.0f;
				}
				break;
			}
			case ArticleBlock::kVideo:
				top = 4.0f;
				bottom = 14.0f;
				b.text.height = std::round(b.video_width * 9.0f / 16.0f); // 16:9, shown also before (or without) its picture
				break;
			default:
				LayoutText(b.text, kArticleWidth, 22.5f);
				break;
			}
			b.y = y + top;
			b.h = b.text.height;
			y = b.y + b.h + bottom;
		}
		l.article_height = y;
	}

	void OpenArticle(const int index)
	{
		Launcher& l = g_launcher;
		if (index < 0 || index >= static_cast<int>(l.articles.size()))
		{
			return;
		}
		l.article_index = index;
		l.article_blocks.clear();
		l.article_height = 0.0f;
		l.article_loading = true;
		l.article_failed = false;
		l.article_scroll.offset = l.article_scroll.target = 0.0f;
		const int request = ++l.article_request;
		SetPage(Page::Article, true);
		const std::wstring file = l.articles[index].file;
		const HWND hwnd = l.hwnd;
		std::thread([hwnd, file, request]()
			{
				auto* result = new TextResult;
				result->request = request;
				result->ok = !file.empty() && GetText(file, result->text);
				if (!PostMessageW(hwnd, kMsgArticle, 0, reinterpret_cast<LPARAM>(result)))
				{
					delete result;
				}
			}).detach();
	}

	void ArticleArrived(TextResult* result)
	{
		Launcher& l = g_launcher;
		if (result->request != l.article_request || l.article_index < 0)
		{
			delete result; // another article was opened meanwhile
			return;
		}
		l.article_loading = false;
		if (!result->ok)
		{
			l.article_failed = true;
			ArticleBlock summary, failed;
			summary.text.runs.push_back(Run(l.articles[l.article_index].summary, kFontUI, 15.0f, kArticleText));
			failed.text.runs.push_back(Run(L"The full article could not be loaded. Is the PC online?", kFontUI, 15.0f, kDimText));
			l.article_blocks = { summary, failed };
		}
		else
		{
			l.article_blocks = ParseArticle(result->text);
			std::vector<std::wstring> images;
			for (const ArticleBlock& b : l.article_blocks)
			{
				if (b.kind == ArticleBlock::kImage || b.kind == ArticleBlock::kVideo)
				{
					images.push_back(b.image);
				}
			}
			LoadPictures(images);
		}
		delete result;
		LayoutArticle();
	}

	void ImageArrived(ImageResult* result)
	{
		Launcher& l = g_launcher;
		l.pictures[result->path] = DecodePicture(result->bytes);
		delete result;
		if (!l.article_blocks.empty())
		{
			LayoutArticle();
		}
	}

	// ------------------------------------------------------------------------------------------
	// Starting the game
	// ------------------------------------------------------------------------------------------

	bool StartGame(const HWND hwnd, const bool multiplayer)
	{
		const std::wstring exe = g_launcher.folder + L"\\" + (multiplayer ? kMPExe : kSPExe);
		if (!PathFileExistsW(exe.c_str()))
		{
			const std::wstring msg = L"Could not find " + exe + L"\n\nThe launcher must be in the MovieDuels game folder.";
			MessageBoxW(hwnd, msg.c_str(), L"MovieDuels", MB_OK | MB_ICONERROR);
			return false;
		}

		// "+set" on the command line overrides the values in the configs (they are the configs' own values unless
		// SETTINGS changed them, and then it wrote them into the configs too)
		std::wstring cmd = L"\"" + exe + L"\"";
		for (const auto& kv : GameConfigValues(multiplayer))
		{
			cmd += L" +set " + std::wstring(kv.first.begin(), kv.first.end()) + L" " + std::wstring(kv.second.begin(), kv.second.end());
		}

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

	// SINGLEPLAYER / Start multiplayer: start the game and close
	void Play(const bool multiplayer)
	{
		PlaySfx(IDR_SND_CLICK, true); // the launcher closes right after
		if (StartGame(g_launcher.hwnd, multiplayer))
		{
			DestroyWindow(g_launcher.hwnd);
		}
	}

	void MusicToggle()
	{
		Launcher& l = g_launcher;
		l.settings.music = !l.settings.music;
		SaveSettings();
		PlaySfx(IDR_SND_CLICK);
		if (l.settings.music)
		{
			StartMusic(l.hwnd);
		}
		else
		{
			StopMusic();
		}
	}

	// an option changed: remember it (renderer, display, resolution, controller also go into the game's configs)
	void OptionChanged(const bool game_config)
	{
		PlaySfx(IDR_SND_CLICK);
		SaveSettings();
		if (game_config)
		{
			SaveGameConfig();
		}
	}

	// what the launcher does once it is open: check the files of a newly installed update, look for an update and
	// for news
	void StartUp()
	{
		Launcher& l = g_launcher;
		if (l.settings.last_update > UpdateNumber() && l.md_update > l.settings.last_update)
		{
			CheckNewFiles();
		}
		else if (l.settings.last_update != l.md_update)
		{
			l.settings.last_update = l.md_update; // the first run: nothing to compare with
			SaveSettings();
		}
		if (l.settings.check_updates)
		{
			LookForUpdate(false);
		}
		LoadNews();
	}

	// ------------------------------------------------------------------------------------------
	// PLAY: the bars, the modes, the emblem
	// ------------------------------------------------------------------------------------------

	float EaseInOut(const float q)
	{
		return q * q * (3.0f - 2.0f * q);
	}

	// draws an image over r, filling it and cut to it (WPF Stretch UniformToFill), at an opacity
	void DrawCover(Gdiplus::Graphics& g, Gdiplus::Bitmap* image, const RectF& r_in, const float opacity, const bool top = false)
	{
		if (!image)
		{
			return;
		}
		const RectF r = Moved(r_in);
		const float iw = static_cast<float>(image->GetWidth()), ih = static_cast<float>(image->GetHeight());
		const float k = (std::max)(r.Width / iw, r.Height / ih);
		const float sw = r.Width / k, sh = r.Height / k;
		const float sx = (iw - sw) / 2.0f, sy = top ? 0.0f : (ih - sh) / 2.0f;
		Gdiplus::ColorMatrix cm = {
			1, 0, 0, 0, 0,
			0, 1, 0, 0, 0,
			0, 0, 1, 0, 0,
			0, 0, 0, opacity * g_alpha, 0,
			0, 0, 0, 0, 1 };
		Gdiplus::ImageAttributes attributes;
		attributes.SetColorMatrix(&cm);
		attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
		g.DrawImage(image, r, sx, sy, sw, sh, Gdiplus::UnitPixel, &attributes);
	}

	// the fill of the bars while the new files are checked: a lightsaber blade from the emblem outwards, cut to the
	// bars' shape
	void DrawFill(Gdiplus::Graphics& g, const float opacity)
	{
		Launcher& l = g_launcher;
		if (opacity <= 0.0f || l.bar_mask.empty())
		{
			return;
		}
		const float p = (std::max)(0.0f, (std::min)(1.0f, l.fill_shown));
		const float left_w = (kBarLeftEnd - kBarLeft) * p, right_w = (kBarRightEnd - kBarRight) * p;
		// the glow under the blades
		if (l.blade_glow.GetA())
		{
			g_alpha = opacity;
			SoftRound(g, RectF(kBarLeftEnd - left_w, 547.0f, left_w, 22.0f), 11.0f, Alpha(l.blade_glow, 0.6f), 22.0f);
			SoftRound(g, RectF(kBarRight, 547.0f, right_w, 22.0f), 11.0f, Alpha(l.blade_glow, 0.6f), 22.0f);
			g_alpha = 1.0f;
		}
		const Gdiplus::Rect& br = l.bar_rect;
		Gdiplus::Bitmap blades(br.Width, br.Height, PixelFormat32bppPARGB);
		Gdiplus::Rect rect(0, 0, br.Width, br.Height);
		Gdiplus::BitmapData data = {};
		if (blades.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &data) != Gdiplus::Ok)
		{
			return;
		}
		if (!data.Scan0)
		{
			blades.UnlockBits(&data);
			return;
		}
		const float s = l.scale;
		const int lx0 = static_cast<int>((kBarLeftEnd - left_w - kBarLeft) * s), lx1 = static_cast<int>((kBarLeftEnd - kBarLeft) * s);
		const int rx0 = static_cast<int>((kBarRight - kBarLeft) * s), rx1 = static_cast<int>((kBarRight + right_w - kBarLeft) * s);
		const Color c = l.blade;
		const Color white(255, 255, 255, 255);
		for (int y = 0; y < br.Height; y++)
		{
			// the blade's colour with a dark edge and a bright middle line
			const float v = static_cast<float>(y) / (std::max)(1, br.Height - 1);
			Color col;
			if (v < 0.32f)
			{
				col = Mix(Color(255, static_cast<BYTE>(c.GetR() * 0.62f), static_cast<BYTE>(c.GetG() * 0.62f), static_cast<BYTE>(c.GetB() * 0.62f)), c, v / 0.32f);
			}
			else if (v < 0.5f)
			{
				col = Mix(c, Mix(c, white, 0.3f), (v - 0.32f) / 0.18f);
			}
			else if (v < 0.68f)
			{
				col = Mix(Mix(c, white, 0.3f), c, (v - 0.5f) / 0.18f);
			}
			else
			{
				col = Mix(c, Color(255, static_cast<BYTE>(c.GetR() * 0.62f), static_cast<BYTE>(c.GetG() * 0.62f), static_cast<BYTE>(c.GetB() * 0.62f)), (v - 0.68f) / 0.32f);
			}
			BYTE* row = static_cast<BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
			for (int x = 0; x < br.Width; x++)
			{
				const bool in = (x >= lx0 && x < lx1) || (x >= rx0 && x < rx1);
				const float a = in ? l.bar_mask[static_cast<size_t>(y) * br.Width + x] / 255.0f * opacity : 0.0f;
				row[x * 4 + 0] = static_cast<BYTE>(col.GetB() * a);
				row[x * 4 + 1] = static_cast<BYTE>(col.GetG() * a);
				row[x * 4 + 2] = static_cast<BYTE>(col.GetR() * a);
				row[x * 4 + 3] = static_cast<BYTE>(255 * a);
			}
		}
		blades.UnlockBits(&data);
		DrawPixels(g, &blades, br.X, br.Y);
	}

	void DrawRing(Gdiplus::Graphics& g, const float cx, const float cy, const float radius, const Color& c, const float width)
	{
		const Gdiplus::Pen pen(Alpha(c, g_alpha), width);
		g.DrawEllipse(&pen, cx - radius + g_dx, cy - radius + g_dy, radius * 2, radius * 2);
	}

	void DrawEmblem(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		const float cx = 544.0f, cy = 558.0f;
		const float t = static_cast<float>(Now() - l.emblem_since) / 1000.0f;
		const bool slow = l.emblem == Emblem::Available || l.emblem == Emblem::Warning;
		if (slow || (l.emblem == Emblem::Ready && t < 3.6f))
		{
			// the pulse (a soft ring), red: slow and forever; missing files: the same in amber, without ripples; ready:
			// three quick teal ones
			const float period = slow ? 1.8f : 1.2f;
			const float phase = std::fmod(t, period) / period;
			const float pulse = 0.2f + 0.8f * (0.5f - 0.5f * std::cos(phase * 6.2831853f));
			const Color pc = l.emblem == Emblem::Warning ? Color(230, 255, 170, 40)
				: l.emblem == Emblem::Available ? Color(217, 255, 40, 50) : Color(217, 50, 240, 210);
			for (int i = 0; i < 4; i++)
			{
				DrawRing(g, cx, cy, 34.5f + i * 1.6f, Alpha(pc, pulse * (0.55f - i * 0.12f)), 2.5f);
			}
			if (l.emblem == Emblem::Available)
			{
				// two ripples
				for (int i = 0; i < 2; i++)
				{
					const float rt = t - i * 0.9f;
					if (rt < 0.0f)
					{
						continue;
					}
					const float q = std::fmod(rt, 1.8f) / 1.8f;
					DrawRing(g, cx, cy, 39.0f * (1.0f + 0.55f * q), Alpha(Color(204, 255, 70, 80), 0.75f * (1.0f - q)), 2.0f);
				}
			}
		}
		if (l.emblem == Emblem::Checking)
		{
			// a gold arc turns while the launcher looks for an update
			const Gdiplus::Pen pen(Alpha(kGoldText, g_alpha), 2.5f);
			Gdiplus::Pen round(Alpha(kGoldText, g_alpha), 2.5f);
			round.SetStartCap(Gdiplus::LineCapRound);
			round.SetEndCap(Gdiplus::LineCapRound);
			const float angle = std::fmod(t * 360.0f, 360.0f);
			g.DrawArc(&round, cx - 42.0f + g_dx, cy - 42.0f + g_dy, 84.0f, 84.0f, angle - 90.0f, 70.0f);
		}
		if (l.shock_start)
		{
			const float q = static_cast<float>(Now() - l.shock_start) / 950.0f;
			if (q < 1.0f)
			{
				DrawRing(g, cx, cy, 40.0f * (0.9f + 4.6f * q), Alpha(Color(255, 47, 214, 191), 1.0f - q), 3.0f);
			}
		}
		Hot(RectF(504.0f, 518.0f, 80.0f, 80.0f), L"emblem", EmblemClick, EmblemTip());
	}

	void DrawPlayControls(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		const bool play = l.page == Page::Play;
		if (l.checking_files)
		{
			// what the bars show while the new files are checked
			if (l.checking_files)
			{
				DrawLine(g, l.status_left, kFontUISemi, 14.0f, kWhiteText, RectF(140.0f, 549.0f, 360.0f, 20.0f), 1, true);
				DrawLine(g, l.status_right, kFontUI, 13.0f, Color(235, 249, 249, 249), RectF(588.0f, 550.0f, 360.0f, 20.0f), 1, true);
			}
		}
		if (!l.checking_files)
		{
			const float a = g_alpha;
			g_alpha *= l.modes_fade.value;
			if (play)
			{
				const bool sp = Hot(RectF(140.0f, 544.0f, 360.0f, 28.0f), L"single", []() { Play(false); });
				const bool mp = Hot(RectF(588.0f, 544.0f, 360.0f, 28.0f), L"multi", []()
					{
						PlaySfx(IDR_SND_CLICK);
						if (!HasMultiplayer())
						{
							ShowToast(L"Multiplayer is coming soon.");
							return;
						}
						SetPage(Page::Multiplayer, true);
					}, HasMultiplayer() ? L"" : L"Multiplayer is unavailable.");
				if (sp)
				{
					l.multiplayer_side = false;
				}
				else if (mp)
				{
					l.multiplayer_side = true;
				}
				DrawLabel(g, L"SINGLEPLAYER", RectF(140.0f, 544.0f, 360.0f, 28.0f), 20.0f, sp, IsDown(L"single"));
				if (HasMultiplayer())
				{
					DrawLabel(g, L"MULTIPLAYER", RectF(588.0f, 544.0f, 360.0f, 28.0f), 20.0f, mp, IsDown(L"multi"));
				}
				else
				{
					// a lock before the word
					const float w = TextWidth(kFontLabel, 20.0f, L"MULTIPLAYER");
					const float x0 = 588.0f + (360.0f - w - 18.0f) / 2.0f;
					DrawLabel(g, L"MULTIPLAYER", RectF(x0 + 18.0f, 544.0f, w, 28.0f), 20.0f, mp, IsDown(L"multi"));
					const Color lc = Alpha(mp ? kGoldText : kWhiteText, g_alpha);
					const Gdiplus::Pen pen(lc, 1.6f);
					const float lx = x0 + g_dx, ly = 552.0f + g_dy;
					g.DrawArc(&pen, lx + 2.0f, ly, 7.0f, 7.0f, 180.0f, 180.0f);
					const Gdiplus::SolidBrush brush(lc);
					g.FillRectangle(&brush, lx + 0.5f, ly + 4.5f, 10.0f, 7.5f);
				}
			}
			g_alpha = a;
		}
		if (play)
		{
			DrawEmblem(g);
			// the reminder above the emblem: missing files (amber, always) or a new update (red, until its window was opened)
			const bool missing = FilesMissing();
			if ((missing || (UpdateAvailable() && !l.chip_dismissed)) && !l.sheet_open)
			{
				const std::wstring text = missing ? ProblemText() + ProblemClick() : l.latest.title + L" is out! Click to see what's new.";
				const float w = TextWidth(kFontUISemi, 13.0f, text) + 28.0f;
				const RectF r(544.0f - w / 2.0f, 484.0f, w, 28.0f);
				Hot(r, L"chip", EmblemClick);
				if (missing)
				{
					SoftRound(g, r, 14.0f, Color(153, 224, 138, 26), 14.0f);
					FillRoundGradient(g, r, 14.0f, Color(255, 224, 138, 26), Color(255, 156, 85, 8));
				}
				else
				{
					SoftRound(g, r, 14.0f, Color(153, 224, 38, 46), 14.0f);
					FillRoundGradient(g, r, 14.0f, Color(255, 214, 34, 43), Color(255, 150, 17, 26));
				}
				StrokeRound(g, r, 14.0f, Color(77, 255, 255, 255));
				DrawLine(g, text, kFontUISemi, 13.0f, Color(255, 255, 255, 255), r, 1);
			}
		}
	}

	// ------------------------------------------------------------------------------------------
	// The top: the pages, the social icons, the window buttons
	// ------------------------------------------------------------------------------------------

	void DrawNav(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		struct Nav
		{
			const wchar_t* text;
			float x, w;
			Page page;
		};
		const Nav navs[] = { { L"PLAY", 62.0f, 100.0f, Page::Play },{ L"NEWS", 178.0f, 100.0f, Page::News },
			{ L"ABOUT", 790.0f, 100.0f, Page::About },{ L"SETTINGS", 905.0f, 120.0f, Page::Settings } };
		const int unread = UnreadNews();
		for (const Nav& n : navs)
		{
			const RectF r(n.x, 59.0f, n.w, 28.0f);
			const bool on = n.page == Page::Play ? l.page == Page::Play || l.page == Page::Multiplayer
				: n.page == Page::News ? l.page == Page::News || l.page == Page::Article : l.page == n.page;
			bool dot = false;
			std::wstring tip;
			if (n.page == Page::Play && UpdateAvailable() && !on)
			{
				dot = true;
				tip = l.latest.title + L" is out!";
			}
			if (n.page == Page::News && unread > 0)
			{
				dot = true;
				tip = unread == 1 ? L"1 new article" : std::to_wstring(unread) + L" new articles";
			}
			const Page target = n.page;
			const bool hover = Hot(r, std::wstring(L"nav") + n.text, [target]()
				{
					if (target != g_launcher.page)
					{
						SetPage(target);
					}
				}, tip);
			Color colour = kWhiteText;
			if (n.page == Page::News && unread > 0)
			{
				// the NEWS button flashes while there are articles a player hasn't seen
				const float q = std::fmod(static_cast<float>(Now() - l.start_tick) / 1100.0f, 1.0f);
				colour = Mix(kWhiteText, kGoldText, 0.5f - 0.5f * std::cos(q * 6.2831853f));
			}
			DrawLabel(g, n.text, r, 19.0f, hover || on, IsDown(std::wstring(L"nav") + n.text), colour);
			if (on)
			{
				FillRound(g, RectF(r.X + 12.0f, r.Y + r.Height - 2.0f, r.Width - 24.0f, 2.0f), 1.0f, kGoldText);
			}
			if (dot)
			{
				const RectF d(r.X + r.Width - 20.0f, r.Y + 2.0f, 10.0f, 10.0f);
				SoftRound(g, d, 5.0f, Color(200, 224, 38, 46), 8.0f);
				FillRound(g, d, 5.0f, kRed);
			}
		}

		// the social icons top left: under the mouse bigger, with the gold glow
		const wchar_t* tips[5] = { L"ModDB", L"Nexus Mods", L"Discord", L"YouTube", L"JKHub" };
		const wchar_t* urls[5] = { kUrlModDB, kUrlNexus, kUrlDiscord, kUrlYouTube, kUrlJKHub };
		for (int i = 0; i < 5; i++)
		{
			const RectF r(32.0f + 30.0f * i, 12.0f, 30.0f, 30.0f);
			const std::wstring url = urls[i];
			const bool hover = Hot(r, std::wstring(L"icon") + tips[i], [url]() { OpenUrl(url); }, tips[i]);
			if (hover)
			{
				DrawLayer(g, l.icon_glow[i]);
				const float px = static_cast<float>(l.icon_big[i]->GetWidth());
				DrawPixels(g, l.icon_big[i].get(), static_cast<int>((r.X + 15.0f) * l.scale - px / 2.0f),
					static_cast<int>((r.Y + 15.0f) * l.scale - px / 2.0f));
			}
			else
			{
				DrawStretched(g, l.icon[i].get(), RectF(r.X + 3.0f, r.Y + 3.0f, 24.0f, 24.0f), 0.85f);
			}
		}
	}

	void DrawWindowButtons(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		const float xs[3] = { 978.0f, 1006.0f, 1034.0f };
		const wchar_t* keys[3] = { L"wmusic", L"wmin", L"wclose" };
		const wchar_t* tips[3] = { L"Music", L"Minimise", L"Close" };
		for (int i = 0; i < 3; i++)
		{
			const RectF r(xs[i], 16.0f, 22.0f, 22.0f);
			std::function<void()> click;
			if (i == 0)
			{
				click = MusicToggle;
			}
			else if (i == 1)
			{
				click = []() { ShowWindow(g_launcher.hwnd, SW_MINIMIZE); };
			}
			else
			{
				click = []() { PlaySfx(IDR_SND_BACK, true); DestroyWindow(g_launcher.hwnd); };
			}
			const bool hover = Hot(r, keys[i], click, tips[i], false);
			const Color c = hover ? kGoldBright : kGold;
			const Gdiplus::SolidBrush fill(Color(150, 0, 0, 0));
			g.FillEllipse(&fill, r.X + 1.0f, r.Y + 1.0f, 20.0f, 20.0f);
			const Gdiplus::Pen ring(c, 2.0f);
			g.DrawEllipse(&ring, r.X + 1.0f, r.Y + 1.0f, 20.0f, 20.0f);
			Gdiplus::Pen mark(c, i == 0 ? 1.5f : 2.0f);
			mark.SetLineJoin(Gdiplus::LineJoinRound);
			if (i == 0)
			{
				// a speaker: waves when the music is on, a cross when it is off
				const PointF speaker[] = { { r.X + 5.5f, r.Y + 9.0f },{ r.X + 8.5f, r.Y + 9.0f },{ r.X + 12.0f, r.Y + 6.0f },
					{ r.X + 12.0f, r.Y + 16.0f },{ r.X + 8.5f, r.Y + 13.0f },{ r.X + 5.5f, r.Y + 13.0f } };
				g.DrawPolygon(&mark, speaker, 6);
				if (l.settings.music)
				{
					g.DrawArc(&mark, r.X + 10.5f, r.Y + 8.5f, 7.0f, 5.0f, -70.0f, 140.0f);
					g.DrawArc(&mark, r.X + 9.0f, r.Y + 6.0f, 13.0f, 10.0f, -70.0f, 140.0f);
				}
				else
				{
					g.DrawLine(&mark, r.X + 14.0f, r.Y + 9.0f, r.X + 18.0f, r.Y + 13.0f);
					g.DrawLine(&mark, r.X + 18.0f, r.Y + 9.0f, r.X + 14.0f, r.Y + 13.0f);
				}
			}
			else if (i == 1)
			{
				g.DrawLine(&mark, r.X + 6.0f, r.Y + 11.0f, r.X + 16.0f, r.Y + 11.0f);
			}
			else
			{
				g.DrawLine(&mark, r.X + 6.5f, r.Y + 6.5f, r.X + 15.5f, r.Y + 15.5f);
				g.DrawLine(&mark, r.X + 15.5f, r.Y + 6.5f, r.X + 6.5f, r.Y + 15.5f);
			}
		}
	}

	// ------------------------------------------------------------------------------------------
	// NEWS
	// ------------------------------------------------------------------------------------------

	Gdiplus::Bitmap* Picture(const std::wstring& path)
	{
		const auto found = g_launcher.pictures.find(path);
		return found != g_launcher.pictures.end() ? found->second.get() : nullptr;
	}

	void DrawNewsPage(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		const RectF page(4.0f, 116.0f, 1080.0f, 450.0f);
		if (l.articles.empty())
		{
			const std::wstring text = l.news_loading || !l.news_loaded ? L"Looking for news..."
				: l.news_failed ? L"Could not load the news. Is the PC online?" : L"No news yet.";
			DrawLine(g, text, kFontUI, 14.5f, kDimText, RectF(page.X, 300.0f, page.Width, 22.0f), 1);
			if (l.news_loaded && !l.news_loading)
			{
				const float w = TextWidth(kFontUISemi, 13.0f, L"Try again") + 32.0f;
				GhostButton(g, RectF(544.0f - w / 2.0f, 336.0f, w, 34.0f), L"Try again", L"newsretry", []()
					{
						PlaySfx(IDR_SND_CLICK);
						LoadNews();
					});
			}
			return;
		}
		l.news_scroll.max = NewsMax();
		l.news_scroll.view = Moved(page);
		const RectF saved = g_clip;
		RectF::Intersect(g_clip, g_clip, Moved(page));
		g.SetClip(g_clip);
		Hot(page, L"newsarea", nullptr, L"", false);
		for (size_t i = 0; i < l.articles.size(); i++)
		{
			const Article& a = l.articles[i];
			const float x = page.X + 28.0f + i * kCardStep - l.news_scroll.offset;
			if (x > page.X + page.Width || x + 548.0f < page.X)
			{
				continue;
			}
			const RectF card(x, page.Y, 548.0f, page.Height);
			// the edges of the gallery fade (the first and last 20 units)
			const float fade = (std::min)(1.0f, (std::min)((x + 548.0f - page.X) / 20.0f, (page.X + page.Width - x) / 20.0f));
			const float a0 = g_alpha;
			g_alpha *= (std::max)(0.0f, fade);
			const int index = static_cast<int>(i);
			const bool hover = Hot(card, L"news" + a.id, [index]()
				{
					if (!g_launcher.news_dragged)
					{
						PlaySfx(IDR_SND_CLICK);
						OpenArticle(index);
					}
				});
			if (hover)
			{
				SoftRound(g, card, 12.0f, Color(140, 255, 170, 40), 10.0f);
			}
			FillRound(g, card, 12.0f, Color(255, 11, 11, 13));
			// the picture, its lower part fading into the card
			Gdiplus::Bitmap* picture = Picture(a.image);
			if (picture)
			{
				const RectF saved2 = g_clip;
				RectF::Intersect(g_clip, g_clip, Moved(RectF(card.X + 2.0f, card.Y + 2.0f, card.Width - 4.0f, 316.0f)));
				g.SetClip(g_clip);
				// inside the card's rounded edge (corner 12 minus the 2 unit edge), so the picture's top corners are round too
				Gdiplus::GraphicsPath edge;
				AddRoundRect(edge, Moved(RectF(card.X + 2.0f, card.Y + 2.0f, card.Width - 4.0f, card.Height - 4.0f)), 10.0f);
				g.SetClip(&edge, Gdiplus::CombineModeIntersect);
				DrawCover(g, picture, RectF(card.X, card.Y, card.Width, 318.0f), 1.0f, true);
				g_clip = saved2;
				g.SetClip(g_clip);
			}
			{
				// the picture fades into the card from 168 to 309 (and the card's colour below)
				const RectF fr = Moved(RectF(card.X + 2.0f, card.Y + 168.0f, card.Width - 4.0f, 141.0f));
				Gdiplus::LinearGradientBrush brush(RectF(fr.X, fr.Y - 1.0f, fr.Width, fr.Height + 2.0f), Color(0, 11, 11, 13),
					Alpha(Color(255, 11, 11, 13), g_alpha), Gdiplus::LinearGradientModeVertical);
				brush.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
				g.FillRectangle(&brush, fr.X, fr.Y, fr.Width, fr.Height);
				FillRect(g, RectF(card.X + 2.0f, card.Y + 168.0f + 141.0f, card.Width - 4.0f, 12.0f), Color(255, 11, 11, 13));
			}
			StrokeRound(g, card, 12.0f, hover ? kGoldText : Color(224, 255, 255, 255), 2.0f);
			if (l.fresh_news.count(a.id))
			{
				const float w = TextWidth(kFontUIBold, 11.0f, L"NEW") + 20.0f;
				const RectF pill(card.X + 14.0f, card.Y + 14.0f, w, 22.0f);
				FillRound(g, pill, 11.0f, kRed);
				DrawLine(g, L"NEW", kFontUIBold, 11.0f, Color(255, 255, 255, 255), pill, 1);
			}
			// the words at the bottom: TAG  DATE, the title, the summary
			const float tw = card.Width - 40.0f;
			TextBox title = MakeText(a.title, kFontLabel, 22.0f, kWhiteText, tw);
			TextBox summary = MakeText(a.summary, kFontUI, 14.0f, Color(255, 207, 207, 210), tw, 19.5f);
			if (summary.height > 19.5f * 3)
			{
				summary.height = 19.5f * 3; // at most three lines
				summary.words.erase(std::remove_if(summary.words.begin(), summary.words.end(), [](const Placed& p) { return p.y >= 19.5f * 3 - 1.0f; }),
					summary.words.end());
			}
			TextBox meta;
			meta.runs.push_back(Run(Upper(a.tag), kFontUISemi, 11.0f, kGoldText));
			meta.runs.push_back(Run(L"   " + Upper(a.date), kFontUI, 11.0f, kDimText));
			LayoutText(meta, tw);
			float y = card.Y + card.Height - 15.0f - summary.height - 6.0f - title.height - 5.0f - meta.height;
			DrawText(g, meta, card.X + 20.0f, y);
			y += meta.height + 5.0f;
			DrawText(g, title, card.X + 20.0f, y);
			y += title.height + 6.0f;
			DrawText(g, summary, card.X + 20.0f, y);
			g_alpha = a0;
		}
		g_clip = saved;
		g.SetClip(g_clip);
		// the arrows
		if (l.news_scroll.offset > 8.0f)
		{
			const RectF r(page.X + 8.0f, page.Y + 204.0f, 42.0f, 42.0f);
			const bool hover = GhostButton(g, r, L"", L"newsleft", []() { StepNews(-1); }, L"Previous articles");
			Gdiplus::Pen pen(Alpha(hover ? kGoldText : kWhiteText, g_alpha), 2.4f);
			pen.SetStartCap(Gdiplus::LineCapRound);
			pen.SetEndCap(Gdiplus::LineCapRound);
			pen.SetLineJoin(Gdiplus::LineJoinRound);
			const RectF m = Moved(r);
			const PointF arrow[] = { { m.X + 24.0f, m.Y + 13.5f },{ m.X + 18.0f, m.Y + 20.0f },{ m.X + 24.0f, m.Y + 26.5f } };
			g.DrawLines(&pen, arrow, 3);
		}
		if (l.news_scroll.offset < l.news_scroll.max - 8.0f)
		{
			const RectF r(page.X + page.Width - 50.0f, page.Y + 204.0f, 42.0f, 42.0f);
			const bool hover = GhostButton(g, r, L"", L"newsright", []() { StepNews(1); }, L"More articles");
			Gdiplus::Pen pen(Alpha(hover ? kGoldText : kWhiteText, g_alpha), 2.4f);
			pen.SetStartCap(Gdiplus::LineCapRound);
			pen.SetEndCap(Gdiplus::LineCapRound);
			pen.SetLineJoin(Gdiplus::LineJoinRound);
			const RectF m = Moved(r);
			const PointF arrow[] = { { m.X + 18.0f, m.Y + 13.5f },{ m.X + 24.0f, m.Y + 20.0f },{ m.X + 18.0f, m.Y + 26.5f } };
			g.DrawLines(&pen, arrow, 3);
		}
	}

	// a YouTube video of an article: its picture (16:9, rounded), a play button (red under the mouse), "Watch on
	// YouTube"; a click opens the video in the browser
	void DrawVideo(Gdiplus::Graphics& g, const ArticleBlock& b, const RectF& r, const std::wstring& key)
	{
		const std::wstring url = YouTubeWatch(b.video);
		const bool hover = Hot(r, key + L"video", [url]() { OpenUrl(url); }, url, false);
		Gdiplus::GraphicsPath frame;
		AddRoundRect(frame, Moved(r), 10.0f);
		g.SetClip(&frame, Gdiplus::CombineModeIntersect);
		FillRect(g, r, Color(255, 16, 16, 18));
		DrawCover(g, Picture(b.image), r, 1.0f);
		{
			// darker towards the bottom (from 55 %), under the words
			const RectF sr = Moved(RectF(r.X, r.Y + r.Height * 0.55f, r.Width, r.Height * 0.45f));
			Gdiplus::LinearGradientBrush shade(RectF(sr.X, sr.Y - 1.0f, sr.Width, sr.Height + 2.0f), Color(0, 0, 0, 0),
				Alpha(Color(176, 0, 0, 0), g_alpha), Gdiplus::LinearGradientModeVertical);
			shade.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
			g.FillRectangle(&shade, sr);
		}
		g.SetClip(g_clip);
		const RectF play(r.X + (r.Width - 68.0f) / 2.0f, r.Y + (r.Height - 48.0f) / 2.0f, 68.0f, 48.0f);
		FillRound(g, play, 12.0f, hover ? Color(255, 255, 0, 0) : Color(230, 33, 33, 33));
		{
			const Gdiplus::SolidBrush white(Alpha(Color(255, 255, 255, 255), g_alpha));
			const float px = play.X + g_dx + (68.0f - 18.0f) / 2.0f + 2.0f, py = play.Y + g_dy + (48.0f - 22.0f) / 2.0f;
			const PointF triangle[] = { { px, py },{ px + 18.0f, py + 11.0f },{ px, py + 22.0f } };
			g.FillPolygon(&white, triangle, 3);
		}
		DrawLine(g, L"Watch on YouTube \x2197", kFontUISemi, 13.0f, Color(255, 255, 255, 255),
			RectF(r.X + 14.0f, r.Y + r.Height - 11.0f - 18.0f, r.Width - 28.0f, 18.0f));
	}

	void DrawArticlePage(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		const RectF card(37.0f, 118.0f, 1014.0f, 448.0f);
		DrawCardBack(g, card);
		if (l.article_index < 0 || l.article_index >= static_cast<int>(l.articles.size()))
		{
			return;
		}
		const Article& a = l.articles[l.article_index];
		const RectF saved = g_clip;
		Gdiplus::GraphicsPath shape;
		AddRoundRect(shape, Moved(RectF(card.X + 1.0f, card.Y + 1.0f, card.Width - 2.0f, card.Height - 2.0f)), 23.0f);
		g.SetClip(&shape);
		// the picture across the top, fading out
		Gdiplus::Bitmap* picture = Picture(a.image);
		if (picture)
		{
			DrawCover(g, picture, RectF(card.X, card.Y, card.Width, 150.0f), 0.75f);
			const RectF fr = Moved(RectF(card.X, card.Y + 45.0f, card.Width, 106.0f));
			Gdiplus::LinearGradientBrush brush(RectF(fr.X, fr.Y - 1.0f, fr.Width, fr.Height + 2.0f), Color(0, 22, 22, 25),
				Alpha(Color(255, 22, 22, 25), g_alpha), Gdiplus::LinearGradientModeVertical);
			brush.SetWrapMode(Gdiplus::WrapModeTileFlipXY);
			g.FillRectangle(&brush, fr);
		}
		g.SetClip(g_clip);

		// the text: meta, title and the article's blocks, scrolled
		const float x = card.X + 48.0f;
		TextBox meta = MakeText(Upper(a.tag + L"  \x00B7  " + a.date), kFontUISemi, 12.0f, kGoldText, kArticleWidth);
		TextBox title = MakeText(a.title, kFontLabel, 30.0f, kWhiteText, kArticleWidth);
		const float head = 44.0f + meta.height + 3.0f + title.height + 12.0f;
		const RectF view(card.X, card.Y + 64.0f, card.Width - 6.0f, card.Height - 64.0f - 4.0f);
		RectF clip_saved;
		const float top = BeginScroll(g, l.article_scroll, view, head + l.article_height + 24.0f, clip_saved);
		DrawText(g, meta, x, top + 44.0f);
		DrawText(g, title, x, top + 44.0f + meta.height + 3.0f);
		const float body = top + head;
		if (l.article_loading)
		{
			DrawLine(g, L"Loading...", kFontUI, 14.5f, kDimText, RectF(x, body, 300.0f, 22.0f));
		}
		for (size_t i = 0; i < l.article_blocks.size(); i++)
		{
			const ArticleBlock& b = l.article_blocks[i];
			const float by = body + b.y;
			if (by > g_clip.Y + g_clip.Height - g_dy || by + b.h < g_clip.Y - g_dy)
			{
				continue;
			}
			const std::wstring key = L"art" + std::to_wstring(i);
			switch (b.kind)
			{
			case ArticleBlock::kNote:
				FillRound(g, RectF(x, by, kArticleWidth, b.h), 6.0f, Color(20, 255, 204, 102));
				StrokeRound(g, RectF(x, by, kArticleWidth, b.h), 6.0f, Color(64, 255, 204, 102));
				DrawText(g, b.text, x + 12.0f, by + 8.0f, key);
				break;
			case ArticleBlock::kItem:
			{
				const float indent = 22.0f * (std::max)(1, b.list_depth);
				const std::wstring marker = b.number > 0 ? std::to_wstring(b.number) + L"." : L"\x2022";
				DrawLine(g, marker, kFontUI, 15.0f, kArticleText, RectF(x + indent - 24.0f, by, 16.0f, 22.5f), 2);
				DrawText(g, b.text, x + indent, by, key);
				break;
			}
			case ArticleBlock::kImage:
			{
				Gdiplus::Bitmap* image = Picture(b.image);
				if (image && b.h > 0.0f)
				{
					const float w = b.h * image->GetWidth() / image->GetHeight();
					DrawStretched(g, image, Moved(RectF(x, by, w, b.h)), g_alpha);
				}
				break;
			}
			case ArticleBlock::kVideo:
				DrawVideo(g, b, RectF(x, by, b.video_width, b.h), key);
				break;
			default:
				DrawText(g, b.text, x, by, key);
				break;
			}
		}
		EndScroll(g, l.article_scroll, clip_saved, L"article");
		g_clip = saved;
		g.SetClip(g_clip);
		const float w = TextWidth(kFontUISemi, 13.0f, L"\x2039  All news") + 25.0f;
		GhostButton(g, RectF(card.X + 16.0f, card.Y + 14.0f, w, 30.0f), L"\x2039  All news", L"allnews", []() { SetPage(Page::News); });
	}

	// ------------------------------------------------------------------------------------------
	// ABOUT
	// ------------------------------------------------------------------------------------------

	const float kAboutTop = 132.0f, kAboutBottom = 552.0f;

	float AboutMainX()
	{
		return g_launcher.tab == Tab::Credits ? 37.0f + 18.0f : 37.0f + 294.0f;
	}

	void DrawHeading(Gdiplus::Graphics& g, const std::wstring& text, const float x, const float y, const float size = 12.0f)
	{
		DrawLine(g, text, kFontUISemi, size, kDimText, RectF(x, y, 600.0f, size * 1.4f));
	}

	void DrawAboutLinks(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		const float x = 37.0f + 18.0f, w = 238.0f;
		DrawHeading(g, L"FIND US", x, 136.0f);
		struct Link
		{
			const wchar_t* name;
			const wchar_t* sub;
			const wchar_t* url;
			int icon;
		};
		const Link links[] = { { L"ModDB", L"Download", kUrlModDB, 0 },{ L"Nexus Mods", L"Download", kUrlNexus, 1 },
			{ L"Discord", L"Community and help", kUrlDiscord, 2 },{ L"YouTube", L"Trailers and guides", kUrlYouTube, 3 },
			{ L"JKHub", L"Files and forum", kUrlJKHub, 4 } };
		float y = 161.0f;
		for (const Link& link : links)
		{
			const RectF r(x, y, w, 53.0f);
			const std::wstring url = link.url;
			const bool hover = Hot(r, std::wstring(L"link") + link.name, [url]() { OpenUrl(url); }, url);
			if (hover)
			{
				FillRound(g, r, 10.0f, Color(20, 255, 204, 102));
				StrokeRound(g, r, 10.0f, Color(82, 255, 204, 102));
			}
			DrawStretched(g, l.icon[link.icon].get(), Moved(RectF(r.X + 10.0f, r.Y + 10.5f, 32.0f, 32.0f)), g_alpha);
			DrawLine(g, link.name, kFontUISemi, 14.0f, kWhiteText, RectF(r.X + 53.0f, r.Y + 7.0f, w - 63.0f, 20.0f));
			DrawLine(g, link.sub, kFontUI, 12.0f, kDimText, RectF(r.X + 53.0f, r.Y + 27.0f, w - 63.0f, 18.0f));
			y += 56.0f;
		}
		FillRect(g, RectF(37.0f + 272.0f, 132.0f, 1.0f, 420.0f), kLine);
	}

	void DrawTabs(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		const float x0 = AboutMainX();
		const float right = 1051.0f - 16.0f;
		float x = x0;
		const struct
		{
			const wchar_t* text;
			Tab tab;
		} tabs[] = { { L"ABOUT", Tab::Mod },{ L"FAQ", Tab::Faq },{ L"TEAM", Tab::Team },{ L"CREDITS", Tab::Credits } };
		for (const auto& t : tabs)
		{
			const float w = TextWidth(kFontLabel, 18.0f, t.text) + 2.0f;
			const RectF r(x, kAboutTop, w, 37.0f);
			const Tab tab = t.tab;
			const bool hover = Hot(r, std::wstring(L"tab") + t.text, [tab]()
				{
					PlaySfx(IDR_SND_CLICK);
					Launcher& m = g_launcher;
					m.tab = tab;
					m.search_focus = tab == Tab::Credits;
					if (tab == Tab::Credits && !m.credits_loaded)
					{
						m.credits_loaded = true;
						std::string data;
						if (ReadFileBytes(m.folder + L"\\" + kModFolder + L"\\ReadMe\\ReadMe_Credits.txt", data))
						{
							ParseCredits(DecodeText(data));
						}
					}
				});
			const bool on = l.tab == t.tab;
			DrawLine(g, t.text, kFontLabel, 18.0f, on || hover ? kGoldText : Color(255, 189, 189, 193), RectF(x, kAboutTop, w + 4.0f, 37.0f));
			if (on)
			{
				FillRect(g, RectF(x, kAboutTop + 36.0f, w, 2.0f), kGoldText);
			}
			x += w + 20.0f;
		}
		HLine(g, x0, kAboutTop + 37.0f, right - x0);
	}

	// About: the mod; the small print sits at the bottom
	void DrawModTab(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		const float x = AboutMainX(), w = 1035.0f - x - 12.0f - 8.0f;
		std::vector<std::pair<TextBox, float>> parts; // text, its left
		std::vector<float> tops;
		float y = 0.0f;
		for (const std::wstring& p : l.about_paragraphs)
		{
			parts.emplace_back(MakeText(p, kFontUI, 14.5f, Color(255, 217, 217, 220), w, 21.5f), 0.0f);
			tops.push_back(y);
			y += parts.back().first.height + 12.0f;
		}
		for (const std::wstring& b : l.about_bullets)
		{
			parts.emplace_back(MakeText(b, kFontUI, 14.5f, Color(255, 217, 217, 220), w - 19.0f, 21.5f), 19.0f);
			tops.push_back(y);
			y += parts.back().first.height + 3.0f;
		}
		const TextBox small_print = MakeText(l.about_small, kFontUI, 11.5f, Color(255, 136, 136, 141), w, 15.5f);
		const RectF view(x, 182.0f, 1035.0f - x, kAboutBottom - 182.0f);
		const float content = (std::max)(view.Height, y + 12.0f + small_print.height);
		RectF saved;
		const float top = BeginScroll(g, l.mod_scroll, view, content, saved);
		for (size_t i = 0; i < parts.size(); i++)
		{
			if (parts[i].second > 0.0f)
			{
				DrawLine(g, L"\x2022", kFontUI, 14.5f, Color(255, 217, 217, 220), RectF(x, top + tops[i], 19.0f, 21.5f));
			}
			DrawText(g, parts[i].first, x + parts[i].second, top + tops[i]);
		}
		DrawText(g, small_print, x, top + content - small_print.height);
		EndScroll(g, l.mod_scroll, saved, L"mod");
	}

	void DrawFaqTab(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		const float x = AboutMainX(), w = 1035.0f - x - 12.0f - 8.0f;
		const Color soft = kSoftText;
		// lay out: heights first (the open answers take their room)
		struct Row
		{
			int group, item;
			float y, h;
		};
		std::vector<Row> rows;
		float y = 0.0f;
		for (size_t gi = 0; gi < l.faq.size(); gi++)
		{
			const FaqGroup& group = l.faq[gi];
			if (!group.name.empty())
			{
				if (gi > 0)
				{
					y += 18.0f;
				}
				rows.push_back({ static_cast<int>(gi), -1, y, 18.0f });
				y += 18.0f;
			}
			for (size_t ii = 0; ii < group.items.size(); ii++)
			{
				const FaqItem& item = group.items[ii];
				const TextBox q = MakeText(item.question, kFontUISemi, 14.0f, kWhiteText, w - 30.0f);
				float h = (std::max)(39.0f, q.height + 12.0f);
				if (item.open)
				{
					for (const std::wstring& line : item.answer)
					{
						const bool bullet = StartsWith(line, L"- ");
						h += MakeText(bullet ? line.substr(2) : line, kFontUI, 13.5f, soft, w - (bullet ? 18.0f : 0.0f), 19.5f).height + (bullet ? 3.0f : 6.0f);
					}
					h += 8.0f;
				}
				rows.push_back({ static_cast<int>(gi), static_cast<int>(ii), y, h + 1.0f });
				y += h + 1.0f;
			}
		}
		const RectF view(x, 182.0f, 1035.0f - x, kAboutBottom - 182.0f);
		RectF saved;
		const float top = BeginScroll(g, l.faq_scroll, view, y, saved);
		for (const Row& row : rows)
		{
			const float ry = top + row.y;
			if (ry > g_clip.Y + g_clip.Height - g_dy || ry + row.h < g_clip.Y - g_dy)
			{
				continue;
			}
			if (row.item < 0)
			{
				DrawHeading(g, Upper(l.faq[row.group].name), x, ry);
				continue;
			}
			FaqItem& item = l.faq[row.group].items[row.item];
			const TextBox q = MakeText(item.question, kFontUISemi, 14.0f, kWhiteText, w - 30.0f);
			const float qh = (std::max)(39.0f, q.height + 12.0f);
			const std::wstring key = L"faq" + std::to_wstring(row.group) + L"." + std::to_wstring(row.item);
			FaqItem* target = &item;
			const bool hover = Hot(RectF(x, ry, w, qh), key, [target]()
				{
					PlaySfx(IDR_SND_CLICK);
					target->open = !target->open;
				});
			TextBox qc = q;
			for (TextRun& r : qc.runs)
			{
				r.color = item.open || hover ? kGoldText : kWhiteText;
			}
			DrawText(g, qc, x, ry + (qh - q.height) / 2.0f);
			DrawLine(g, item.open ? L"\x00D7" : L"+", kFontUI, 18.0f, kGoldText, RectF(x + w - 22.0f, ry, 20.0f, qh), 1);
			if (item.open)
			{
				float ay = ry + qh;
				for (const std::wstring& line : item.answer)
				{
					const bool bullet = StartsWith(line, L"- ");
					const TextBox a = MakeText(bullet ? line.substr(2) : line, kFontUI, 13.5f, soft, w - (bullet ? 18.0f : 0.0f), 19.5f);
					if (bullet)
					{
						DrawLine(g, L"\x2022", kFontUI, 13.5f, Color(255, 217, 217, 220), RectF(x, ay, 18.0f, 19.5f));
					}
					DrawText(g, a, x + (bullet ? 18.0f : 0.0f), ay);
					ay += a.height + (bullet ? 3.0f : 6.0f);
				}
			}
			HLine(g, x, ry + row.h - 1.0f, w);
		}
		EndScroll(g, l.faq_scroll, saved, L"faq");
	}

	void DrawTeamTab(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		const float x = AboutMainX(), w = 1035.0f - x - 12.0f - 8.0f;
		const RectF view(x, 182.0f, 1035.0f - x, kAboutBottom - 182.0f);
		// first the height
		float y = 0.0f;
		const float col = w / 3.0f;
		std::vector<float> group_tops;
		for (size_t gi = 0; gi < l.team.size(); gi++)
		{
			const TeamGroup& group = l.team[gi];
			y += gi == 0 ? 0.0f : 6.0f;
			group_tops.push_back(y);
			y += 16.0f + 9.0f;
			bool roles = false;
			for (const auto& p : group.people)
			{
				roles = roles || !p.second.empty();
			}
			if (roles)
			{
				y += ((group.people.size() + static_cast<float>(2)) / 3) * (18.0f + 16.0f + 9.0f) + 12.0f;
			}
			else
			{
				std::wstring names;
				for (const auto& p : group.people)
				{
					names += (names.empty() ? L"" : L", ") + p.first;
				}
				y += MakeText(names, kFontUI, 13.5f, Color(255, 188, 188, 193), w, 20.0f).height + 14.0f;
			}
		}
		RectF saved;
		const float top = BeginScroll(g, l.team_scroll, view, y, saved);
		for (size_t gi = 0; gi < l.team.size(); gi++)
		{
			const TeamGroup& group = l.team[gi];
			float gy = top + group_tops[gi];
			DrawHeading(g, Upper(group.name), x, gy);
			gy += 25.0f;
			bool roles = false;
			for (const auto& p : group.people)
			{
				roles = roles || !p.second.empty();
			}
			if (roles)
			{
				for (size_t i = 0; i < group.people.size(); i++)
				{
					const float cx = x + (i % 3) * col, cy = gy + (i / static_cast<float>(3)) * (18.0f + 16.0f + 9.0f);
					DrawLine(g, group.people[i].first, kFontUISemi, 13.5f, kWhiteText, RectF(cx, cy, col - 12.0f, 19.0f));
					DrawLine(g, group.people[i].second, kFontUI, 11.5f, kDimText, RectF(cx, cy + 18.0f, col - 12.0f, 16.0f));
				}
			}
			else
			{
				std::wstring names;
				for (const auto& p : group.people)
				{
					names += (names.empty() ? L"" : L", ") + p.first;
				}
				DrawText(g, MakeText(names, kFontUI, 13.5f, Color(255, 188, 188, 193), w, 20.0f), x, gy);
			}
		}
		EndScroll(g, l.team_scroll, saved, L"team");
	}

	// ---- Credits: built once per search ----

	struct CreditCardView
	{
		const CreditEntry* entry = nullptr;
		TextBox name;
		std::vector<TextBox> fields;
		float h = 0.0f;
	};
	struct CreditItem
	{
		enum Kind { kTitle, kGroup, kNote, kRow, kEmpty } kind = kTitle;
		std::wstring text;
		TextBox note;
		std::vector<CreditCardView> cards;
		int section = 0;
		float y = 0.0f, h = 0.0f;
	};
	struct CreditNav
	{
		bool heading = false;
		std::wstring label;
		int count = 0;
		int section = 0; // index into g_credit_titles
	};
	struct CreditTitle
	{
		std::wstring title;
		float y = 0.0f, end = 0.0f;
		std::vector<std::pair<float, std::wstring>> groups;
	};
	std::vector<CreditItem> g_credit_items;
	std::vector<CreditNav> g_credit_nav;
	std::vector<CreditTitle> g_credit_titles;
	float g_credits_height = 0.0f;
	int g_credits_total = 0;

	constexpr float kCreditsListX = 37.0f + 18.0f + 196.0f + 16.0f;
	constexpr float kCreditsListW = 1035.0f - kCreditsListX - 10.0f - 8.0f;

	// the text with the search match marked
	TextBox Marked(const std::wstring& key, const std::wstring& text, const std::wstring& query, const FontId font, const float size,
		const Color& c, const float width)
	{
		TextBox box;
		if (!key.empty())
		{
			box.runs.push_back(Run(key + L": ", kFontUI, size, Color(255, 142, 142, 148)));
		}
		const int at = FindNoCase(text, query);
		if (at < 0)
		{
			box.runs.push_back(Run(text, font, size, c));
		}
		else
		{
			box.runs.push_back(Run(text.substr(0, at), font, size, c));
			TextRun hit = Run(text.substr(at, query.size()), font, size, c);
			hit.back = Color(82, 255, 204, 102);
			box.runs.push_back(hit);
			box.runs.push_back(Run(text.substr(at + query.size()), font, size, c));
		}
		LayoutText(box, width);
		return box;
	}

	void BuildCredits()
	{
		Launcher& l = g_launcher;
		const std::wstring query = Trim(l.credits_query);
		l.credits_shown_query = l.credits_query;
		g_credit_items.clear();
		g_credit_nav.clear();
		g_credit_titles.clear();
		g_credits_total = 0;
		const float card_w = (kCreditsListW) / 3.0f - 8.0f;
		const float inner = card_w - 22.0f;
		float y = 0.0f;
		std::wstring last_group = L"\x0001";
		int heading_index = -1;
		for (const CreditSection& section : l.credits)
		{
			std::wstring group_name, label;
			SectionLabel(section.name, group_name, label);
			std::vector<CreditItem> body;
			std::vector<std::pair<float, std::wstring>> group_heads; // relative y
			float by = 0.0f;
			int count = 0;
			for (const CreditGroup& group : section.groups)
			{
				std::vector<const CreditEntry*> entries;
				for (const CreditEntry& e : group.entries)
				{
					bool hit = query.empty();
					if (!hit && e.note.empty())
					{
						hit = FindNoCase(e.name, query) >= 0;
						for (const auto& f : e.fields)
						{
							hit = hit || FindNoCase(f.second, query) >= 0;
						}
					}
					if (hit)
					{
						entries.push_back(&e);
					}
				}
				if (entries.empty())
				{
					continue;
				}
				if (!group.name.empty())
				{
					by += 12.0f;
					CreditItem head;
					head.kind = CreditItem::kGroup;
					head.text = Upper(group.name);
					head.y = by;
					head.h = 15.0f;
					group_heads.emplace_back(by, group.name);
					body.push_back(head);
					by += 15.0f + 7.0f;
				}
				CreditItem row;
				row.kind = CreditItem::kRow;
				for (const CreditEntry* e : entries)
				{
					if (!e->note.empty())
					{
						CreditItem note;
						note.kind = CreditItem::kNote;
						note.note = MakeText(e->note, kFontUI, 12.5f, Color(255, 232, 216, 181), kCreditsListW - 22.0f);
						note.y = by;
						note.h = note.note.height + 14.0f;
						body.push_back(note);
						by += note.h + 8.0f;
						continue;
					}
					count++;
					CreditCardView card;
					card.entry = e;
					card.name = Marked(L"", e->name, query, kFontUISemi, 13.5f, kWhiteText, inner);
					card.h = 18.0f + card.name.height + 3.0f;
					for (const auto& f : e->fields)
					{
						card.fields.push_back(Marked(f.first, f.second, query, kFontUI, 12.0f, Color(255, 208, 208, 212), inner));
						card.h += card.fields.back().height;
					}
					bool links = false;
					for (const std::wstring& u : e->links)
					{
						links = links || StartsWith(u, L"https://", true);
					}
					if (links)
					{
						card.h += 5.0f + 21.0f;
					}
					row.cards.push_back(card);
				}
				// three cards a row, as high as the highest
				for (size_t i = 0; i < row.cards.size(); i += 3)
				{
					CreditItem part;
					part.kind = CreditItem::kRow;
					part.y = by;
					for (size_t j = i; j < row.cards.size() && j < i + 3; j++)
					{
						part.h = (std::max)(part.h, row.cards[j].h);
						part.cards.push_back(row.cards[j]);
					}
					body.push_back(part);
					by += part.h + 8.0f;
				}
			}
			if (group_name != last_group)
			{
				CreditNav head;
				head.heading = true;
				head.label = Upper(group_name);
				head.count = 0;
				g_credit_nav.push_back(head);
				heading_index = static_cast<int>(g_credit_nav.size()) - 1;
				last_group = group_name;
			}
			if (count == 0)
			{
				continue;
			}
			g_credit_nav[heading_index].count += count; // a heading shows when one of its sections does
			g_credits_total += count;
			const std::wstring title = group_name == L"Models / Skins" || group_name == L"Weapons" ? group_name + L": " + label : label;
			CreditNav nav;
			nav.label = label;
			nav.count = count;
			nav.section = static_cast<int>(g_credit_titles.size());
			g_credit_nav.push_back(nav);
			CreditTitle t;
			t.title = title;
			t.y = y;
			CreditItem heading;
			heading.kind = CreditItem::kTitle;
			heading.text = title;
			heading.y = y;
			heading.h = 6.0f + 26.0f + 7.0f;
			heading.section = nav.section;
			g_credit_items.push_back(heading);
			const float body_top = y + heading.h + 8.0f;
			for (CreditItem& item : body)
			{
				item.y += body_top;
				item.section = nav.section;
				g_credit_items.push_back(item);
			}
			for (auto& gh : group_heads)
			{
				t.groups.emplace_back(gh.first + body_top, gh.second);
			}
			y = body_top + by + 16.0f;
			t.end = y;
			g_credit_titles.push_back(t);
		}
		if (g_credits_total == 0 && !l.credits.empty())
		{
			CreditItem empty;
			empty.kind = CreditItem::kEmpty;
			empty.text = L"Nothing found for \x201C" + query + L"\x201D.";
			empty.y = 40.0f;
			empty.h = 22.0f;
			g_credit_items.push_back(empty);
			y = 62.0f;
		}
		g_credits_height = y + 300.0f; // the last section can scroll to the top
		l.credits_scroll.offset = l.credits_scroll.target = 0.0f;
	}

	void DrawCreditsTab(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		const float x0 = 37.0f + 18.0f;
		const float search_x = 1035.0f - 270.0f;
		if (l.credits.empty())
		{
			DrawText(g, MakeText(L"The credits are in MD\\ReadMe\\ReadMe_Credits.txt of the game folder, which wasn't found.", kFontUI, 14.5f,
				kDimText, 900.0f, 21.5f), x0, 182.0f);
			return;
		}
		if (l.credits_shown_query != l.credits_query && Now() - l.credits_typed >= 150)
		{
			BuildCredits();
		}
		else if (g_credit_titles.empty() && g_credit_items.empty())
		{
			BuildCredits();
		}
		// the search, on the tab row
		const RectF box(search_x, kAboutTop + 2.5f, 270.0f, 32.0f);
		Hot(box, L"search", []() { g_launcher.search_focus = true; }, L"", false);
		FillRound(g, box, 16.0f, Color(115, 0, 0, 0));
		StrokeRound(g, box, 16.0f, l.search_focus ? kGoldText : kLine2);
		const RectF text_box(box.X + 14.0f, box.Y, box.Width - 28.0f, box.Height);
		if (l.credits_query.empty())
		{
			DrawLine(g, L"Search a name or an author", kFontUI, 13.0f, Color(255, 142, 142, 148), RectF(text_box.X + 2.0f, box.Y, text_box.Width, box.Height));
		}
		else
		{
			DrawLine(g, l.credits_query, kFontUI, 13.0f, kWhiteText, text_box);
		}
		if (l.search_focus && (Now() / 530) % 2 == 0)
		{
			const float cx = text_box.X + (std::min)(text_box.Width, TextWidth(kFontUI, 13.0f, l.credits_query)) + 1.0f;
			FillRect(g, RectF(cx, box.Y + 8.0f, 1.2f, 16.0f), kGoldText);
		}
		const std::wstring count = Trim(l.credits_query).empty() ? std::to_wstring(g_credits_total) + L" entries"
			: std::to_wstring(g_credits_total) + L" found";
		DrawLine(g, count, kFontUI, 12.0f, kDimText, RectF(search_x - 214.0f, box.Y, 200.0f, box.Height), 2);

		// the section in view (for the list on the left and the pinned heading)
		int current = 0;
		const float offset = l.credits_scroll.offset;
		for (size_t i = 0; i < g_credit_titles.size(); i++)
		{
			if (g_credit_titles[i].y <= offset + 6.0f)
			{
				current = static_cast<int>(i);
			}
		}

		// the list of sections on the left
		{
			const RectF view(x0, 182.0f, 196.0f, kAboutBottom - 182.0f);
			float h = 0.0f;
			for (const CreditNav& n : g_credit_nav)
			{
				if (n.count > 0)
				{
					h += n.heading ? (h == 0.0f ? 2.0f : 10.0f) + 14.0f + 3.0f : 30.0f;
				}
			}
			RectF saved;
			const float top = BeginScroll(g, l.credits_nav_scroll, view, h, saved);
			float y = top;
			for (const CreditNav& n : g_credit_nav)
			{
				if (n.count == 0)
				{
					continue;
				}
				if (n.heading)
				{
					y += y == top ? 2.0f : 10.0f;
					DrawLine(g, n.label, kFontUISemi, 10.5f, kDimText, RectF(x0 + 9.0f, y, 180.0f, 14.0f));
					y += 17.0f;
					continue;
				}
				const RectF r(x0, y, 190.0f, 30.0f);
				const int section = n.section;
				const bool hover = Hot(r, L"cnav" + std::to_wstring(section), [section]()
					{
						PlaySfx(IDR_SND_CLICK);
						Scroller& s = g_launcher.credits_scroll;
						s.target = (std::min)(s.max, g_credit_titles[section].y);
					}, L"", false);
				const bool on = section == current;
				if (hover || on)
				{
					FillRound(g, r, 10.0f, on ? Color(33, 255, 204, 102) : Color(20, 255, 204, 102));
					if (hover)
					{
						StrokeRound(g, r, 10.0f, Color(82, 255, 204, 102));
					}
				}
				DrawLine(g, n.label, kFontUI, 13.0f, on ? kGoldText : Color(255, 200, 200, 204), RectF(r.X + 9.0f, r.Y, 150.0f, 30.0f));
				DrawLine(g, std::to_wstring(n.count), kFontUI, 11.5f, Color(255, 119, 119, 124), RectF(r.X + 140.0f, r.Y, 41.0f, 30.0f), 2);
				y += 30.0f;
			}
			EndScroll(g, l.credits_nav_scroll, saved, L"cnav");
			FillRect(g, RectF(x0 + 196.0f, 182.0f, 1.0f, kAboutBottom - 182.0f), kLine);
		}

		// the cards
		const RectF view(kCreditsListX, 182.0f, 1035.0f - kCreditsListX, kAboutBottom - 182.0f);
		RectF saved;
		const float top = BeginScroll(g, l.credits_scroll, view, g_credits_height, saved);
		const float card_w = kCreditsListW / 3.0f - 8.0f;
		const float vis_top = g_clip.Y - g_dy, vis_bottom = g_clip.Y + g_clip.Height - g_dy;
		for (size_t i = 0; i < g_credit_items.size(); i++)
		{
			const CreditItem& item = g_credit_items[i];
			const float iy = top + item.y;
			if (iy > vis_bottom || iy + item.h < vis_top)
			{
				continue;
			}
			switch (item.kind)
			{
			case CreditItem::kTitle:
				DrawLine(g, item.text, kFontLabel, 19.0f, kGoldText, RectF(kCreditsListX, iy + 6.0f, kCreditsListW, 26.0f));
				HLine(g, kCreditsListX, iy + item.h - 1.0f, kCreditsListW);
				break;
			case CreditItem::kGroup:
				DrawLine(g, item.text, kFontUISemi, 11.0f, kDimText, RectF(kCreditsListX, iy, kCreditsListW, 15.0f));
				break;
			case CreditItem::kNote:
				FillRound(g, RectF(kCreditsListX, iy, kCreditsListW, item.h), 8.0f, Color(18, 255, 204, 102));
				StrokeRound(g, RectF(kCreditsListX, iy, kCreditsListW, item.h), 8.0f, Color(51, 255, 204, 102));
				DrawText(g, item.note, kCreditsListX + 11.0f, iy + 7.0f);
				break;
			case CreditItem::kEmpty:
				DrawLine(g, item.text, kFontUI, 14.5f, kDimText, RectF(kCreditsListX, iy, kCreditsListW, 22.0f), 1);
				break;
			default:
				for (size_t c = 0; c < item.cards.size(); c++)
				{
					const CreditCardView& card = item.cards[c];
					const RectF r(kCreditsListX + c * (card_w + 8.0f), iy, card_w, item.h);
					const std::wstring key = L"ccard" + std::to_wstring(i) + L"." + std::to_wstring(c);
					const bool hover = Hot(r, key, nullptr, L"", false);
					if (hover)
					{
						SoftRound(g, r, 10.0f, Color(100, 255, 170, 40), 8.0f);
					}
					FillRound(g, r, 10.0f, hover ? Color(15, 255, 204, 102) : Color(9, 255, 255, 255));
					StrokeRound(g, r, 10.0f, hover ? Color(179, 255, 204, 102) : kLine);
					float cy = iy + 9.0f;
					DrawText(g, card.name, r.X + 11.0f, cy);
					cy += card.name.height + 3.0f;
					for (const TextBox& f : card.fields)
					{
						DrawText(g, f, r.X + 11.0f, cy);
						cy += f.height;
					}
					float lx = r.X + 11.0f;
					cy += 5.0f;
					int n = 0;
					for (const std::wstring& url : card.entry->links)
					{
						if (!StartsWith(url, L"https://", true))
						{
							continue;
						}
						const std::wstring text = LinkName(url);
						const float w = LinkChipWidth(text);
						if (lx + w > r.X + r.Width - 8.0f && n > 0)
						{
							break;
						}
						LinkChip(g, RectF(lx, cy, w, 21.0f), text, key + L"l" + std::to_wstring(n++), url);
						lx += w + 5.0f;
					}
				}
				break;
			}
		}
		EndScroll(g, l.credits_scroll, saved, L"credits");

		// the heading of the section in view stays at the top (with the group in view)
		if (!g_credit_titles.empty())
		{
			const CreditTitle& t = g_credit_titles[current];
			if (offset > t.y + 2.0f)
			{
				std::wstring group;
				for (const auto& gh : t.groups)
				{
					if (gh.first <= offset + 40.0f)
					{
						group = gh.second;
					}
				}
				const RectF bar(kCreditsListX, 182.0f, kCreditsListW, 6.0f + 26.0f + 7.0f);
				FillRect(g, bar, Color(255, 21, 21, 24));
				HLine(g, bar.X, bar.Y + bar.Height - 1.0f, bar.Width);
				const float tw = (std::min)(kCreditsListW - 10.0f, TextWidth(kFontLabel, 19.0f, t.title));
				DrawLine(g, t.title, kFontLabel, 19.0f, kGoldText, RectF(bar.X, bar.Y + 6.0f, tw + 4.0f, 26.0f));
				if (!group.empty())
				{
					DrawLine(g, L"   \x00B7   " + Upper(group), kFontUISemi, 11.0f, kDimText, RectF(bar.X + tw + 4.0f, bar.Y + 6.0f, bar.Width - tw - 4.0f, 26.0f));
				}
			}
		}
	}

	void DrawAboutPage(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		DrawCardBack(g, RectF(37.0f, 118.0f, 1014.0f, 448.0f));
		if (l.tab != Tab::Credits)
		{
			DrawAboutLinks(g);
		}
		DrawTabs(g);
		switch (l.tab)
		{
		case Tab::Mod: DrawModTab(g); break;
		case Tab::Faq: DrawFaqTab(g); break;
		case Tab::Team: DrawTeamTab(g); break;
		case Tab::Credits: DrawCreditsTab(g); break;
		}
	}

	// ------------------------------------------------------------------------------------------
	// SETTINGS
	// ------------------------------------------------------------------------------------------

	void SettingRow(Gdiplus::Graphics& g, const float x, const float y, const float w, const float h, const wchar_t* title,
		const wchar_t* sub, const bool line)
	{
		DrawLine(g, title, kFontUISemi, 14.5f, kWhiteText, RectF(x, y + h / 2.0f - 19.0f, w - 200.0f, 21.0f));
		DrawLine(g, sub, kFontUI, 12.0f, kDimText, RectF(x, y + h / 2.0f + 1.0f, w - 200.0f, 18.0f));
		if (line)
		{
			HLine(g, x, y + h - 1.0f, w);
		}
	}

	// a row with a mark, a name and what was found; link: a "Get it" button that opens the page, or with
	// "folder:MD" a button (link_text) that opens that folder of the game
	void InstallRow(Gdiplus::Graphics& g, const float x, const float y, const float w, const wchar_t* state, const std::wstring& name,
		const std::wstring& text, const float name_w, const std::wstring& link = L"", const std::wstring& link_text = L"Get it")
	{
		const std::wstring s = state;
		const Color c = s == L"ok" ? Color(255, 47, 214, 191) : s == L"warn" ? kGoldText : s == L"skip" ? kDimText : Color(255, 255, 143, 122);
		const RectF mark(x, y + 6.0f, 18.0f, 18.0f);
		FillRound(g, mark, 9.0f, Color(48, c.GetR(), c.GetG(), c.GetB()));
		{
			// the mark, drawn (not every font has the characters)
			Gdiplus::Pen pen(Alpha(c, g_alpha), 1.8f);
			pen.SetStartCap(Gdiplus::LineCapRound);
			pen.SetEndCap(Gdiplus::LineCapRound);
			pen.SetLineJoin(Gdiplus::LineJoinRound);
			const float mx = mark.X + 9.0f + g_dx, my = mark.Y + 9.0f + g_dy;
			if (s == L"ok")
			{
				const PointF tick[] = { { mx - 3.5f, my + 0.2f },{ mx - 1.0f, my + 2.8f },{ mx + 3.8f, my - 2.6f } };
				g.DrawLines(&pen, tick, 3);
			}
			else if (s == L"warn")
			{
				g.DrawLine(&pen, mx, my - 3.8f, mx, my + 0.8f);
				g.DrawLine(&pen, mx, my + 3.6f, mx, my + 3.7f);
			}
			else if (s == L"skip")
			{
				g.DrawLine(&pen, mx - 3.0f, my, mx + 3.0f, my);
			}
			else
			{
				g.DrawLine(&pen, mx - 3.0f, my - 3.0f, mx + 3.0f, my + 3.0f);
				g.DrawLine(&pen, mx + 3.0f, my - 3.0f, mx - 3.0f, my + 3.0f);
			}
		}
		DrawLine(g, name, kFontUISemi, 12.5f, kWhiteText, RectF(x + 27.0f, y, name_w, 30.0f));
		float right = x + w;
		if (!link.empty())
		{
			const bool folder = link.compare(0, 7, L"folder:") == 0;
			const float cw = LinkChipWidth(link_text, !folder);
			if (folder)
			{
				const std::wstring target = g_launcher.folder + L"\\" + link.substr(7);
				LinkChip(g, RectF(right - cw, y + 4.5f, cw, 21.0f), link_text, L"get" + name, target, [target]()
					{
						PlaySfx(IDR_SND_CLICK);
						OpenFolder(target);
					});
			}
			else
			{
				LinkChip(g, RectF(right - cw, y + 4.5f, cw, 21.0f), link_text, L"get" + name, link);
			}
			right -= cw + 8.0f;
		}
		const RectF detail(x + 27.0f + name_w, y, right - x - 27.0f - name_w, 30.0f);
		Hot(detail, L"detail" + name, nullptr, text, false);
		DrawLine(g, text, kFontUI, 12.0f, Color(255, 185, 185, 190), detail);
	}

	void DrawSettingsPage(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		Settings& s = l.settings;
		DrawCardBack(g, RectF(37.0f, 118.0f, 1014.0f, 448.0f));
		const float lx = 65.0f, cw = (958.0f - 30.0f) / 2.0f, rx = lx + cw + 30.0f;

		// GAME: read from and written to the game's configs
		DrawHeading(g, L"GAME", lx, 142.0f);
		float y = 167.0f;
		SettingRow(g, lx, y, cw, 66.0f, L"Renderer", L"Rend2 (beta) is the newer graphics engine", true);
		Switch(g, lx + cw, y + 33.0f, L"Vanilla", L"Rend2", s.rend2, L"rend", [](const bool b)
			{
				if (g_launcher.settings.rend2 != b)
				{
					g_launcher.settings.rend2 = b;
					OptionChanged(true);
				}
			});
		y += 66.0f;
		SettingRow(g, lx, y, cw, 66.0f, L"Display", L"How the game window opens", true);
		Switch(g, lx + cw, y + 33.0f, L"Fullscreen", L"Windowed", s.windowed, L"disp", [](const bool b)
			{
				if (g_launcher.settings.windowed != b)
				{
					g_launcher.settings.windowed = b;
					OptionChanged(true);
				}
			});
		y += 66.0f;
		SettingRow(g, lx, y, cw, 66.0f, L"Resolution", L"Desktop uses your screen's resolution", true);
		{
			const RectF box(lx + cw - 158.0f, y + 16.0f, 158.0f, 34.0f);
			FillRound(g, box, 17.0f, Color(115, 0, 0, 0));
			StrokeRound(g, box, 17.0f, kLine2);
			const RectF prev(box.X + 4.0f, box.Y + 4.0f, 26.0f, 26.0f), next(box.X + box.Width - 30.0f, box.Y + 4.0f, 26.0f, 26.0f);
			const bool hp = Hot(prev, L"resprev", []() { PreviousResolution(); OptionChanged(true); }, L"Previous resolution");
			const bool hn = Hot(next, L"resnext", []() { NextResolution(); OptionChanged(true); }, L"Next resolution");
			DrawLine(g, L"\x2039", kFontUISemi, 15.0f, hp ? kGoldText : Color(255, 200, 200, 204), prev, 1);
			DrawLine(g, L"\x203A", kFontUISemi, 15.0f, hn ? kGoldText : Color(255, 200, 200, 204), next, 1);
			const std::wstring res = s.resolution.width > 0 ? std::to_wstring(s.resolution.width) + L" x " + std::to_wstring(s.resolution.height) : L"Desktop";
			DrawLine(g, res, kFontUISemi, 12.5f, kWhiteText, RectF(prev.X + prev.Width, box.Y, 92.0f, box.Height), 1);
		}
		y += 66.0f;
		SettingRow(g, lx, y, cw, 66.0f, L"Controller", L"Play with a gamepad or joystick", false);
		Switch(g, lx + cw, y + 33.0f, L"Off", L"On", s.controller, L"pad", [](const bool b)
			{
				if (g_launcher.settings.controller != b)
				{
					g_launcher.settings.controller = b;
					OptionChanged(true);
				}
			});

		// LAUNCHER, INSTALLATION and ADDONS: the right column scrolls when the installation has a lot to report
		const RectF right_view(rx, 134.0f, cw + 14.0f, 422.0f);
		RectF saved_clip;
		const float right_top = BeginScroll(g, l.settings_scroll, right_view, l.settings_right_height, saved_clip);
		const float right_dy = right_top - right_view.Y;
		DrawHeading(g, L"LAUNCHER", rx, 142.0f + right_dy);
		y = 167.0f + right_dy;
		SettingRow(g, rx, y, cw, 53.0f, L"Music", L"Play background music", true);
		Switch(g, rx + cw, y + 26.5f, L"Off", L"On", s.music, L"music", [](const bool b)
			{
				if (g_launcher.settings.music != b)
				{
					MusicToggle();
				}
			});
		y += 53.0f;
		SettingRow(g, rx, y, cw, 53.0f, L"Check for updates", L"Every time the launcher opens", true);
		Switch(g, rx + cw, y + 26.5f, L"Off", L"On", s.check_updates, L"upd", [](const bool b)
			{
				Launcher& m = g_launcher;
				if (m.settings.check_updates != b)
				{
					m.settings.check_updates = b;
					OptionChanged(false);
					if (b && !m.have_latest)
					{
						LookForUpdate(false);
					}
					ShowUpdate();
				}
			});
		y += 53.0f;

		// INSTALLATION: what was found, and what the last check said
		y += 13.0f;
		DrawHeading(g, L"INSTALLATION", rx, y);
		y += 19.0f;
		const bool ja = HasJediAcademy();
		switch (l.place)
		{
		case Place::InsideBase:
			InstallRow(g, rx, y, cw, L"bad", L"Jedi Academy", L"Movie Duels is inside base. Move the launcher, the game exes and the MD folder into GameData.", 94.0f);
			break;
		case Place::JediOutcast:
			InstallRow(g, rx, y, cw, L"bad", L"Jedi Academy", L"This is Jedi Outcast's folder. Install Movie Duels into Jedi Academy's GameData folder.", 94.0f);
			break;
		default:
			InstallRow(g, rx, y, cw, ja ? L"ok" : L"bad", L"Jedi Academy", ja ? L"Found: base\\assets0-3.pk3"
				: L"Not found. Install Movie Duels into Jedi Academy's GameData folder.", 94.0f);
			break;
		}
		y += 30.0f;
		const size_t pk3 = Pk3Files().size();
		const std::wstring update = L"Update " + l.md_update.Shown();
		if (pk3 == 0)
		{
			InstallRow(g, rx, y, cw, L"bad", L"Movie Duels", L"No pk3 files in the MD folder", 94.0f);
		}
		else if (!l.missing_files.empty())
		{
			std::wstring list;
			for (const std::wstring& file : l.missing_files)
			{
				list += (list.empty() ? L"" : L", ") + file;
			}
			InstallRow(g, rx, y, cw, L"bad", L"Movie Duels", MissingText() + L": " + list, 94.0f);
		}
		else if (l.have_check && !l.last_check.broken.empty())
		{
			std::wstring list;
			for (size_t i = 0; i < l.last_check.broken.size() && i < 3; i++)
			{
				list += (i ? L", " : L"") + l.last_check.broken[i];
			}
			InstallRow(g, rx, y, cw, L"warn", L"Movie Duels", update + L" \x00B7 can't read " + list + (l.last_check.broken.size() > 3 ? L" ..." : L""), 94.0f);
		}
		else
		{
			InstallRow(g, rx, y, cw, L"ok", L"Movie Duels", update + L" \x00B7 all " + std::to_wstring(l.game_files.size()) + L" game files found"
				+ (l.have_check ? L" and readable" : L""), 94.0f);
		}
		y += 30.0f;
		if (!l.old_files.empty())
		{
			std::wstring list;
			for (const std::wstring& file : l.old_files)
			{
				list += (list.empty() ? L"" : L", ") + file;
			}
			InstallRow(g, rx, y, cw, L"bad", L"Old files", L"Remove from MD: " + list, 94.0f, std::wstring(L"folder:") + kModFolder, L"Open MD folder");
			y += 30.0f;
		}
		if (l.settings_check_shown)
		{
			y += 8.0f;
			FillRound(g, RectF(rx, y, cw, 6.0f), 3.0f, Color(26, 255, 255, 255));
			if (l.settings_check_bar > 0.0f)
			{
				const RectF bar = Moved(RectF(rx, y, (std::max)(6.0f, cw * l.settings_check_bar), 6.0f));
				Gdiplus::GraphicsPath path;
				AddRoundRect(path, bar, 3.0f);
				const Gdiplus::LinearGradientBrush brush(RectF(bar.X - 1.0f, bar.Y, bar.Width + 2.0f, bar.Height),
					Alpha(Color(255, 12, 145, 128), g_alpha), Alpha(Color(255, 111, 245, 226), g_alpha), Gdiplus::LinearGradientModeHorizontal);
				g.FillPath(&brush, &path);
			}
			y += 6.0f + 5.0f;
			// up to two lines (a list of missing files can be long: all of it in the tooltip)
			TextBox text = MakeText(l.settings_check_text, kFontUI, 12.0f, Color(255, 196, 196, 200), cw, 16.0f);
			if (text.height > 32.0f)
			{
				text.height = 32.0f;
				text.words.erase(std::remove_if(text.words.begin(), text.words.end(), [](const Placed& p) { return p.y >= 31.0f; }),
					text.words.end());
				if (!text.words.empty())
				{
					text.words.back().text = L"...";
				}
			}
			Hot(RectF(rx, y, cw, (std::max)(16.0f, text.height)), L"checktext", nullptr, text.height > 16.0f ? l.settings_check_text : L"", false);
			DrawText(g, text, rx, y);
			y += (std::max)(16.0f, text.height);
		}
		y += 8.0f;
		{
			float bx = rx;
			const struct
			{
				const wchar_t* text;
				const wchar_t* key;
				const wchar_t* tip;
			} buttons[] = { { L"Check installation", L"checkinst", L"" },{ L"Open folder", L"openfolder", L"The game folder" },
				{ L"Open config folder", L"openconfig", L"Documents\\My Games\\MovieDuels-UD9\\MD: the game's configs and saves" } };
			for (int i = 0; i < 3; i++)
			{
				const float w = TextWidth(kFontUISemi, 13.0f, buttons[i].text) + 26.0f + 2.0f;
				std::function<void()> click;
				if (i == 0)
				{
					click = CheckInstallation;
				}
				else if (i == 1)
				{
					click = []() { PlaySfx(IDR_SND_CLICK); OpenFolder(g_launcher.folder); };
				}
				else
				{
					// Documents\My Games\MovieDuels-UD9\MD: the game writes it the first time it runs
					click = []()
						{
							PlaySfx(IDR_SND_CLICK);
							const std::wstring config = HomeModFolder();
							if (config.empty() || !PathIsDirectoryW(config.c_str()))
							{
								ShowToast(L"The config folder is made when the game runs for the first time.");
								return;
							}
							OpenFolder(config);
						};
				}
				GhostButton(g, RectF(bx, y, w, 30.0f), buttons[i].text, buttons[i].key, click, buttons[i].tip, i != 0 || !l.check_running);
				bx += w + 8.0f;
			}
			y += 30.0f;
		}

		// ADDONS: installed, partly there, or where to get them
		y += 16.0f;
		DrawHeading(g, L"ADDONS", rx, y);
		y += 19.0f;
		for (const Addon& addon : kAddons)
		{
			const std::vector<std::wstring> missing = MissingAddonFiles(addon);
			if (missing.empty())
			{
				InstallRow(g, rx, y, cw, L"ok", addon.name, L"Installed", 150.0f);
			}
			else if (missing.size() == addon.files.size())
			{
				InstallRow(g, rx, y, cw, L"skip", addon.name, L"Not installed", 150.0f, addon.page);
			}
			else
			{
				std::wstring list;
				for (const std::wstring& m : missing)
				{
					list += (list.empty() ? L"" : L", ") + m;
				}
				InstallRow(g, rx, y, cw, L"warn", addon.name, L"Missing " + list, 150.0f, addon.page);
			}
			y += 30.0f;
		}
		const float right_height = y - right_top + 8.0f;
		EndScroll(g, l.settings_scroll, saved_clip, L"settings");
		if (std::fabs(right_height - l.settings_right_height) > 0.5f)
		{
			l.settings_right_height = right_height; // the next frame scrolls with it
			StartTimer();
		}
	}

	// ------------------------------------------------------------------------------------------
	// MULTIPLAYER: Start multiplayer (the game) and Start server (the FFA dedicated server batch file in the MD
	// folder; the launcher stays open to join it)
	// ------------------------------------------------------------------------------------------

	void DrawMultiplayerPage(Gdiplus::Graphics& g)
	{
		const RectF card(220.0f, 180.0f, 648.0f, 320.0f);
		DrawCardBack(g, card);
		DrawLabel(g, L"Multiplayer", RectF(card.X, card.Y + 20.0f, card.Width, 36.0f), 25.0f, true, false, kGoldText, true);
		const float tw = (card.Width - 56.0f - 16.0f) / 2.0f;
		const struct
		{
			const wchar_t* title;
			const wchar_t* text;
			const wchar_t* key;
		} tiles[] = { { L"Start multiplayer", L"Open the game and join a server or play with friends.", L"startmp" },
			{ L"Start server", L"Host a free-for-all server in its own window. The launcher stays open so you can join it.", L"startserver" } };
		for (int i = 0; i < 2; i++)
		{
			const RectF r(card.X + 28.0f + i * (tw + 16.0f), card.Y + 76.0f, tw, 150.0f);
			std::function<void()> click = i == 0 ? std::function<void()>([]() { Play(true); }) : std::function<void()>([]()
				{
					PlaySfx(IDR_SND_CLICK);
					if (StartServer(g_launcher.hwnd)) // the launcher stays open, to start the game and join
					{
						ShowToast(L"The server opens in its own window. You can join it now.");
					}
				});
			const bool hover = Hot(r, tiles[i].key, click);
			FillRound(g, r, 14.0f, hover ? Color(20, 255, 204, 102) : Color(10, 255, 255, 255));
			StrokeRound(g, r, 14.0f, hover ? kGoldText : kLine2);
			const TextBox title = MakeText(tiles[i].title, kFontLabel, 20.0f, hover ? kGoldText : kWhiteText, tw - 32.0f, 0.0f, true);
			const TextBox text = MakeText(tiles[i].text, kFontUI, 13.5f, Color(255, 196, 196, 200), tw - 32.0f, 21.5f, true);
			const float th = title.height + 8.0f + text.height;
			const float ty = r.Y + (r.Height - th) / 2.0f;
			DrawText(g, title, r.X + 16.0f, ty);
			DrawText(g, text, r.X + 16.0f, ty + title.height + 8.0f);
		}
		const float bw = TextWidth(kFontUISemi, 13.0f, L"Back") + 42.0f;
		GhostButton(g, RectF(card.X + (card.Width - bw) / 2.0f, card.Y + 76.0f + 150.0f + 16.0f, bw, 34.0f), L"Back", L"mpback",
			[]() { SetPage(Page::Play); });
	}

	// ------------------------------------------------------------------------------------------
	// On top: the update window, the short messages, the corner texts, the tooltip
	// ------------------------------------------------------------------------------------------

	void DrawUpdateSheet(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		if (!l.sheet_open || !l.have_latest)
		{
			return;
		}
		// the team's 2026-11-07 window: wider, "<TITLE> IS OUT!", what's new and Close (the ModDB / Nexus buttons and
		// the install note are gone)
		const float x = 234.0f, w = 620.0f, inner = w - 44.0f;
		const TextBox title = MakeText(Upper(l.latest.title) + L" IS OUT!", kFontLabel, 24.0f, kGoldText, inner);
		std::vector<TextBox> lines;
		float lines_h = 0.0f;
		for (size_t i = 0; i < l.latest.whats_new.size() && i < 6; i++)
		{
			lines.push_back(MakeText(l.latest.whats_new[i], kFontUI, 13.5f, Color(255, 221, 221, 221), inner - 18.0f, 19.0f));
			lines_h += lines.back().height + 3.0f;
		}
		const float h = 18.0f + title.height + 3.0f + 18.0f + 9.0f + lines_h + 10.0f + 34.0f + 16.0f;
		// it grows from its bottom as it opens
		const float q = l.sheet_fade.value;
		const float a0 = g_alpha;
		g_alpha = (std::min)(1.0f, q * 1.22f);
		const float scale = 0.94f + 0.06f * EaseInOut(q);
		const float y = 186.0f + h * (1.0f - scale);
		Hot(RectF(x, y, w, h), L"sheet", nullptr, L"", false);
		SoftRound(g, RectF(x, y, w, h), 18.0f, Color(190, 0, 0, 0), 40.0f, 8.0f);
		FillRound(g, RectF(x, y, w, h), 18.0f, Color(247, 18, 18, 21));
		StrokeRound(g, RectF(x, y, w, h), 18.0f, Color(128, 255, 90, 90));
		float cy = y + 18.0f;
		DrawText(g, title, x + 22.0f, cy);
		cy += title.height + 3.0f;
		DrawLine(g, L"Get it from ModDB or Nexus Mods", kFontUI, 13.0f, Color(255, 185, 185, 190), RectF(x + 22.0f, cy, inner, 18.0f));
		cy += 18.0f + 9.0f;
		for (const TextBox& line : lines)
		{
			DrawLine(g, L"\x2022", kFontUI, 13.5f, kSoftText, RectF(x + 22.0f, cy, 18.0f, 19.0f));
			DrawText(g, line, x + 40.0f, cy);
			cy += line.height + 3.0f;
		}
		cy += 10.0f;
		const int article = ArticleById(l.latest.article);
		if (article >= 0)
		{
			const float ww = TextWidth(kFontUISemi, 13.0f, L"What's new") + 42.0f;
			GhostButton(g, RectF(x + 22.0f, cy, ww, 34.0f), L"What's new", L"sheetnews", [article]()
				{
					PlaySfx(IDR_SND_CLICK);
					OpenArticle(article);
				});
		}
		const float nw = TextWidth(kFontUISemi, 13.5f, L"Close") + 14.0f;
		const RectF later(x + w - 22.0f - nw, cy, nw, 34.0f);
		const bool hover = Hot(later, L"sheetlater", []()
			{
				PlaySfx(IDR_SND_BACK);
				g_launcher.sheet_open = false; // the emblem stays red
			});
		DrawLine(g, L"Close", kFontUISemi, 13.5f, hover ? kWhiteText : kDimText, later, 1);
		g_alpha = a0;
	}

	void DrawToast(Gdiplus::Graphics& g)
	{
		const Launcher& l = g_launcher;
		if (l.toast_fade.value <= 0.0f)
		{
			return;
		}
		const float a0 = g_alpha;
		g_alpha = l.toast_fade.value;
		const float w = TextWidth(kFontUI, 13.5f, l.toast) + 34.0f;
		const RectF r(544.0f - w / 2.0f, 438.0f, w, 36.0f);
		FillRound(g, r, 18.0f, Color(242, 10, 10, 12));
		StrokeRound(g, r, 18.0f, kLine2);
		DrawLine(g, l.toast, kFontUI, 13.5f, kWhiteText, r, 1);
		g_alpha = a0;
	}

	void DrawCorners(Gdiplus::Graphics& g)
	{
		const Launcher& l = g_launcher;
		TextBox version;
		version.runs.push_back(Run(L"Update " + l.md_update.Shown(), kFontUI, 11.0f, kVersionGrey));
		if (FilesMissing())
		{
			version.runs.push_back(Run(L"  \x00B7  ", kFontUI, 11.0f, kVersionGrey));
			version.runs.push_back(Run(ProblemText(), kFontUISemi, 11.0f, Alpha(Color(255, 255, 170, 40), 0.3f)));
		}
		else if (UpdateAvailable())
		{
			version.runs.push_back(Run(L"  \x00B7  ", kFontUI, 11.0f, kVersionGrey));
			version.runs.push_back(Run(l.latest.title + L" available", kFontUISemi, 11.0f, Alpha(kGoldBright, 0.3f)));
		}
		LayoutText(version, 600.0f);
		DrawText(g, version, 30.0f, 583.0f);
		DrawLine(g, L"" VERSION_STRING, kFontUI, 11.0f, kVersionGrey, RectF(kUnitW - 30.0f - 400.0f, 583.0f, 400.0f, 15.0f), 2);
	}

	void DrawTooltip(Gdiplus::Graphics& g)
	{
		Launcher& l = g_launcher;
		if (l.tip.empty() || Now() - l.hover_since < 500 || l.news_down || l.thumb_drag)
		{
			return;
		}
		const float w = (std::min)(420.0f, TextWidth(kFontUI, 12.0f, l.tip) + 20.0f);
		float x = l.mouse.X + 2.0f, y = l.mouse.Y + 22.0f;
		x = (std::min)(x, kUnitW - w - 10.0f);
		if (y + 24.0f > kUnitH - 6.0f)
		{
			y = l.mouse.Y - 30.0f;
		}
		const RectF r(x, y, w, 24.0f);
		FillRect(g, r, Color(255, 0, 0, 0));
		const Gdiplus::Pen pen(kLine2, 1.0f);
		g.DrawRectangle(&pen, r);
		DrawLine(g, l.tip, kFontUI, 12.0f, kWhiteText, RectF(r.X + 9.0f, r.Y, r.Width - 18.0f, r.Height));
	}

	// ------------------------------------------------------------------------------------------
	// The frame
	// ------------------------------------------------------------------------------------------

	void Render()
	{
		Launcher& l = g_launcher;
		if (!l.bits || IsIconic(l.hwnd))
		{
			return;
		}
		l.hot.clear();
		g_scrollers.clear();
		g_dx = g_dy = 0.0f;
		g_clip = RectF(0.0f, 0.0f, kUnitW, kUnitH);
		g_alpha = 1.0f;
		{
			Gdiplus::Bitmap canvas(l.width, l.height, l.width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(l.bits));
			Gdiplus::Graphics g(&canvas);
			SetQuality(g);
			g.Clear(Color(0, 0, 0, 0));

			// the art, bottom layer first
			const float play = l.play_fade.value;
			DrawPixels(g, l.base.get(), 0, 0);
			DrawLayer(g, l.art, l.art_fade.value);
			DrawLayer(g, l.glow_single, l.glow_single_fade.value * play);
			DrawLayer(g, l.glow_multi, l.glow_multi_fade.value * play);
			DrawLayer(g, l.play_panel, play);
			g.ScaleTransform(l.scale, l.scale);
			DrawFill(g, l.fill_fade.value * play);
			DrawLayer(g, l.red_emblem, l.red_fade.value * play);
			DrawLayer(g, l.teal_emblem, l.teal_fade.value * play);
			DrawLayer(g, l.logo);

			// the bottom bar (PLAY)
			if (play > 0.0f)
			{
				g_alpha = play;
				DrawPlayControls(g);
				g_alpha = 1.0f;
			}

			// the page: it fades in and moves up a little
			if (l.page != Page::Play)
			{
				const float q = EaseInOut(l.page_fade.value);
				g_alpha = l.page_fade.value;
				g_dy = 10.0f * (1.0f - q);
				switch (l.page)
				{
				case Page::News: DrawNewsPage(g); break;
				case Page::Article: DrawArticlePage(g); break;
				case Page::About: DrawAboutPage(g); break;
				case Page::Settings: DrawSettingsPage(g); break;
				case Page::Multiplayer: DrawMultiplayerPage(g); break;
				default: break;
				}
				g_alpha = 1.0f;
				g_dy = 0.0f;
				g_clip = RectF(0.0f, 0.0f, kUnitW, kUnitH);
				g.ResetClip();
			}

			DrawNav(g);
			DrawWindowButtons(g);
			DrawCorners(g);
			DrawUpdateSheet(g);
			DrawToast(g);
			DrawTooltip(g);
		}

		// the tooltip of what is under the mouse now (its text can change, e.g. "1 new article")
		if (!l.hover.empty())
		{
			for (const HotRegion& r : l.hot)
			{
				if (r.key == l.hover)
				{
					l.tip = r.tip;
					break;
				}
			}
		}

		const HDC screen = GetDC(nullptr);
		POINT src = { 0, 0 };
		SIZE size = { l.width, l.height };
		BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		UpdateLayeredWindow(l.hwnd, screen, nullptr, &size, l.mem_dc, &src, 0, &blend, ULW_ALPHA);
		ReleaseDC(nullptr, screen);
	}

	// the scroller a thumb key belongs to
	Scroller* ScrollerOfThumb(const std::wstring& key)
	{
		Launcher& l = g_launcher;
		const struct
		{
			const wchar_t* key;
			Scroller* s;
		} thumbs[] = { { L"articlethumb", &l.article_scroll },{ L"modthumb", &l.mod_scroll },{ L"faqthumb", &l.faq_scroll },
			{ L"teamthumb", &l.team_scroll },{ L"creditsthumb", &l.credits_scroll },{ L"cnavthumb", &l.credits_nav_scroll },
			{ L"settingsthumb", &l.settings_scroll } };
		for (const auto& t : thumbs)
		{
			if (key == t.key)
			{
				return t.s;
			}
		}
		return nullptr;
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
	// Everything that moves
	// ------------------------------------------------------------------------------------------

	void StartTimer()
	{
		Launcher& l = g_launcher;
		if (!l.timer_on && l.hwnd)
		{
			l.timer_on = true;
			l.last_tick = Now();
			SetTimer(l.hwnd, kAnimTimer, 33, nullptr);
		}
	}

	// whether the next frame looks different
	bool Animating()
	{
		Launcher& l = g_launcher;
		for (const Fader* f : { &l.art_fade, &l.play_fade, &l.page_fade, &l.glow_single_fade, &l.glow_multi_fade, &l.sheet_fade,
			&l.red_fade, &l.teal_fade, &l.fill_fade, &l.modes_fade, &l.toast_fade })
		{
			if (f->Moving())
			{
				return true;
			}
		}
		const ULONGLONG now = Now();
		const bool play = l.page == Page::Play;
		return (play && (l.emblem == Emblem::Checking || l.emblem == Emblem::Available || l.emblem == Emblem::Warning
			|| (l.emblem == Emblem::Ready && now - l.emblem_since < 3600)))
			|| (l.shock_start && now - l.shock_start < 1000)
			|| UnreadNews() > 0 || l.checking_files || l.news_animating || l.search_focus
			|| (l.page == Page::About && l.tab == Tab::Credits && l.credits_shown_query != l.credits_query)
			|| (!l.tip.empty() && now - l.hover_since < 700);
	}

	void Tick()
	{
		Launcher& l = g_launcher;
		const ULONGLONG now = Now();
		const float dt = static_cast<float>(std::min<ULONGLONG>(100, now - l.last_tick));
		l.last_tick = now;

		// hovering a mode lights up its side of the art (the side the mouse was on last stays lit)
		const bool play = l.page == Page::Play && !l.checking_files;
		l.glow_single_fade.Set(play && !l.multiplayer_side ? 1.0f : 0.0f, 180.0f);
		l.glow_multi_fade.Set(play && l.multiplayer_side ? 1.0f : 0.0f, 180.0f);

		for (Fader* f : { &l.art_fade, &l.play_fade, &l.page_fade, &l.glow_single_fade, &l.glow_multi_fade, &l.sheet_fade,
			&l.red_fade, &l.teal_fade, &l.fill_fade, &l.modes_fade, &l.toast_fade })
		{
			f->Step(dt);
		}
		StepFill(dt);
		StepNewsScroll();
		const bool scrolling = StepScrollers(dt);

		// the delayed things whose time has come
		std::vector<std::function<void()>> due;
		for (auto it = l.later.begin(); it != l.later.end();)
		{
			if (it->first <= now)
			{
				due.push_back(std::move(it->second));
				it = l.later.erase(it);
			}
			else
			{
				++it;
			}
		}
		for (auto& action : due)
		{
			action();
		}

		const bool animating = Animating() || scrolling || !due.empty();
		if (animating)
		{
			Render();
		}
		else if (l.later.empty() && !l.check_running && !l.looking_for_update)
		{
			KillTimer(l.hwnd, kAnimTimer);
			l.timer_on = false;
			Render(); // the last frame
		}
	}

	// ------------------------------------------------------------------------------------------
	// The mouse and the keys
	// ------------------------------------------------------------------------------------------

	PointF ToUnits(const int x, const int y)
	{
		return PointF(x / g_launcher.scale, y / g_launcher.scale);
	}

	const HotRegion* RegionAt(const PointF& p)
	{
		const auto& hot = g_launcher.hot;
		for (auto it = hot.rbegin(); it != hot.rend(); ++it)
		{
			if (Contains(it->box, p))
			{
				return &*it;
			}
		}
		return nullptr;
	}

	void UpdateHover(const bool sound)
	{
		Launcher& l = g_launcher;
		const HotRegion* region = RegionAt(l.mouse);
		const std::wstring key = region ? region->key : L"";
		if (key == l.hover)
		{
			return;
		}
		l.hover = key;
		l.hover_since = Now();
		l.tip = region ? region->tip : L"";
		if (sound && region && region->click && region->hover_sound)
		{
			PlaySfx(IDR_SND_HOVER);
		}
		if (!l.tip.empty())
		{
			StartTimer();
		}
		Render();
	}

	bool IsNewsKey(const std::wstring& key)
	{
		return key == L"newsarea" || (StartsWith(key, L"news") && key != L"newsleft" && key != L"newsright" && key != L"newsretry");
	}

	void EndNewsDrag()
	{
		Launcher& l = g_launcher;
		if (!l.news_down)
		{
			return;
		}
		l.news_down = false;
		if (!l.news_dragged)
		{
			return;
		}
		if (Now() - l.news_last_time > 90)
		{
			l.news_speed = 0.0f; // held still before letting go: no throw
		}
		ScrollNewsTo(NearestCard(l.news_scroll.offset + l.news_speed * 220.0f));
	}

	void GoBack(const HWND hwnd)
	{
		Launcher& l = g_launcher;
		if (l.search_focus && !l.credits_query.empty())
		{
			l.credits_query.clear();
			l.credits_typed = 0;
		}
		else if (l.sheet_open)
		{
			PlaySfx(IDR_SND_BACK);
			l.sheet_open = false;
		}
		else if (l.page == Page::Article)
		{
			SetPage(Page::News);
		}
		else if (l.page != Page::Play)
		{
			SetPage(Page::Play);
		}
		else
		{
			PlaySfx(IDR_SND_BACK, true);
			DestroyWindow(hwnd);
			return;
		}
		Render();
	}

	LRESULT CALLBACK WndProc(const HWND hwnd, const UINT msg, const WPARAM wparam, const LPARAM lparam)
	{
		Launcher& l = g_launcher;
		switch (msg)
		{
		case WM_NCHITTEST:
		{
			// drag the window by anything that is not a button
			POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
			ScreenToClient(hwnd, &pt);
			return RegionAt(ToUnits(pt.x, pt.y)) || l.news_down || l.thumb_drag ? HTCLIENT : HTCAPTION;
		}
		case WM_NCLBUTTONDBLCLK:
			return 0; // no maximise by double-clicking the art
		case WM_NCLBUTTONDOWN:
			if (l.search_focus)
			{
				l.search_focus = false;
				Render();
			}
			return DefWindowProcW(hwnd, msg, wparam, lparam);
		case WM_SETCURSOR:
			if (LOWORD(lparam) == HTCLIENT)
			{
				const HotRegion* region = RegionAt(l.mouse);
				LPCWSTR cursor = IDC_ARROW;
				if (l.news_dragged && l.news_down)
				{
					cursor = IDC_SIZEWE;
				}
				else if (region && region->key == L"search")
				{
					cursor = IDC_IBEAM;
				}
				else if (region && region->click)
				{
					cursor = IDC_HAND;
				}
				SetCursor(LoadCursorW(nullptr, cursor));
				return TRUE;
			}
			return DefWindowProcW(hwnd, msg, wparam, lparam);
		case WM_MOUSEMOVE:
		{
			if (!l.tracking_mouse)
			{
				TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
				l.tracking_mouse = TrackMouseEvent(&tme) != FALSE;
			}
			l.mouse = ToUnits(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			if (l.thumb_drag)
			{
				Scroller& s = *l.thumb_drag;
				const float h = (std::max)(24.0f, s.view.Height * s.view.Height / (s.view.Height + s.max));
				const float room = s.view.Height - h;
				if (room > 0.0f)
				{
					const float q = (l.mouse.Y - l.thumb_grab - s.view.Y) / room;
					s.offset = s.target = (std::max)(0.0f, (std::min)(1.0f, q)) * s.max;
				}
				Render();
				return 0;
			}
			if (l.news_down)
			{
				const float x = l.mouse.X;
				if (!l.news_dragged && std::fabs(x - l.news_start_x) > 5.0f)
				{
					l.news_dragged = true;
					SetCapture(hwnd);
				}
				if (l.news_dragged)
				{
					l.news_scroll.offset = (std::max)(0.0f, (std::min)(NewsMax(), l.news_start_offset - (x - l.news_start_x)));
					const ULONGLONG now = Now();
					const float dt = (std::max)(1.0f, static_cast<float>(now - l.news_last_time));
					l.news_speed = 0.7f * l.news_speed + 0.3f * (l.news_last_x - x) / dt;
					l.news_last_x = x;
					l.news_last_time = now;
					Render();
					return 0;
				}
			}
			UpdateHover(true);
			return 0;
		}
		case WM_MOUSELEAVE:
			l.tracking_mouse = false;
			if (!l.news_down && !l.thumb_drag)
			{
				l.pressed.clear();
				l.mouse = PointF(-1.0f, -1.0f);
				UpdateHover(false);
			}
			return 0;
		case WM_LBUTTONDOWN:
		{
			l.mouse = ToUnits(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			const HotRegion* region = RegionAt(l.mouse);
			l.pressed = region ? region->key : L"";
			if (l.pressed != L"search")
			{
				l.search_focus = false;
			}
			SetCapture(hwnd);
			if (Scroller* s = ScrollerOfThumb(l.pressed))
			{
				const float h = (std::max)(24.0f, s->view.Height * s->view.Height / (s->view.Height + s->max));
				const float top = s->view.Y + (s->view.Height - h) * (s->max > 0.0f ? s->offset / s->max : 0.0f);
				l.thumb_drag = s;
				l.thumb_grab = l.mouse.Y - top;
			}
			else if (l.page == Page::News && IsNewsKey(l.pressed))
			{
				l.news_animating = false;
				l.news_down = true;
				l.news_dragged = false;
				l.news_start_x = l.news_last_x = l.mouse.X;
				l.news_start_offset = l.news_scroll.offset;
				l.news_last_time = Now();
				l.news_speed = 0.0f;
			}
			Render();
			return 0;
		}
		case WM_LBUTTONUP:
		{
			ReleaseCapture();
			l.mouse = ToUnits(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
			const bool dragged = l.news_down && l.news_dragged;
			EndNewsDrag();
			l.news_dragged = false;
			if (l.thumb_drag)
			{
				l.thumb_drag = nullptr;
				l.pressed.clear();
				Render();
				return 0;
			}
			const HotRegion* region = RegionAt(l.mouse);
			const std::wstring pressed = l.pressed;
			l.pressed.clear();
			if (dragged || !region || region->key != pressed || !region->click)
			{
				Render(); // released somewhere else, or the end of a drag (a drag doesn't open the card)
				return 0;
			}
			const std::function<void()> click = region->click; // the regions are rebuilt by the click's frame
			click();
			Render();
			UpdateHover(false);
			return 0;
		}
		case WM_MOUSEWHEEL:
		{
			POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
			ScreenToClient(hwnd, &pt);
			const PointF p = ToUnits(pt.x, pt.y);
			const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam));
			if (l.page == Page::News && Contains(l.news_scroll.view, p) && !l.articles.empty())
			{
				// one wheel notch = one card; a touchpad's many small steps add up first
				l.wheel_sum += delta;
				if (Now() < l.wheel_lock || std::fabs(l.wheel_sum) < 40.0f)
				{
					return 0;
				}
				const int direction = l.wheel_sum < 0 ? 1 : -1; // wheel down: the next card
				l.wheel_sum = 0.0f;
				l.wheel_lock = Now() + 260;
				StepNews(direction);
				return 0;
			}
			for (auto it = g_scrollers.rbegin(); it != g_scrollers.rend(); ++it)
			{
				Scroller* s = *it;
				if (Contains(s->view, p) && s->max > 0.0f)
				{
					s->target = (std::max)(0.0f, (std::min)(s->max, s->target - delta / 120.0f * 64.0f));
					StartTimer();
					break;
				}
			}
			return 0;
		}
		case WM_CHAR:
			if (l.search_focus && l.page == Page::About && l.tab == Tab::Credits)
			{
				const wchar_t c = static_cast<wchar_t>(wparam);
				if (c == 8)
				{
					if (!l.credits_query.empty())
					{
						l.credits_query.pop_back();
					}
				}
				else if (c == 22 && OpenClipboard(hwnd)) // Ctrl+V
				{
					const HANDLE data = GetClipboardData(CF_UNICODETEXT);
					const wchar_t* text = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
					if (text)
					{
						for (const wchar_t* t = text; *t && l.credits_query.size() < 60; t++)
						{
							if (*t >= 32)
							{
								l.credits_query += *t;
							}
						}
						GlobalUnlock(data);
					}
					CloseClipboard();
				}
				else if (c >= 32 && l.credits_query.size() < 60)
				{
					l.credits_query += c;
				}
				l.credits_typed = Now();
				StartTimer();
				Render();
			}
			return 0;
		case WM_KEYDOWN:
			if (wparam == VK_ESCAPE)
			{
				GoBack(hwnd);
			}
			else if (l.page == Page::News && (wparam == VK_LEFT || wparam == VK_RIGHT))
			{
				StepNews(wparam == VK_LEFT ? -1 : 1);
			}
			return 0;
		case WM_TIMER:
			if (wparam == kAnimTimer)
			{
				Tick();
			}
			return 0;
		case kMsgLatest:
			LatestArrived(reinterpret_cast<LatestResult*>(lparam));
			Render();
			return 0;
		case kMsgNews:
			NewsArrived(reinterpret_cast<TextResult*>(lparam));
			Render();
			return 0;
		case kMsgImage:
			ImageArrived(reinterpret_cast<ImageResult*>(lparam));
			Render();
			return 0;
		case kMsgArticle:
			ArticleArrived(reinterpret_cast<TextResult*>(lparam));
			Render();
			return 0;
		case kMsgCheckProgress:
			CheckProgress(static_cast<int>(wparam), reinterpret_cast<std::pair<int, std::wstring>*>(lparam));
			StartTimer();
			Render();
			return 0;
		case kMsgCheckDone:
			CheckDone(reinterpret_cast<CheckResult*>(lparam));
			StartTimer();
			Render();
			return 0;
		case MM_MCINOTIFY:
			// the music reached its end: play it again (loop)
			if (wparam == MCI_NOTIFY_SUCCESSFUL && l.music_open)
			{
				PlayMusicFromStart(hwnd);
			}
			return 0;
		case WM_SIZE:
			if (wparam == SIZE_RESTORED)
			{
				Render();
			}
			return DefWindowProcW(hwnd, msg, wparam, lparam);
		case WM_DESTROY:
			KillTimer(hwnd, kAnimTimer);
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

	Launcher& l = g_launcher;
	l.instance = instance;
	l.start_tick = Now();

	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	PathRemoveFileSpecW(path);
	l.folder = path;
	LoadSettings();
	l.md_update = DetectUpdate();

	Gdiplus::GdiplusStartupInput gdiplus_input;
	ULONG_PTR gdiplus_token = 0;
	Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr);
	ChooseScale();
	LoadFonts();
	BuildArt();
	ParseAbout(ResourceText(IDR_TEXT_ABOUT));
	ParseFaq(ResourceText(IDR_TEXT_FAQ));
	ParseTeam(ResourceText(IDR_TEXT_TEAM));
	l.game_files = ParseGameFiles(ResourceText(IDR_TEXT_GAMEFILES));
	l.old_game_files = ParseGameFiles(ResourceText(IDR_TEXT_OLDGAMEFILES));
	FindMissingFiles(); // each time the launcher opens: the warning on PLAY (amber emblem) when the installation is wrong

	// the PLAY page at once
	l.art_fade.Snap(1.0f);
	l.play_fade.Snap(1.0f);
	l.page_fade.Snap(1.0f);
	l.modes_fade.Snap(1.0f);
	l.glow_single_fade.Snap(1.0f);

	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = instance;
	// the icon at the sizes Windows asks for (large: taskbar / Alt+Tab, small: the title), taken from the .ico's own
	// 16-256 px pictures instead of one 32 px picture scaled
	wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_LAUNCHER), IMAGE_ICON,
		GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
	wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_LAUNCHER), IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.lpszClassName = L"MovieDuelsLauncher";
	RegisterClassExW(&wc);

	// centred on the work area, borderless and layered (the art has its own frame and window buttons)
	RECT work = {};
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
	const int x = work.left + (work.right - work.left - l.width) / 2;
	const int y = work.top + (work.bottom - work.top - l.height) / 2;
	l.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, wc.lpszClassName, L"MovieDuels",
		WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU, x, y, l.width, l.height, nullptr, nullptr, instance, nullptr);
	if (!l.hwnd || !CreateSurface())
	{
		FreeSurface();
		FreeArt();
		Gdiplus::GdiplusShutdown(gdiplus_token);
		return 1;
	}
	ShowUpdate();
	Render();
	ShowWindow(l.hwnd, show);
	StartMusic(l.hwnd);
	StartUp();
	Render();

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