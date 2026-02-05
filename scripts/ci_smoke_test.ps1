param(
    [Parameter(Mandatory = $true)][string]$SnapllmPath,
    [int]$Port = 6930,
    [int]$TimeoutSec = 30
)

if (-not (Test-Path $SnapllmPath)) {
    throw "snapllm binary not found at: $SnapllmPath"
}

$proc = Start-Process -FilePath $SnapllmPath -ArgumentList "--server --port $Port" -PassThru -WindowStyle Hidden
$healthy = $false

try {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $resp = Invoke-WebRequest -Uri "http://localhost:$Port/health" -UseBasicParsing -TimeoutSec 2
            if ($resp.StatusCode -eq 200) {
                $healthy = $true
                break
            }
        } catch {
            Start-Sleep -Seconds 1
        }
    }

    if (-not $healthy) {
        throw "Health endpoint did not become ready within ${TimeoutSec}s"
    }
} finally {
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
    }
}