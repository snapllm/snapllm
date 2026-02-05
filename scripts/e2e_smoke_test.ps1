param(
    [string]$SnapllmPath,
    [int]$Port = 6930,
    [int]$TimeoutSec = 120,
    [string]$ModelsRoot = 'D:\\Models',
    [string]$LlmPath,
    [string]$DiffusionPath,
    [string]$VisionPath,
    [string]$MmprojPath,
    [string]$VisionImage,
    [switch]$SkipDiffusion,
    [switch]$SkipVision,
    [switch]$SkipContexts
)

$ErrorActionPreference = 'Stop'

function Resolve-ExistingPath([string[]]$candidates) {
    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
        if (Test-Path -Path $candidate) {
            return (Resolve-Path -Path $candidate).Path
        }
    }
    return $null
}

function Require-Path([string]$label, [string]$path) {
    if (-not (Test-Path -Path $path)) {
        throw "$label not found: $path"
    }
    return (Resolve-Path -Path $path).Path
}

function Resolve-SnapllmPath {
    if ($SnapllmPath) {
        return (Require-Path 'snapllm.exe' $SnapllmPath)
    }
    $candidates = @(
        (Join-Path $PSScriptRoot '..\\build_codex_gpu4\\bin\\snapllm.exe'),
        (Join-Path $PSScriptRoot '..\\build_gpu\\bin\\snapllm.exe'),
        (Join-Path $PSScriptRoot '..\\bin\\snapllm.exe'),
        (Join-Path $PSScriptRoot '..\\snapllm.exe')
    )
    $resolved = Resolve-ExistingPath $candidates
    if (-not $resolved) {
        throw 'snapllm.exe not found. Pass -SnapllmPath explicitly.'
    }
    return $resolved
}

function Resolve-LlmPath {
    if ($LlmPath) {
        return (Require-Path 'LLM model' $LlmPath)
    }
    $candidates = @(
        (Join-Path $ModelsRoot 'tinyllama-1.1b-chat-v1.0.Q4_0.gguf'),
        (Join-Path $ModelsRoot 'tinyllama-1.1b-chat-v1.0.Q4_K_S.gguf'),
        (Join-Path $ModelsRoot 'tinyllama-1.1b-chat-v1.0.Q5_K_M.gguf')
    )
    $resolved = Resolve-ExistingPath $candidates
    if (-not $resolved) {
        throw "No TinyLlama GGUF found in $ModelsRoot. Pass -LlmPath explicitly."
    }
    return $resolved
}

function Resolve-DiffusionPath {
    if ($DiffusionPath) {
        return (Require-Path 'Diffusion model' $DiffusionPath)
    }
    $candidates = @(
        (Join-Path $ModelsRoot 'stable-diffusion-v1-5-Q4_0.gguf'),
        (Join-Path $ModelsRoot 'sd3.5-2b-lite-q4_k_m.gguf')
    )
    $resolved = Resolve-ExistingPath $candidates
    if (-not $resolved) {
        throw "No diffusion GGUF found in $ModelsRoot. Pass -DiffusionPath explicitly."
    }
    return $resolved
}

function Resolve-VisionPaths {
    if ($VisionPath -and $MmprojPath) {
        return @((Require-Path 'Vision model' $VisionPath), (Require-Path 'Vision projector' $MmprojPath))
    }

    if ($VisionPath -and -not $MmprojPath) {
        $resolvedVision = Require-Path 'Vision model' $VisionPath
        $visionName = [IO.Path]::GetFileName($resolvedVision)
        if ($visionName -match 'Qwen2\.5-Omni-3B') {
            $candidate = Join-Path $ModelsRoot 'mmproj-Qwen2.5-Omni-3B-Q8_0.gguf'
            return @($resolvedVision, (Require-Path 'Vision projector' $candidate))
        }
        throw 'MmprojPath is required for this vision model. Pass -MmprojPath explicitly.'
    }

    if (-not $VisionPath -and $MmprojPath) {
        throw 'VisionPath is required when MmprojPath is provided.'
    }

    $pairs = @(
        @{
            model = (Join-Path $ModelsRoot 'Qwen2.5-Omni-3B-4bit.gguf')
            mmproj = (Join-Path $ModelsRoot 'mmproj-Qwen2.5-Omni-3B-Q8_0.gguf')
        },
        @{
            model = (Join-Path $ModelsRoot 'Qwen2.5-Omni-3B-Q8_0.gguf')
            mmproj = (Join-Path $ModelsRoot 'mmproj-Qwen2.5-Omni-3B-Q8_0.gguf')
        }
    )

    foreach ($pair in $pairs) {
        if ((Test-Path -Path $pair.model) -and (Test-Path -Path $pair.mmproj)) {
            return @((Resolve-Path -Path $pair.model).Path, (Resolve-Path -Path $pair.mmproj).Path)
        }
    }

    throw "No vision model + mmproj pair found in $ModelsRoot. Pass -VisionPath and -MmprojPath explicitly."
}

