# Lists every entry in a standalone .rpf (including nested archives) using CodeWalker.Core.
# Resource entries show their RSC version number (legacy vs gen9 formats differ).
param(
    [Parameter(Mandatory)] [string] $Rpf,
    [string] $GameDir = 'E:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced',
    [string] $CodeWalkerDir = 'D:\Development\CodeWalker30_dev48\CodeWalker30_dev48'
)
$ErrorActionPreference = 'Stop'
Get-ChildItem $CodeWalkerDir -Filter 'SharpDX*.dll' | ForEach-Object { [void][Reflection.Assembly]::LoadFrom($_.FullName) }
[void][Reflection.Assembly]::LoadFrom((Join-Path $CodeWalkerDir 'CodeWalker.Core.dll'))
[CodeWalker.GameFiles.GTA5Keys]::LoadFromPath($GameDir, $true, $null)

$file = New-Object CodeWalker.GameFiles.RpfFile($Rpf, [IO.Path]::GetFileName($Rpf))
$file.ScanStructure([Action[string]]{ param($s) }, [Action[string]]{ param($s) Write-Host "err: $s" })

function Show($rpf) {
    Write-Host ("[{0}] encryption={1}" -f $rpf.Path, $rpf.Encryption)
    foreach ($e in $rpf.AllEntries) {
        if ($e -is [CodeWalker.GameFiles.RpfResourceFileEntry]) {
            Write-Host ("   {0,-70} rsc v{1,-4} {2,10}" -f $e.Path, $e.Version, $e.FileSize)
        } elseif ($e -is [CodeWalker.GameFiles.RpfFileEntry]) {
            Write-Host ("   {0,-70} {1,15}" -f $e.Path, $e.FileSize)
        }
    }
    if ($rpf.Children) { foreach ($c in $rpf.Children) { Show $c } }
}
Show $file
