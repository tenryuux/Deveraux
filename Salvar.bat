@echo off
rem Projeto Deveraux - Salvar.bat
rem Grava e envia os tres repositorios: codigo para o GitHub,
rem Content e Arte para o disco de backup. O codigo tambem traz o que
rem foi aceito no GitHub (pull requests) antes de enviar.
setlocal
cd /d "%~dp0"
echo Grava e envia os tres repositorios: codigo para o GitHub,
echo Content e Arte para o disco de backup. Salve tudo no editor antes.
echo.
set "FALHOU="
set "CODIGONOVO="
set "MSG="
set /p "MSG=O que mudou nesta sessao? "
if not defined MSG set "MSG=Salvamento"
set "MSG=%MSG:"=%"
if not defined MSG set "MSG=Salvamento"

call :salva . Codigo sincroniza
call :salva Content Content
call :salva Arte Arte

echo.
if defined CODIGONOVO (
  echo Chegou codigo novo do GitHub. Feche o editor, gere os arquivos do
  echo Visual Studio e compile de novo antes de abrir o projeto.
  echo.
)
if defined FALHOU (
  echo Terminou com erro. Me mande o texto acima.
) else (
  echo Pronto.
)
pause
exit /b 0

rem :salva PASTA NOME [sincroniza] - grava as mudancas da pasta, traz o que
rem mudou no GitHub (so com sincroniza) e envia para o remoto da pasta
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
if not "%3"=="sincroniza" goto :envia

rem Traz o que foi aceito no GitHub e poe as mudancas daqui por cima
set "ANTES="
for /f %%h in ('git rev-parse HEAD') do set "ANTES=%%h"
git pull -q --rebase --autostash
if errorlevel 1 goto :falhaSincronia
set "DEPOIS="
for /f %%h in ('git rev-parse HEAD') do set "DEPOIS=%%h"
if not "%ANTES%"=="%DEPOIS%" set "CODIGONOVO=1"

:envia
git push -q
if errorlevel 1 goto :falhaEnvio
popd
exit /b 0

:falhaSincronia
git rebase --abort >nul 2>&1
echo ERRO: nao deu para juntar o que esta no GitHub com o que mudou aqui.
echo Nada foi perdido: as mudancas daqui continuam gravadas no seu PC.
set "FALHOU=1"
popd
exit /b 0

:falhaEnvio
echo ERRO: o envio de %2 falhou.
set "FALHOU=1"
popd
exit /b 0
