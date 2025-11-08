@echo off
echo.
echo [Toplik Service] Instaliram potrebne Python biblioteke...
echo.

:: Provjeri je li pip dostupan
pip --version >nul 2>&1
if %errorlevel% neq 0 (
    echo GRESKA: Python 'pip' nije pronadjen. 
    echo Provjerite da li ste instalirali Python i stiklirali "Add to PATH".
    pause
    exit /b
)

:: Instaliraj biblioteke iz requirements.txt
pip install -r requirements.txt

echo.
echo [Toplik Service] Sve biblioteke su uspjesno instalirane.
echo.
pause