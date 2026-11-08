<#
Purpose: Install the pinned official Nuvoton OpenOCD Windows distribution.
Author: TinyML Hearing Aids contributors
Date: 2026-10-24
#>
[CmdletBinding()]
param([string]$ArchivePath)

$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
    $stream = [System.IO.File]::OpenRead($Path)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $sha.Dispose()
    }
}

$distribution = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'distribution.json') | ConvertFrom-Json
if (-not $ArchivePath) {
    $ArchivePath = Join-Path $PSScriptRoot $distribution.archive
    if (-not (Test-Path -LiteralPath $ArchivePath)) {
        Invoke-WebRequest -UseBasicParsing -Uri $distribution.url -OutFile $ArchivePath
    }
}
$ArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
if ((Get-Sha256 $ArchivePath) -ne $distribution.sha256) {
    throw 'Archive SHA-256 mismatch. The pinned Nuvoton distribution was not installed.'
}

$runtime = Join-Path $PSScriptRoot '.runtime'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
& tar -xzf $ArchivePath -C $runtime
if ($LASTEXITCODE -ne 0) {
    throw 'Could not extract the Nuvoton OpenOCD distribution.'
}
foreach ($file in $distribution.files.PSObject.Properties) {
    $path = Join-Path $runtime $file.Name
    if ((Get-Sha256 $path) -ne $file.Value) {
        throw "Installed file SHA-256 mismatch: $($file.Name)"
    }
}
Write-Host "Installed Nuvoton OpenOCD $($distribution.version) in $runtime"
