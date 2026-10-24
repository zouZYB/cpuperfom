<#
.SYNOPSIS
  Count the branch/CMOV mix of every function in an object file or library.
  Used to verify that "branch" kernels really contain branches and that
  "cmov" kernels really are branchless.

.DESCRIPTION
  Columns:
    jcc    conditional jumps (jz/jnz/jl/jg/... but not jmp)
    jmp    unconditional jumps
    cmov   conditional moves (branchless select)
    call   calls
  Counts are static instruction counts, not executed counts.

.EXAMPLE
  pwsh -File scripts/check_branches.ps1 -Obj build/CMakeFiles/branch_lab.dir/src/kernels_data.cpp.obj
  pwsh -File scripts/check_branches.ps1 -Obj build-vs2022/x64/Release/branch_lab.lib -Filter sum_if
#>
param(
    [Parameter(Mandatory = $true)][string]$Obj,
    [string]$Filter = '',
    [switch]$Csv
)

$ErrorActionPreference = 'Stop'

function Find-Objdump {
    $candidates = @(
        'C:\msys64\ucrt64\bin\objdump.exe',
        'C:\msys64\mingw64\bin\objdump.exe',
        'C:\msys64\usr\bin\objdump.exe'
    )
    $vsLlvm = 'D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\llvm-objdump.exe'
    foreach ($p in $candidates) { if (Test-Path $p) { return $p } }
    if (Test-Path $vsLlvm) { return $vsLlvm }
    $cmd = Get-Command objdump -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $cmd = Get-Command llvm-objdump -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw 'objdump / llvm-objdump not found (needs MSYS2 or the VS2022 LLVM component)'
}

if (-not (Test-Path $Obj)) { throw "file not found: $Obj" }
$objdump = Find-Objdump
$text = & $objdump -d --no-show-raw-insn $Obj 2>$null
if ($LASTEXITCODE -ne 0) { throw "objdump failed: $objdump $Obj" }

$rows = New-Object System.Collections.Generic.List[object]
$script:cur = $null
$script:jcc = 0
$script:jmp = 0
$script:cmov = 0
$script:call = 0
$script:insn = 0

function Flush {
    if ($null -ne $script:cur) {
        $script:rows.Add([pscustomobject]@{
            Function     = $script:cur
            Instructions = $script:insn
            Jcc          = $script:jcc
            Jmp          = $script:jmp
            Cmov         = $script:cmov
            Call         = $script:call
        })
    }
}

foreach ($line in $text) {
    if ($line -match '^[0-9a-fA-F]+ <(.+)>:\s*$') {
        Flush
        $script:cur = $Matches[1]
        # Demangle the simple case: _ZN2fz13sum_if_branchERKNS_5InputE -> sum_if_branch
        if ($script:cur -match '^_ZN2fz\d+') {
            $n = $script:cur -replace '^_ZN2fz\d+', ''
            $n = $n -replace 'ERKNS_5InputE$', ''
            if ($n -match '^([A-Za-z_][A-Za-z0-9_]*)') { $script:cur = $Matches[1] }
        }
        $script:jcc = 0
        $script:jmp = 0
        $script:cmov = 0
        $script:call = 0
        $script:insn = 0
        continue
    }
    if ($null -eq $script:cur) { continue }
    if ($line -match '^\s+[0-9a-fA-F]+:\s+(.+?)\s*$') {
        $op = $Matches[1]
        $script:insn++
        if ($op -match '^cmov') { $script:cmov++ }
        elseif ($op -match '^j') {
            if ($op -match '^jmp') { $script:jmp++ } else { $script:jcc++ }
        }
        elseif ($op -match '^call') { $script:call++ }
    }
}
Flush

$out = $rows
if ($Filter -ne '') { $out = $rows | Where-Object { $_.Function -like "*$Filter*" } }

if ($Csv) {
    $out | ConvertTo-Csv -NoTypeInformation
} else {
    $out | Format-Table -AutoSize
}
