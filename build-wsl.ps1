param(
    [string]$Distribution = $env:BYTEBEAT_WSL_DISTRO
)
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Distribution)) {
    $installed = @(wsl --list --quiet |
        ForEach-Object { ($_ -replace "`0", '').Trim() } |
        Where-Object { $_ -and $_ -notmatch '^docker-desktop' })
    if ($installed.Count -gt 0) {
        $Distribution = [string]$installed[0]
    }
}

if ([string]::IsNullOrWhiteSpace($Distribution) -or $Distribution -match '^docker-desktop') {
    throw 'No usable Linux WSL distribution is installed. Install one with: wsl --install -d Ubuntu'
}

$projectPath = (Resolve-Path $PSScriptRoot).Path
$drive = $projectPath.Substring(0, 1).ToLowerInvariant()
$wslPath = "/mnt/$drive" + ($projectPath.Substring(2) -replace '\\', '/')

if ([string]::IsNullOrWhiteSpace($wslPath)) {
    throw "Could not convert project path to a WSL path."
}

$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
wsl -d $Distribution -- bash -lc "cd '$wslPath' && bash ./build-linux.sh"
$buildExitCode = $LASTEXITCODE
$ErrorActionPreference = $previousErrorActionPreference
if ($buildExitCode -ne 0) {
    exit $buildExitCode
}