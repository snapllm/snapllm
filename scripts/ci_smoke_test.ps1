param(
    [Parameter(Mandatory = $true)][string]$SnapllmPath,
    [string]$BindHost = "127.0.0.1",
    [int]$Port = 6930,
    [int]$TimeoutSec = 60
)

if (-not (Test-Path $SnapllmPath)) {
    throw "snapllm binary not found at: $SnapllmPath"
}

$logDir = Join-Path $env:TEMP "snapllm-ci"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stdoutLog = Join-Path $logDir "snapllm-stdout.log"
$stderrLog = Join-Path $logDir "snapllm-stderr.log"
if (Test-Path $stdoutLog) { Remove-Item $stdoutLog -Force }
if (Test-Path $stderrLog) { Remove-Item $stderrLog -Force }

$proc = Start-Process `
    -FilePath $SnapllmPath `
    -ArgumentList "--server --host $BindHost --port $Port" `
    -WorkingDirectory (Get-Location) `
    -PassThru `
    -NoNewWindow `
    -RedirectStandardOutput $stdoutLog `
    -RedirectStandardError $stderrLog

$healthy = $false

try {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        if ($proc -and $proc.HasExited) {
            $stdoutTail = if (Test-Path $stdoutLog) { Get-Content $stdoutLog -Tail 200 -ErrorAction SilentlyContinue } else { @() }
            $stderrTail = if (Test-Path $stderrLog) { Get-Content $stderrLog -Tail 200 -ErrorAction SilentlyContinue } else { @() }
            throw ("snapllm exited early (code {0}).`nstdout:`n{1}`nstderr:`n{2}" -f $proc.ExitCode, ($stdoutTail -join "`n"), ($stderrTail -join "`n"))
        }

        try {
            $resp = Invoke-WebRequest -Uri "http://$BindHost:$Port/health" -UseBasicParsing -TimeoutSec 2
            if ($resp.StatusCode -eq 200) {
                $healthy = $true
                break
            }
        } catch {
            Start-Sleep -Seconds 1
        }
    }

    if (-not $healthy) {
        $stdoutTail = if (Test-Path $stdoutLog) { Get-Content $stdoutLog -Tail 200 -ErrorAction SilentlyContinue } else { @() }
        $stderrTail = if (Test-Path $stderrLog) { Get-Content $stderrLog -Tail 200 -ErrorAction SilentlyContinue } else { @() }
        throw ("Health endpoint did not become ready within {0}s.`nstdout:`n{1}`nstderr:`n{2}" -f $TimeoutSec, ($stdoutTail -join "`n"), ($stderrTail -join "`n"))
    }
} finally {
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
    }
}
