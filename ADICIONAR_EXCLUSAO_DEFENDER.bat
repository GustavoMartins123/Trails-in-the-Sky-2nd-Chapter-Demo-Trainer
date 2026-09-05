@echo off
:: ---------------------------------------------------------------------------
::  Adiciona a pasta do trainer na lista de exclusoes do Windows Defender.
::
::  Por que isso e necessario: o trainer abre o processo do jogo e escreve na
::  memoria dele. Esse e exatamente o comportamento que o antivirus classifica
::  como "HackTool". Nao ha assinatura de malware aqui - o codigo-fonte inteiro
::  esta em .\src e voce pode recompilar com build.bat.
::
::  Pede elevacao (UAC) porque so um administrador altera exclusoes.
:: ---------------------------------------------------------------------------
setlocal

net session >nul 2>&1
if %errorlevel% neq 0 (
  echo Pedindo permissao de administrador...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)

set "ALVO=%~dp0"
if "%ALVO:~-1%"=="\" set "ALVO=%ALVO:~0,-1%"

echo Excluindo do Defender: "%ALVO%"
powershell -NoProfile -Command "Add-MpPreference -ExclusionPath '%ALVO%'"

if errorlevel 1 (
  echo.
  echo [ERRO] Nao foi possivel adicionar a exclusao.
  echo Adicione manualmente: Seguranca do Windows ^> Protecao contra virus
  echo  ^> Gerenciar configuracoes ^> Exclusoes ^> Adicionar pasta.
) else (
  echo.
  echo Exclusao adicionada. Agora rode build.bat para gerar o executavel.
)
echo.
pause
