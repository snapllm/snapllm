param([string]$SnapLLM = "$PSScriptRoot\snapllm.exe")
$task = "SnapLLM Local API"
$action = New-ScheduledTaskAction -Execute $SnapLLM -Argument "--server --host 127.0.0.1 --port 6930"
$trigger = New-ScheduledTaskTrigger -AtLogOn
Register-ScheduledTask -TaskName $task -Action $action -Trigger $trigger -Description "SnapLLM user-local inference API" -Force | Out-Null
Write-Host "Installed $task for the current user."
