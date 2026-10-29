@echo off
rem Projeto Deveraux - Salvar.bat
rem Grava e envia os tres repositorios: codigo para o GitHub,
rem Content e Arte para o disco de backup.
setlocal
cd /d "%~dp0"
echo Grava e envia os tres repositorios: codigo para o GitHub,
echo Content e Arte para o disco de backup. Salve tudo no editor antes.
echo.
set "MSG="
set /p "MSG=O que mudou nesta sessao? "
if not defined MSG set "MSG=Salvamento"
set "MSG=%MSG:"=%"
if not defined MSG set "MSG=Salvamento"

call :salva . Codigo
call :salva Content Content
call :salva Arte Arte

echo.
echo Pronto. Se alguma linha acima mostrar erro, me mande o texto.
pause
exit /b 0

rem :salva PASTA NOME - grava as mudancas da pasta e envia para o remoto dela
:salva
echo.
echo == %2
if not exist "%1\.git\" (
  echo Repositorio nao encontrado. Rode o Configurar-Repos.bat.
  exit /b 0
)
pushd "%1"
git add -A
git diff --cached --quiet
if errorlevel 1 git commit -q -m "%MSG%"
git push -q
popd
exit /b 0
