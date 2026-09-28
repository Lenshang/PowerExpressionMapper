@echo off
cd /d "%~dp0"
set /p "VERSION=«Î ‰»Î∞Ê±æ∫≈£¨¿˝»Á 1.0.0: "
if not defined VERSION goto :eof
if "%VERSION:~0,1%"=="v" set "VERSION=%VERSION:~1%"
set "TAG=v%VERSION%"
git tag -a "%TAG%" -m "Release %TAG%"
git push origin "%TAG%"
pause
