@echo off
:: run.bat — launch AlphaBetaNew engine (used by UCI GUIs like UciBoardArena)
:: Point your GUI's engine path to this file.

set DIR=%~dp0
if not exist "%DIR%AlphaBetaNew.exe" (
    echo Binary not found. Running build.bat first...
    call "%DIR%build.bat"
)
"%DIR%AlphaBetaNew.exe"
