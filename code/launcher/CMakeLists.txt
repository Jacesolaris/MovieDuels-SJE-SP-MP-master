# MovieDuels launcher (Windows only): the team's launcher (MovieDuels-SJE-Launcher-master, WPF; its new pages of
# 2026-11-06) as a native exe built with the game, like the SerenityJediEngine launcher. SINGLEPLAYER / MULTIPLAYER
# start MovieDuels-SP.x86_64.exe / MovieDuels-MP.x86_64.exe from its own folder (release builds of the game only start
# from it: shared/sys/sys_main.cpp Sys_RequireLauncher).
# art/, sound/, fonts/ and texts/ are the team's launcher assets (Assets/Images, Assets/Icons, Assets/Audio,
# Assets/Fonts, Assets/Text), renamed without spaces. The old launcher's bg_*.jpg, btn_*.png, CTCMusic.mp3,
# info_panel.png and icon_settings.png are no longer used.

set(LauncherName "MovieDuels-Launcher")

set(LauncherFiles
	"${SPDir}/launcher/md_launcher.cpp"
	"${SPDir}/launcher/md_launcher.rc"
	"${SPDir}/launcher/resource.h"
	)
source_group("launcher" FILES ${LauncherFiles})

add_executable(${LauncherName} WIN32 ${LauncherFiles})
target_compile_definitions(${LauncherName} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
# ${SPDir}: win32/AutoVersion.h (the build shown in the corner); minizip + zlib: the update of the game's assets,
# read from ui/main.menu in the MD folder's pk3s, and the installation check; winhttp: the news and the newest update
# (MovieDuels-Launcher-News)
target_include_directories(${LauncherName} PRIVATE "${SPDir}" ${MINIZIP_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIR})
target_link_libraries(${LauncherName} ${MINIZIP_LIBRARIES} ${ZLIB_LIBRARIES} gdiplus shlwapi winmm shell32 winhttp)
set_target_properties(${LauncherName} PROPERTIES PROJECT_LABEL "Launcher")

install(TARGETS ${LauncherName}
	RUNTIME
	DESTINATION ${JKAInstallDir}
	COMPONENT ${JKASPClientComponent})
