$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$modelPath = (Read-Host 'Enter path to your .gguf model file').Trim().Trim('"')
$modelName = Read-Host 'Enter a name for this model'
if ($modelName -notmatch '^[A-Za-z0-9_.-]{1,128}$') {
    throw 'Invalid model name. Use 1-128 letters, digits, dots, underscores, or hyphens.'
}
try {
    $modelPath = [IO.Path]::GetFullPath($modelPath)
} catch {
    throw 'Invalid model path. Enter an absolute path to an existing .gguf file.'
}
if ([IO.Path]::GetExtension($modelPath) -ine '.gguf' -or
    -not (Test-Path -LiteralPath $modelPath -PathType Leaf)) {
    throw 'Invalid model path. Use an existing absolute .gguf path.'
}
$binary = Join-Path $PSScriptRoot 'bin\snapllm.exe'
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
    throw "SnapLLM binary not found: $binary"
}
& $binary --server --port 6930 --ui-dir (Join-Path $PSScriptRoot 'ui') --load-model $modelName $modelPath
if ($LASTEXITCODE -ne 0) {
    throw "SnapLLM exited with code $LASTEXITCODE"
}