function Resolve-VisionImagePath {
    if ($VisionImage) {
        return (Require-Path 'Vision image' $VisionImage)
    }
    $candidate = Join-Path $ModelsRoot 'test_cat.png'
    if (Test-Path -Path $candidate) {
        return (Resolve-Path -Path $candidate).Path
    }
    $fallback = Get-ChildItem -Path $ModelsRoot -Filter *.png -File -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($fallback) {
        return $fallback.FullName
    }
    throw "No PNG image found in $ModelsRoot. Pass -VisionImage explicitly."
}

function Wait-ForHealth {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $resp = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/health" -UseBasicParsing -TimeoutSec 2
            if ($resp.StatusCode -eq 200) {
                return $true
            }
        } catch {
            Start-Sleep -Seconds 1
        }
    }
    return $false
}

function Invoke-SnapLLM {
    param(
        [string]$Method,
        [string]$Path,
        [object]$Body,
        [int]$Timeout = 300
    )
    $uri = "http://127.0.0.1:$Port$Path"
    if ($Body -ne $null) {
        $json = $Body | ConvertTo-Json -Depth 8
        return Invoke-RestMethod -Method $Method -Uri $uri -Body $json -ContentType 'application/json' -TimeoutSec $Timeout
    }
    return Invoke-RestMethod -Method $Method -Uri $uri -TimeoutSec $Timeout
}

$resolvedSnapllm = Resolve-SnapllmPath
$resolvedLlm = Resolve-LlmPath
$resolvedDiffusion = $null
$resolvedVision = $null
$resolvedMmproj = $null
$resolvedVisionImage = $null

if (-not $SkipDiffusion) {
    $resolvedDiffusion = Resolve-DiffusionPath
}

if (-not $SkipVision) {
    $visionPair = Resolve-VisionPaths
    $resolvedVision = $visionPair[0]
    $resolvedMmproj = $visionPair[1]
    $resolvedVisionImage = Resolve-VisionImagePath
}

$startedServer = $false
$proc = $null

