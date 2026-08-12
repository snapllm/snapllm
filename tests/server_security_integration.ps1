param(
    [Parameter(Mandatory = $true)]
    [string]$ServerExecutable
)

$ErrorActionPreference = "Stop"
$apiKey = "integration-test-key-000000000000000000000000"
$port = Get-Random -Minimum 18000 -Maximum 28000
$baseUrl = "http://127.0.0.1:$port"
$serverProcess = $null

function Invoke-Status {
    param(
        [string]$Path,
        [string[]]$Headers = @(),
        [string]$Method = "GET",
        [string]$Body = ""
    )

    $arguments = @("--max-time", "10", "-sS", "-o", "NUL", "-w", "%{http_code}", "-X", $Method)
    $bodyFile = $null
    foreach ($header in $Headers) {
        $arguments += @("-H", $header)
    }
    if ($Body.Length -gt 0) {
        $bodyFile = Join-Path $env:TEMP "snapllm-request-$([Guid]::NewGuid()).json"
        [System.IO.File]::WriteAllText($bodyFile, $Body, [System.Text.UTF8Encoding]::new($false))
        $arguments += @("-H", "Content-Type: application/json", "--data-binary", "@$bodyFile")
    }
    $arguments += "$baseUrl$Path"
    try {
        $status = & curl.exe @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "curl failed for $Method $Path with exit code $LASTEXITCODE"
        }
        return [int]$status
    } finally {
        if ($bodyFile) {
            Remove-Item -LiteralPath $bodyFile -Force -ErrorAction SilentlyContinue
        }
    }
}

function Assert-Status {
    param([int]$Actual, [int]$Expected, [string]$Case)
    if ($Actual -ne $Expected) {
        throw "$Case returned HTTP $Actual; expected $Expected"
    }
}

$resolvedExecutable = (Resolve-Path -LiteralPath $ServerExecutable).Path
try {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $resolvedExecutable
    $startInfo.Arguments = "--server --host 127.0.0.1 --port $port --cors-origin http://127.0.0.1:$port"
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $false
    $startInfo.RedirectStandardError = $false
    $startInfo.Environment["SNAPLLM_API_KEY"] = $apiKey
    $serverProcess = [System.Diagnostics.Process]::Start($startInfo)

    $ready = $false
    for ($attempt = 0; $attempt -lt 100; $attempt++) {
        if ($serverProcess.HasExited) {
            throw "Server exited before becoming ready with code $($serverProcess.ExitCode)"
        }
        try {
            if ((Invoke-Status -Path "/health") -eq 200) {
                $ready = $true
                break
            }
        } catch {
            Start-Sleep -Milliseconds 100
        }
    }
    if (-not $ready) {
        throw "Server did not become ready"
    }

    Assert-Status (Invoke-Status -Path "/health") 200 "public health"
    Assert-Status (Invoke-Status -Path "/api/v1/config") 401 "missing API key"
    Assert-Status `
        (Invoke-Status -Path "/api/v1/config" -Headers @("Authorization: Bearer $apiKey")) `
        200 "Bearer API key"
    Assert-Status `
        (Invoke-Status -Path "/api/v1/config" -Headers @("X-API-Key: $apiKey")) `
        200 "X-API-Key"
    Assert-Status `
        (Invoke-Status -Path "/api/v1/config" -Headers @(
            "Authorization: Bearer $apiKey",
            "Origin: http://127.0.0.1:$port"
        )) `
        200 "approved browser Origin"
    Assert-Status `
        (Invoke-Status -Path "/api/v1/config" -Headers @("Host: evil.example")) `
        400 "forged Host"
    Assert-Status `
        (Invoke-Status -Path "/api/v1/config" -Headers @(
            "Authorization: Bearer $apiKey",
            "Origin: https://evil.example"
        )) `
        403 "unapproved browser Origin"
    Assert-Status `
        (Invoke-Status -Path "/api/v1/config" -Headers @(
            "Authorization: Bearer $apiKey",
            "Origin: http://127%2E0%2E0%2E1:$port"
        )) `
        403 "percent-encoded Origin alias"
    Assert-Status `
        (Invoke-Status -Path "/api/v1/config" -Headers @(
            "Authorization: Bearer $apiKey",
            "Host: 127%2E0%2E0%2E1:$port"
        )) `
        400 "percent-encoded Host alias"
    Assert-Status `
        (Invoke-Status -Path "/api/v1/config" -Method "POST" `
            -Headers @("Authorization: Bearer $apiKey") `
            -Body '{"host":"127.0.0.1\r\nX-Injected: true"}') `
        400 "configuration header injection"
    $negativeTokenStatus = Invoke-Status -Path "/api/v1/generate" -Method "POST" `
        -Headers @("Authorization: Bearer $apiKey") `
        -Body '{"prompt":"test","max_tokens":-1}'
    if ($negativeTokenStatus -notin @(400, 422)) {
        throw "negative token limit returned HTTP $negativeTokenStatus; expected 400 or 422"
    }
    Assert-Status `
        (Invoke-Status -Path "/api/v1/models/scan" -Method "POST" `
            -Headers @("Authorization: Bearer $apiKey") `
            -Body '{"path":"C:\\Windows"}') `
        400 "folder scan outside configured roots"
    Assert-Status `
        (Invoke-Status -Path "/api/v1/contexts/ingest" -Method "POST" `
            -Headers @("Authorization: Bearer $apiKey") `
            -Body '{"content":"test","model_id":"missing","dtype":"executable"}') `
        400 "invalid context dtype"
    $unsupportedImageStatus = Invoke-Status -Path "/v1/messages" -Method "POST" `
        -Headers @("Authorization: Bearer $apiKey") `
        -Body '{"model":"missing","max_tokens":8,"messages":[{"role":"user","content":[{"type":"image","source":{"type":"base64","media_type":"image/png","data":"AA=="}}]}]}'
    if ($unsupportedImageStatus -notin @(422, 501)) {
        throw "unsupported Messages image block returned HTTP $unsupportedImageStatus; expected 422 or 501"
    }
} finally {
    if ($serverProcess -and -not $serverProcess.HasExited) {
        $serverProcess.Kill()
        $serverProcess.WaitForExit()
    }
}

$publicStartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$publicStartInfo.FileName = $resolvedExecutable
$publicStartInfo.Arguments = "--server --host 0.0.0.0 --port $port"
$publicStartInfo.UseShellExecute = $false
$publicStartInfo.CreateNoWindow = $true
$publicStartInfo.RedirectStandardOutput = $false
$publicStartInfo.RedirectStandardError = $false
[void]$publicStartInfo.Environment.Remove("SNAPLLM_API_KEY")
$publicProcess = [System.Diagnostics.Process]::Start($publicStartInfo)
if (-not $publicProcess.WaitForExit(10000)) {
    $publicProcess.Kill()
    throw "Unauthenticated public bind did not fail promptly"
}
if ($publicProcess.ExitCode -eq 0) {
    throw "Unauthenticated public bind unexpectedly succeeded"
}

$unproxiedStartInfo = [System.Diagnostics.ProcessStartInfo]::new()
$unproxiedStartInfo.FileName = $resolvedExecutable
$unproxiedStartInfo.Arguments = "--server --host 0.0.0.0 --port $port"
$unproxiedStartInfo.UseShellExecute = $false
$unproxiedStartInfo.CreateNoWindow = $true
$unproxiedStartInfo.RedirectStandardOutput = $false
$unproxiedStartInfo.RedirectStandardError = $false
$unproxiedStartInfo.Environment["SNAPLLM_API_KEY"] = $apiKey
[void]$unproxiedStartInfo.Environment.Remove("SNAPLLM_NETWORK_GUARD")
$unproxiedProcess = [System.Diagnostics.Process]::Start($unproxiedStartInfo)
if (-not $unproxiedProcess.WaitForExit(10000)) {
    $unproxiedProcess.Kill()
    throw "Unproxied public bind did not fail promptly"
}
if ($unproxiedProcess.ExitCode -eq 0) {
    throw "Unproxied public bind unexpectedly succeeded"
}

Write-Host "server_security_integration: all checks passed"
