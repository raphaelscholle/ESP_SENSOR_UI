# Run this from a PowerShell window with admin rights to point PlatformIO at a specific Python.
# Update the path below to your installed Python (3.13 or newer) if it differs.
$pythonPath = "C:\\Users\\$env:USERNAME\\AppData\\Local\\Programs\\Python\\Python313\\python.exe"

[Environment]::SetEnvironmentVariable("PIO_USE_NATIVE_PYTHON", "1", "User")
[Environment]::SetEnvironmentVariable("PLATFORMIO_PYTHON_EXE", $pythonPath, "User")
Write-Output "PlatformIO Python set to: $pythonPath"
Write-Output "Restart VS Code and reopen the project to apply."
