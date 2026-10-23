$ErrorActionPreference = 'Stop'

$vcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
$vcVersion = Get-ChildItem $vcRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10\Lib'
$sdkVersion = Get-ChildItem $sdkRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1

if ($null -eq $vcVersion -or $null -eq $sdkVersion) {
    throw 'Visual Studio Build Tools and the Windows SDK are required.'
}

$vcLib = Join-Path $vcVersion.FullName 'lib\x64'
$sdkLib = $sdkVersion.FullName
$arguments = @(
    '--target=x86_64-pc-windows-msvc'
    '-std=c11'
    '-O2'
    '-D__EMSCRIPTEN__'
    '-D_CRT_SECURE_NO_WARNINGS'
    '-Icompat'
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
    "-Xlinker"
    "/libpath:$vcLib"
    "-Xlinker"
    "/libpath:$sdkLib\ucrt\x64"
    "-Xlinker"
    "/libpath:$sdkLib\um\x64"
    '-o'
    'bytebeat.exe'
)

& clang @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}