@echo off
REM Setup dual push remotes (GitHub + Gitee) for current repository

for /f %%I in ('git rev-parse --show-toplevel') do set REPO_NAME=%%~nxI
set GITHUB_URL=https://github.com/chenyananee/%REPO_NAME%.git
set GITEE_URL=https://gitee.com/chenyananee/%REPO_NAME%.git

git remote set-url --add --push origin %GITEE_URL%
git remote set-url --add --push origin %GITHUB_URL%

echo Dual push remotes configured:
git remote -v | findstr push
