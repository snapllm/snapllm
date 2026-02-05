param(
    [Parameter(Mandatory = $true)][string]$SnapllmPath,
    [int]$Port = 6930,
    [int]$TimeoutSec = 60
)

$envVars = @("SNAPLLM_MODEL1_ID", "SNAPLLM_MODEL1_PATH", "SNAPLLM_MODEL2_ID", "SNAPLLM_MODEL2_PATH")
$missing = $envVars | Where-Object { -not [System.Environment]::GetEnvironmentVariable($_) }
if ($missing.Count -gt 0) {
    Write-Host "Skipping model smoke test; set env vars: $($envVars -join ', ')"
    exit 0
}

if (-not (Test-Path $SnapllmPath)) {
    throw "snapllm binary not found at: $SnapllmPath"
}

$model1Id = $env:SNAPLLM_MODEL1_ID
$model1Path = $env:SNAPLLM_MODEL1_PATH
$model2Id = $env:SNAPLLM_MODEL2_ID
$model2Path = $env:SNAPLLM_MODEL2_PATH

$proc = Start-Process -FilePath $SnapllmPath -ArgumentList @(
    "--server",
    "--port", $Port,
    "--load-model", $model1Id, $model1Path
) -PassThru -WindowStyle Hidden
$healthy = $false

try {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $resp = Invoke-WebRequest -Uri "http://localhost:$Port/health" -UseBasicParsing -TimeoutSec 2
            if ($resp.StatusCode -eq 200) { $healthy = $true; break }
        } catch {
            Start-Sleep -Seconds 1
        }
    }

    if (-not $healthy) {
        throw "Health endpoint did not become ready within ${TimeoutSec}s"
    }

    $loadBody = @{ model_id = $model2Id; file_path = $model2Path } | ConvertTo-Json
    $loadResp = Invoke-WebRequest -Method POST -Uri "http://localhost:$Port/api/v1/models/load" -ContentType "application/json" -Body $loadBody -UseBasicParsing -TimeoutSec 30
    if ($loadResp.StatusCode -ne 200) { throw "Model load failed" }

    $switchBody = @{ model_id = $model2Id } | ConvertTo-Json
    $switchResp = Invoke-WebRequest -Method POST -Uri "http://localhost:$Port/api/v1/models/switch" -ContentType "application/json" -Body $switchBody -UseBasicParsing -TimeoutSec 30
    if ($switchResp.StatusCode -ne 200) { throw "Model switch failed" }
} finally {
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
    }
}
