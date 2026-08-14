param([string]$SnapLLM = "$PSScriptRoot\snapllm.exe")
$task = "SnapLLM Local API"
$resolvedSnapLLM = (Resolve-Path -LiteralPath $SnapLLM -ErrorAction Stop).Path
$workingDirectory = Split-Path -Parent $resolvedSnapLLM
$action = New-ScheduledTaskAction -Execute $resolvedSnapLLM -Argument "--server --host 127.0.0.1 --port 6930" -WorkingDirectory $workingDirectory
$trigger = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet `
  -StartWhenAvailable `
  -RestartCount 5 `
  -RestartInterval (New-TimeSpan -Minutes 1) `
  -ExecutionTimeLimit ([TimeSpan]::Zero)
Register-ScheduledTask -TaskName $task -Action $action -Trigger $trigger -Settings $settings -Description "SnapLLM user-local inference API (restart on failure)" -Force | Out-Null
Write-Host "Installed $task for the current user."
Write-Host "The task restarts the daemon up to five times at one-minute intervals after an unexpected exit."
