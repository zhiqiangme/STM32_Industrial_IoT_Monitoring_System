@echo off
cd /d "%~dp0"
tree /f /a > list.txt
start list.txt