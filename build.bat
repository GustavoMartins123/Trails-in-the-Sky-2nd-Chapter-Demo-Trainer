@echo off
setlocal
cd /d "%~dp0"

set CXX=g++
where g++ >nul 2>nul || set CXX=C:\TDM-GCC-64\bin\g++.exe
if not exist "%CXX%" if "%CXX%" neq "g++" (
  echo [ERRO] g++ nao encontrado. Instale o TDM-GCC-64 ou o MinGW-w64.
  pause & exit /b 1
)

echo Compilando SoraTrainer.exe ...
"%CXX%" -std=c++11 -O2 -municode -mwindows -static ^
  -o SoraTrainer.exe src\main.cpp src\memory.cpp src\engine.cpp ^
  -lcomctl32 -lpsapi -lgdi32 -luser32
if errorlevel 1 goto :fail

echo Compilando SoraSelfTest.exe ...
"%CXX%" -std=c++11 -O2 -static ^
  -o SoraSelfTest.exe src\selftest.cpp src\memory.cpp -lpsapi
if errorlevel 1 goto :fail

echo.
echo OK. Gerados: SoraTrainer.exe e SoraSelfTest.exe
echo.
echo Se o Windows Defender apagar os arquivos, rode primeiro
echo ADICIONAR_EXCLUSAO_DEFENDER.bat (trainers sempre sao falso positivo).
pause
exit /b 0

:fail
echo.
echo [ERRO] Falha na compilacao.
pause
exit /b 1
