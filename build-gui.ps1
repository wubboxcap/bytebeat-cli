$ErrorActionPreference = 'Stop'

$vcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
$vcVersion = Get-ChildItem $vcRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdkIncludeRoot = Join-Path $sdkRoot 'Include'
$sdkLibRoot = Join-Path $sdkRoot 'Lib'
$sdkVersion = Get-ChildItem $sdkIncludeRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkLibVersion = Get-ChildItem $sdkLibRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1

if ($null -eq $vcVersion -or $null -eq $sdkVersion -or $null -eq $sdkLibVersion) {
    throw 'Visual Studio Build Tools and the Windows SDK are required.'
}

$vcLib = Join-Path $vcVersion.FullName 'lib\x64'
$vcInclude = Join-Path $vcVersion.FullName 'include'
$sdkInclude = $sdkVersion.FullName
$sdkLib = $sdkLibVersion.FullName
$arguments = @(
    '--target=x86_64-pc-windows-msvc'
    '-std=c11'
    '-O2'
    '-DBYTEBEAT_GUI'
    '-D__EMSCRIPTEN__'
    '-D_CRT_SECURE_NO_WARNINGS'
    '-Icompat'
    "-I$vcInclude"
    "-I$sdkInclude\ucrt"
    "-I$sdkInclude\shared"
    "-I$sdkInclude\um"
    '-include'
    'malloc.h'
    '-include'
    'bytebeat_version.h'
    'bytebeat.c'
    'strict_expr.c'
    'compiler_rt_fallback.c'
    'quickjs.c'
    'cutils.c'
    'dtoa.c'
    'libregexp.c'
    'libunicode.c'
    '-fuse-ld=lld'
    '-Xlinker'
    '/subsystem:windows'
    '-Xlinker'
    "/libpath:$vcLib"
    '-Xlinker'
    "/libpath:$sdkLib\ucrt\x64"
    '-Xlinker'
    "/libpath:$sdkLib\um\x64"
    '-lwinmm'
    '-lcomctl32'
    '-lcomdlg32'
    '-luser32'
    '-lgdi32'
    '-o'
    'bytebeat-gui.exe'
)

& clang @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}