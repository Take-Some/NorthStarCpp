@echo off
setlocal
python "%~dp0launch_qa.py" %*
exit /b %ERRORLEVEL%
