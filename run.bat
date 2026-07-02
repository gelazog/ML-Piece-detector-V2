@echo off
rem Doble clic para verificar requisitos, compilar si falta y lanzar PC Inspector.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" %*
pause
