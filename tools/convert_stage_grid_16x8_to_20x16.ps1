# 役割: 旧16x8ステージ設定を、実行時互換処理なしで20x16形式へ一度だけ変換する。
# 依存する自プロジェクト内ファイル: assets/Settings/Stage/StageN と build/Stage/game/StageN のステージ設定。

param(
    [Parameter(Mandatory = $true)]
    [string[]]$StageConfigPaths
)

$ErrorActionPreference = 'Stop'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

function Write-Utf8Atomic([string]$path, [string[]]$lines)
{
    $temporary = "$path.grid20x16.tmp"
    [IO.File]::WriteAllLines($temporary, $lines, $utf8NoBom)
    Move-Item -LiteralPath $temporary -Destination $path -Force
}

function Convert-Cell([int]$row, [int]$column)
{
    if ($row -lt 0 -or $row -ge 8 -or $column -lt 0 -or $column -ge 384) {
        throw "旧グリッド範囲外です: $row,$column"
    }
    $convertedRow = $row + 8
    $convertedColumn = ([math]::Floor($column / 16) * 20) + ($column % 16)
    return @($convertedRow, $convertedColumn)
}

function Convert-WorldX([double]$value)
{
    if ($value -lt 0.0) { return $value }
    return $value + ([math]::Floor($value / 512.0) * 128.0)
}

function Convert-WorldY([double]$value)
{
    return $value + 256.0
}

function Convert-StageGrid([string]$stagePath)
{
    $lines = [IO.File]::ReadAllLines($stagePath)
    if ($lines.Count -lt 9 -or $lines[0] -notmatch '^grid\s+32\s+16\s+8\s*$') { return $false }

    $oldGrid = @()
    for ($row = 0; $row -lt 8; $row++) {
        $values = @($lines[$row + 1].Trim() -split '\s+' | ForEach-Object { [int]$_ })
        if ($values.Count -ne 384) { throw "旧グリッド列数が不正です: $stagePath" }
        $oldGrid += ,$values
    }

    $newGrid = New-Object 'int[,]' 16,480
    for ($oldRow = 0; $oldRow -lt 8; $oldRow++) {
        for ($oldColumn = 0; $oldColumn -lt 384; $oldColumn++) {
            $cell = Convert-Cell $oldRow $oldColumn
            $newGrid[$cell[0], $cell[1]] = $oldGrid[$oldRow][$oldColumn]
        }
    }

    $output = [System.Collections.Generic.List[string]]::new()
    $output.Add('grid 32 20 16')
    for ($row = 0; $row -lt 16; $row++) {
        $rowValues = [System.Collections.Generic.List[string]]::new()
        for ($column = 0; $column -lt 480; $column++) {
            $rowValues.Add([string]($newGrid[$row, $column]))
        }
        $output.Add([string]::Join(' ', $rowValues))
    }

    for ($index = 9; $index -lt $lines.Count; $index++) {
        $line = $lines[$index]
        if ($line -match '^(reference|image4|image3|image2|image)\s+(-?\d+)\s+(-?\d+)(.*)$') {
            $cell = Convert-Cell ([int]$Matches[2]) ([int]$Matches[3])
            $line = "$($Matches[1]) $($cell[0]) $($cell[1])$($Matches[4])"
        }
        $output.Add($line)
    }
    Write-Utf8Atomic $stagePath $output.ToArray()
    return $true
}

