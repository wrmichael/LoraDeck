@echo off
rem Restarts the LoRaDeck and saves its console for 90 seconds to tools\monitor.log.
"C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe" "%~dp0capture.py" %*
timeout /t 5
