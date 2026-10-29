@echo off
rem Projeto Deveraux - Configurar-Repos.bat
rem Cria os repositorios Git locais de Content e Arte, com LFS, e as copias
rem no disco de backup. Rode uma vez, depois do primeiro push do codigo.
setlocal
cd /d "%~dp0"
echo Configura os repositorios de Content e Arte, com copia em outro disco.
echo.

rem Pre-requisitos da Etapa 4: Git, Git LFS, nome e e-mail
git --version >nul 2>&1 || goto :semGit
git lfs version >nul 2>&1 || goto :semLfs
git config user.email >nul 2>&1 || goto :semIdentidade

set "BK="
set /p "BK=Pasta de backup, com a letra do disco e sem espacos (exemplo: E:\Backup\Deveraux): "
if not defined BK goto :semPasta
set "BK=%BK:"=%"
if not defined BK goto :semPasta
if not "%BK:~1,1%"==":" goto :semLetra
if not "%BK: =%"=="%BK%" goto :comEspaco
if "%BK:~-1%"=="\" set "BK=%BK:~0,-1%"

rem A copia nao pode ficar dentro do projeto: o Git do codigo a mandaria para o GitHub
set "PROJ=%~dp0"
set "PROJ=%PROJ:~0,-1%"
if /i "%BK%"=="%PROJ%" goto :dentroDoProjeto
call set "FORA=%%BK:%PROJ%\=%%"
if /i not "%FORA%"=="%BK%" goto :dentroDoProjeto

if not exist "%BK%\" mkdir "%BK%"
if not exist "%BK%\" goto :semDisco
set "BKURL=%BK:\=/%"

call :configura Content
call :configura Arte

echo.
echo Pronto. No fim de cada sessao, use o Salvar.bat.
pause
exit /b 0

rem :configura PASTA - cria o repositorio da pasta e a copia no disco de backup
:configura
echo.
echo == %1
if not exist "%1\.gitattributes" (
  echo Falta o arquivo %1\.gitattributes do pacote. Copie e rode de novo.
  exit /b 0
)
pushd "%1"
if not exist ".git\" git init -q -b main
git lfs install >nul 2>&1
git rev-parse -q --verify HEAD >nul 2>&1
if errorlevel 1 (
  git add -A
  git commit -q --allow-empty -m "Inicio do repositorio %1"
)
if not exist "%BK%\%1.git\" git init -q --bare -b main "%BK%\%1.git"
rem Discos que nao registram dono de arquivo, como exFAT e FAT32, exigem esta liberacao
git config --global --get-all safe.directory 2>nul | findstr /l /i /x /c:"%BKURL%/%1.git" >nul || git config --global --add safe.directory "%BKURL%/%1.git"
git remote remove origin >nul 2>&1
git remote add origin "file:///%BKURL%/%1.git"
git push -u origin main
popd
exit /b 0

:semGit
echo O Git nao foi encontrado. Instale o Git for Windows, como na Etapa 4.
goto :erro
:semLfs
echo O Git LFS nao foi encontrado. Reinstale o Git for Windows com a opcao Git LFS marcada.
goto :erro
:semIdentidade
echo Falta configurar nome e e-mail do Git. Rode os comandos git config da Etapa 4.
goto :erro
:semPasta
echo Nenhuma pasta informada. Nada foi feito.
goto :erro
:semLetra
echo Use o caminho completo, com a letra do disco, por exemplo E:\Backup\Deveraux.
goto :erro
:comEspaco
echo O caminho nao pode ter espacos. Escolha outra pasta.
goto :erro
:dentroDoProjeto
echo A pasta de backup nao pode ficar dentro da pasta do projeto.
goto :erro
:semDisco
echo Nao consegui criar a pasta "%BK%". Confira se o disco esta conectado.
goto :erro
:erro
pause
exit /b 1
