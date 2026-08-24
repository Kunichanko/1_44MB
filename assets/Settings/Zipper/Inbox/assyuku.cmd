@echo off
del /f /q "%~dp0assyuku.request" >nul 2>nul
> "%~dp0assyuku.request" echo animate %DATE%_%TIME%_%RANDOM%_%RANDOM%
