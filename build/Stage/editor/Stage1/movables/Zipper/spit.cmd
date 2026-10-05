@echo off
del /f /q "%~dp0zipper_command.request" >nul 2>nul
> "%~dp0zipper_command.request" echo spit %DATE%_%TIME%_%RANDOM%_%RANDOM%
