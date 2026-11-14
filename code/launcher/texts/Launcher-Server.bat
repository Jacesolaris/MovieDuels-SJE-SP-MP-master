@ECHO OFF
REM MovieDuels dedicated server. Built into the launcher (code/launcher/texts/Launcher-Server.bat):
REM Start server writes it to %TEMP% and runs it with the game folder as the current folder.
REM The server settings are Launcher-Server.cfg in MD/Update9.pk3.
ECHO This batch file starts an MovieDuels dedicated server.
ECHO It will autorestart the server whenever it crashes.
ECHO To stop the restart cycle, just close this window.

ECHO Genuine MD-MP-SJE Combat System OK.

ECHO Starting Server...

ECHO Now Start MovieDuels-MP.x86_64 and Join the Server.
ECHO.
call MovieDuels-DED.x86_64 +set fs_game MD +set dedicated 2 +exec Launcher-Server.cfg

ECHO.
ECHO Restarting Server...
ECHO.
ECHO.Please Wait

ECHO MovieDuels-Sabersystem-FFA-Server server auto-restarted: >> MD-MP.txt

DATE /t >> MD.txt
TIME /t >> MD.txt

call MovieDuels-DED.x86_64 +set fs_game MD +set dedicated 2 +exec Launcher-Server.cfg

ECHO.
ECHO Restarting Server...
ECHO.
ECHO.Please Wait

ECHO MovieDuels-Sabersystem-FFA-Server server auto-restarted: >> MD-MP.txt

DATE /t >> MD.txt
TIME /t >> MD.txt

call MovieDuels-DED.x86_64 +set fs_game MD +set dedicated 2 +exec Launcher-Server.cfg

ECHO.
ECHO Restarting Server...
ECHO.
ECHO.Please Wait

ECHO MovieDuels-Sabersystem-FFA-Server server auto-restarted: >> MD-MP.txt

DATE /t >> MD.txt
TIME /t >> MD.txt

call MovieDuels-DED.x86_64 +set fs_game MD +set dedicated 2 +exec Launcher-Server.cfg
