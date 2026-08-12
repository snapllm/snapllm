$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$modelPath = Read-Host 'Enter path to your .gguf model file'
$modelName = Read-Host 'Enter a name for this model'
if ($modelName -notmatch '^[A-Za-z0-9_.-]{1,128}$') {
    throw 'Invalid model name. Use 1-128 letters, digits, dots, underscores, or hyphens.'
}
if ([IO.Path]::GetExtension($modelPath) -ine '.gguf' -or
    -not [IO.Path]::IsPathFullyQualified($modelPath) -or
    -not (Test-Path -LiteralPath $modelPath -PathType Leaf)) {
    throw 'Invalid model path. Use an existing absolute .gguf path.'
}
& "$PSScriptRoot\bin\snapllm.exe" --server --port 6930 --ui-dir "$PSScriptRoot\ui" --load-model $modelName $modelPath
