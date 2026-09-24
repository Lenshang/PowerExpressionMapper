@echo off
setlocal
title 版本发布工具

cd /d "%~dp0"

echo ==============================================
echo   PowerExpressionMapper 版本发布工具
echo   推送 v* 标签后 GitHub Actions 会自动构建
echo   并发布 Release，无需其他操作。
echo ==============================================
echo.

rem ---- 检查当前目录是否为 Git 仓库 ----
git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
    echo [错误] 当前目录不是 Git 仓库: %cd%
    goto :fail
)

rem ---- 从 origin 地址解析 仓库所有者/名称 ----
set "REPO=Lenshang/PowerExpressionMapper"
for /f "delims=" %%i in ('git remote get-url origin 2^>nul ^<nul') do set "REPO=%%i"
set "REPO=%REPO:git@github.com:=%"
set "REPO=%REPO:https://github.com/=%"
set "REPO=%REPO:.git=%"

rem ---- 输入版本号 ----
set "VERSION="
set /p "VERSION=请输入版本号，例如 1.0.0，带不带 v 前缀都可以: "
if not defined VERSION (
    echo [错误] 版本号不能为空
    goto :fail
)

set "TAG=%VERSION%"
if "%TAG:~0,1%"=="v" set "TAG=%TAG:~1%"

echo %TAG%|findstr /r "^[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo [错误] 版本号格式不正确，应形如 1.0.0
    goto :fail
)
set "TAG=v%TAG%"

rem ---- 检查 TAG 是否已存在 ----
git rev-parse -q --verify "refs/tags/%TAG%" >nul 2>&1
if not errorlevel 1 (
    echo [错误] TAG %TAG% 已存在，请换一个版本号。
    goto :fail
)

rem ---- 发布前确认 ----
set "DIRTY="
git diff --quiet --ignore-submodules 2>nul || set "DIRTY=1"
git diff --cached --quiet --ignore-submodules 2>nul || set "DIRTY=1"

echo.
echo ----------------------------------------------
echo   仓库: %REPO%
echo   TAG : %TAG%
if defined DIRTY echo   [注意] 工作区有未提交的修改，不会包含在本次发布中
echo ----------------------------------------------
set "CONFIRM="
set /p "CONFIRM=确认发布? 输入 Y 继续: "
if /i not "%CONFIRM%"=="Y" (
    echo 已取消。
    goto :end
)

rem ---- 打 TAG 并推送 ----
echo.
echo [1/2] 创建 TAG %TAG% ...
git tag -a "%TAG%" -m "Release %TAG%"
if errorlevel 1 goto :fail

echo [2/2] 推送 TAG 到 origin ...
git push origin "%TAG%"
if errorlevel 1 (
    git tag -d "%TAG%" >nul 2>&1
    echo [错误] 推送 TAG 失败，已回滚本地 TAG，请检查网络后重试。
    goto :fail
)

echo.
echo [完成] TAG 已推送，GitHub Actions 正在自动构建并发布 Release。
echo        构建完成后可在这里下载:
echo        https://github.com/%REPO%/releases/tag/%TAG%
echo.
start "" "https://github.com/%REPO%/actions"
goto :end

:fail
echo.
echo 发布失败。
pause
exit /b 1

:end
echo.
pause
exit /b 0
