[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('gdbserver', 'require-flash-verification')]
    [string]$Command
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$pyocd = 'C:\Users\proha\AppData\Local\Programs\Python\Python310\Scripts\pyocd.exe'
$scriptPath = Join-Path $PSScriptRoot 'm55m1r2ljae.py'
$verificationMarker = Join-Path $PSScriptRoot '.flash-verified.json'

if (-not (Test-Path -LiteralPath $pyocd)) {
    throw "pyOCD was not found at $pyocd. Update this wrapper and launch.json together."
}

switch ($Command) {
    'gdbserver' {
        & $pyocd gdbserver -t m55m1r2ljae -u 0001A0000001 --connect attach -f 100000 --persist --script $scriptPath
        exit $LASTEXITCODE
    }
    'require-flash-verification' {
        if (-not (Test-Path -LiteralPath $verificationMarker)) {
            throw 'Flash launch is locked. Complete and record the one-page APROM verification first; attach-only remains safe.'
        }
        $record = Get-Content -Raw -LiteralPath $verificationMarker | ConvertFrom-Json
        if ($record.target -ne 'M55M1R2LJAE' -or $record.page -ne '0x002FE000' -or -not $record.verifiedAt) {
            throw 'The Flash verification record is invalid. Re-run the documented one-page verification.'
        }
    }
}