function Convert-GridCoordinateFiles([string]$directory)
{
    $attachmentPath = Join-Path $directory 'rpg_attachments.cfg'
    if (Test-Path -LiteralPath $attachmentPath) {
        $lines = [IO.File]::ReadAllLines($attachmentPath)
        if ($lines.Count -gt 0 -and $lines[0] -match '^v6\s+') {
            for ($index = 1; $index -lt $lines.Count; $index++) {
                $tokens = @($lines[$index].Trim() -split '\s+')
                if ($tokens.Count -lt 14) { throw "アタッチメント形式が不正です: $attachmentPath" }
                $cell = Convert-Cell ([int]($tokens[2])) ([int]($tokens[3]))
                $tokens[2] = $cell[0]; $tokens[3] = $cell[1]
                $pathCount = [int]($tokens[13])
                if ($tokens.Count -ne 14 + $pathCount * 2) { throw "アタッチメント軌道が不正です: $attachmentPath" }
                for ($pathIndex = 0; $pathIndex -lt $pathCount; $pathIndex++) {
                    $cell = Convert-Cell ([int]($tokens[14 + $pathIndex * 2])) ([int]($tokens[15 + $pathIndex * 2]))
                    $tokens[14 + $pathIndex * 2] = $cell[0]; $tokens[15 + $pathIndex * 2] = $cell[1]
                }
                $lines[$index] = $tokens -join ' '
            }
            Write-Utf8Atomic $attachmentPath $lines
        }
    }

    $receiverPath = Join-Path $directory 'rpg_receivers.cfg'
    if (Test-Path -LiteralPath $receiverPath) {
        $lines = [IO.File]::ReadAllLines($receiverPath)
        for ($index = 1; $index -lt $lines.Count; $index++) {
            $tokens = @($lines[$index].Trim() -split '\s+')
            if ($tokens.Count -eq 3) {
                $cell = Convert-Cell ([int]($tokens[0])) ([int]($tokens[1]))
                $lines[$index] = "$($cell[0]) $($cell[1]) $($tokens[2])"
            }
        }
        Write-Utf8Atomic $receiverPath $lines
    }

    $signalPath = Join-Path $directory 'rpg_signal_blocks.cfg'
    if (Test-Path -LiteralPath $signalPath) {
        $lines = [IO.File]::ReadAllLines($signalPath)
        for ($index = 1; $index -lt $lines.Count; $index++) {
            $tokens = @($lines[$index].Trim() -split '\s+')
            if ($tokens.Count -eq 4) {
                $cell = Convert-Cell ([int]($tokens[0])) ([int]($tokens[1]))
                $lines[$index] = "$($cell[0]) $($cell[1]) $($tokens[2]) $($tokens[3])"
            }
        }
        Write-Utf8Atomic $signalPath $lines
    }

    $wirePath = Join-Path $directory 'rpg_wires.cfg'
    if (Test-Path -LiteralPath $wirePath) {
        $lines = [IO.File]::ReadAllLines($wirePath)
        for ($index = 1; $index -lt $lines.Count; $index++) {
            $tokens = @($lines[$index].Trim() -split '\s+')
            $pathCount = [int]($tokens[4])
            if ($tokens.Count -ne 5 + $pathCount * 2) { throw "導線形式が不正です: $wirePath" }
            $cell = Convert-Cell ([int]($tokens[1])) ([int]($tokens[2]))
            $tokens[1] = $cell[0]; $tokens[2] = $cell[1]
            for ($pathIndex = 0; $pathIndex -lt $pathCount; $pathIndex++) {
                $cell = Convert-Cell ([int]($tokens[5 + $pathIndex * 2])) ([int]($tokens[6 + $pathIndex * 2]))
                $tokens[5 + $pathIndex * 2] = $cell[0]; $tokens[6 + $pathIndex * 2] = $cell[1]
            }
            $lines[$index] = $tokens -join ' '
        }
        Write-Utf8Atomic $wirePath $lines
    }

    foreach ($fileName in @('rpg_items.cfg', 'rpg_map_events.cfg')) {
        $path = Join-Path $directory $fileName
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $lines = [IO.File]::ReadAllLines($path)
        for ($index = 1; $index -lt $lines.Count; $index++) {
            $tokens = @($lines[$index].Trim() -split '\s+', 3)
            if ($tokens.Count -eq 3) {
                $lines[$index] = ('{0:0.0} {1:0.0} {2}' -f (Convert-WorldX ([double]($tokens[0]))), (Convert-WorldY ([double]($tokens[1]))), $tokens[2])
            }
        }
        Write-Utf8Atomic $path $lines
    }

    $layoutPath = Join-Path $directory 'rpg_layout.cfg'
    if (Test-Path -LiteralPath $layoutPath) {
        $lines = [IO.File]::ReadAllLines($layoutPath)
        $tokens = @($lines[0].Trim() -split '\s+')
        if ($tokens.Count -ge 4) {
            $tokens[0] = '{0:0.0}' -f (Convert-WorldX ([double]($tokens[0])))
            $tokens[1] = '{0:0.0}' -f (Convert-WorldY ([double]($tokens[1])))
            $tokens[2] = '{0:0.0}' -f (Convert-WorldX ([double]($tokens[2])))
            $tokens[3] = '{0:0.0}' -f (Convert-WorldY ([double]($tokens[3])))
            $lines[0] = $tokens -join ' '
        }
        Write-Utf8Atomic $layoutPath $lines
    }

    foreach ($fileName in @('rpg_inspect.cfg', 'rpg_stage_entry_event.cfg', 'rpg_stage3_event.cfg', 'rpg_area_entry_events.cfg')) {
        $path = Join-Path $directory $fileName
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $lines = [IO.File]::ReadAllLines($path)
        for ($index = 0; $index -lt $lines.Count; $index++) {
            if ($lines[$index] -notmatch '^M\t([^\t]*)\t(.*)$') { continue }
            $title = $Matches[1]
            $tokens = @($Matches[2].Trim() -split '\s+')
            if ($tokens.Count -ge 6) {
                $tokens[1] = '{0:F2}' -f (Convert-WorldX ([double]($tokens[1])))
                $tokens[5] = '{0:F2}' -f (Convert-WorldY ([double]($tokens[5])))
                $lines[$index] = "M`t$title`t$($tokens -join ' ')"
            }
        }
        Write-Utf8Atomic $path $lines
    }
}

$converted = @()
foreach ($stageConfigPath in $StageConfigPaths) {
    if (Convert-StageGrid $stageConfigPath) {
        Convert-GridCoordinateFiles (Split-Path -Parent $stageConfigPath)
        $converted += $stageConfigPath
    }
}

Write-Output ("変換済み: {0}" -f $converted.Count)
$converted