try {
    $health = $false
    try {
        $healthResp = Invoke-WebRequest -Uri "http://127.0.0.1:$Port/health" -UseBasicParsing -TimeoutSec 2
        $health = $healthResp.StatusCode -eq 200
    } catch {
        $health = $false
    }

    if (-not $health) {
        Write-Host "[Smoke] Starting SnapLLM server: $resolvedSnapllm"
        $proc = Start-Process -FilePath $resolvedSnapllm -ArgumentList @('--server', '--host', '127.0.0.1', '--port', $Port) -PassThru -WindowStyle Hidden
        $startedServer = $true
    } else {
        Write-Host "[Smoke] Using existing server on port $Port"
    }

    if (-not (Wait-ForHealth)) {
        throw "Server did not become healthy within ${TimeoutSec}s"
    }

    Write-Host '[Smoke] Health check OK'

    $llmId = 'tinyllama-smoke'
    Write-Host "[Smoke] Loading LLM: $resolvedLlm"
    Invoke-SnapLLM -Method POST -Path '/api/v1/models/load' -Body @{
        model_id = $llmId
        file_path = $resolvedLlm
        model_type = 'llm'
    } | Out-Null

    Write-Host '[Smoke] LLM generate'
    $llmResponse = Invoke-SnapLLM -Method POST -Path '/api/v1/generate' -Body @{
        prompt = 'Reply with the single word OK.'
        model = $llmId
        max_tokens = 8
        temperature = 0.0
        top_p = 1.0
        top_k = 1
        repeat_penalty = 1.0
    }
    if (-not $llmResponse.generated_text -or $llmResponse.generated_text.Trim().Length -eq 0) {
        throw 'LLM generation returned empty text'
    }

    if (-not $SkipContexts) {
        Write-Host '[Smoke] Context ingest'
        $ctx = Invoke-SnapLLM -Method POST -Path '/api/v1/contexts/ingest' -Body @{
            content = 'SnapLLM context cache smoke test. The answer should be OK.'
            model_id = $llmId
            name = 'smoke'
            source = 'e2e'
            ttl_seconds = 600
            priority = 'normal'
        }
        $contextId = $ctx.context_id
        if (-not $contextId) {
            throw 'Context ingest did not return a context_id'
        }

        Write-Host '[Smoke] Context query'
        $ctxResp = Invoke-SnapLLM -Method POST -Path ("/api/v1/contexts/$contextId/query") -Body @{
            query = 'Answer with OK.'
            max_tokens = 8
            temperature = 0.0
            top_p = 1.0
            top_k = 1
            repeat_penalty = 1.0
        }
        if (-not $ctxResp.status -or $ctxResp.status -ne 'success') {
            throw 'Context query failed'
        }
    }

    if (-not $SkipDiffusion) {
        $diffId = 'sd15-smoke'
        Write-Host "[Smoke] Loading diffusion model: $resolvedDiffusion"
        Invoke-SnapLLM -Method POST -Path '/api/v1/models/load' -Body @{
            model_id = $diffId
            file_path = $resolvedDiffusion
            model_type = 'diffusion'
        } | Out-Null

        Write-Host '[Smoke] Diffusion generate'
        $diff = Invoke-SnapLLM -Method POST -Path '/api/v1/diffusion/generate' -Body @{
            prompt = 'A small red cube on a white table'
            width = 128
            height = 128
            steps = 4
            cfg_scale = 4.0
            seed = 12345
        } -Timeout 600
        if (-not $diff.images -or $diff.images.Count -lt 1) {
            throw 'Diffusion did not return any images'
        }

        $outDir = Join-Path $PSScriptRoot '_smoke_out'
        New-Item -ItemType Directory -Path $outDir -Force | Out-Null
        $imgPath = Join-Path $outDir 'diffusion.png'
        Invoke-WebRequest -Uri $diff.images[0] -OutFile $imgPath -UseBasicParsing -TimeoutSec 120 | Out-Null
        if (-not (Test-Path -Path $imgPath)) {
            throw 'Failed to download diffusion image'
        }

        Write-Host '[Smoke] Diffusion video not supported check'
        try {
            Invoke-SnapLLM -Method POST -Path '/api/v1/diffusion/video' -Body @{ prompt = 'test' } -Timeout 60 | Out-Null
            throw 'Expected video endpoint to return not_supported'
        } catch {
            if (-not $_.Exception.Response) {
                throw
            }
            $status = [int]$_.Exception.Response.StatusCode
            if ($status -ne 501) {
                throw "Unexpected status from video endpoint: $status"
            }
        }
    }

    if (-not $SkipVision) {
        $visionId = 'vision-smoke'
        Write-Host "[Smoke] Loading vision model: $resolvedVision"
        Invoke-SnapLLM -Method POST -Path '/api/v1/models/load' -Body @{
            model_id = $visionId
            file_path = $resolvedVision
            model_type = 'vision'
            mmproj_path = $resolvedMmproj
            n_gpu_layers = -1
            ctx_size = 2048
            n_threads = 4
            use_gpu = $true
        } | Out-Null

        Write-Host '[Smoke] Vision generate'
        $bytes = [IO.File]::ReadAllBytes($resolvedVisionImage)
        $b64 = [Convert]::ToBase64String($bytes)
        $visionResp = Invoke-SnapLLM -Method POST -Path '/api/v1/vision/generate' -Body @{
            prompt = 'Describe this image in one short sentence.'
            image = $b64
            max_tokens = 32
            temperature = 0.0
            top_p = 1.0
            top_k = 1
            repeat_penalty = 1.0
        } -Timeout 600
        if (-not $visionResp.response -or $visionResp.response.Trim().Length -eq 0) {
            throw 'Vision response was empty'
        }
    }

    Write-Host '[Smoke] All checks passed'
} finally {
    if ($startedServer -and $proc -and -not $proc.HasExited) {
        Write-Host '[Smoke] Stopping server'
        Stop-Process -Id $proc.Id -Force
    }
}
