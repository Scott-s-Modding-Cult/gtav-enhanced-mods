# Adds DLC packs to dlclist.xml inside the *mods* copy of update.rpf (never the game's original).
# CodeWalker re-saves the archive with OPEN encryption, which OpenRPF loads from the mods folder.
#   & add-dlc-to-list.ps1 -Packs mpnaked
#   & add-dlc-to-list.ps1 -Packs mpnaked -Remove     # take entries out again
param(
    [Parameter(Mandatory)] [string[]] $Packs,
    [switch] $Remove,
    [string] $GameDir = 'E:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced',
    [string] $CodeWalkerDir = 'D:\Development\CodeWalker30_dev48\CodeWalker30_dev48'
)
$ErrorActionPreference = 'Stop'
$rpfPath = Join-Path $GameDir 'mods\update\update.rpf'
if (-not (Test-Path $rpfPath)) { throw "No mods copy of update.rpf at $rpfPath" }
Get-ChildItem $CodeWalkerDir -Filter 'SharpDX*.dll' | ForEach-Object { [void][Reflection.Assembly]::LoadFrom($_.FullName) }
[void][Reflection.Assembly]::LoadFrom((Join-Path $CodeWalkerDir 'CodeWalker.Core.dll'))
[CodeWalker.GameFiles.GTA5Keys]::LoadFromPath($GameDir, $true, $null)

$rpf = New-Object CodeWalker.GameFiles.RpfFile($rpfPath, 'update\update.rpf')
$rpf.ScanStructure($null, $null)
$entry = $rpf.AllEntries | Where-Object { $_.Path -like '*\common\data\dlclist.xml' } | Select-Object -First 1
$text = [Text.Encoding]::UTF8.GetString($entry.File.ExtractFile($entry)).TrimStart([char]0xFEFF)
$changed = $false
foreach ($p in $Packs) {
    $item = "<Item>dlcpacks:/$p/</Item>"
    $present = $text -match [regex]::Escape($item)
    if ($Remove -and $present) {
        $text = [regex]::Replace($text, "\s*" + [regex]::Escape($item), ''); $changed = $true; Write-Host "removed $p"
    } elseif (-not $Remove -and -not $present) {
        $text = $text -replace '(\s*)</Paths>', "`r`n`t`t$item`$1</Paths>"; $changed = $true; Write-Host "added $p"
    } else { Write-Host "$p already $(if ($Remove) {'absent'} else {'listed'})" }
}
if ($changed) {
    if ($rpf.Encryption -ne [CodeWalker.GameFiles.RpfEncryption]::OPEN) {
        Write-Host "re-saving mods\update\update.rpf with OPEN encryption (was $($rpf.Encryption))"
        [void][CodeWalker.GameFiles.RpfFile]::EnsureValidEncryption($rpf, [Func[CodeWalker.GameFiles.RpfFile, bool]]{ param($f) $true })
    }
    $dir = $entry.Parent
    [void][CodeWalker.GameFiles.RpfFile]::CreateFile($dir, 'dlclist.xml', (New-Object Text.UTF8Encoding $false).GetBytes($text), $true)
}
# Read back to confirm
$chk = New-Object CodeWalker.GameFiles.RpfFile($rpfPath, 'update\update.rpf'); $chk.ScanStructure($null, $null)
$e = $chk.AllEntries | Where-Object { $_.Path -like '*\common\data\dlclist.xml' } | Select-Object -First 1
$t = [Text.Encoding]::UTF8.GetString($e.File.ExtractFile($e))
Write-Host ("dlclist now has {0} packs; encryption {1}; last entries:" -f ([regex]::Matches($t, '<Item>')).Count, $chk.Encryption)
([regex]::Matches($t, '<Item>[^<]+</Item>') | Select-Object -Last 4) | ForEach-Object { Write-Host "   $($_.Value)" }
