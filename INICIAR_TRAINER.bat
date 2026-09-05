@echo off
cd /d "%~dp0"
if not exist SoraTrainer.exe (
  echo SoraTrainer.exe nao existe.
  echo Rode ADICIONAR_EXCLUSAO_DEFENDER.bat e depois build.bat.
  pause
  exit /b 1
)
start "" SoraTrainer.exe
