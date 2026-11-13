param([string]$Zig = '')
$ErrorActionPreference = 'Stop'
$testRoot = $PSScriptRoot
if (!$Zig) {
    $bundled = Join-Path $testRoot 'bin\python_deps\ziglang\zig.exe'
    if (Test-Path $bundled) { $Zig = $bundled }
    else { $Zig = (Get-Command zig -ErrorAction Stop).Source }
}
$out = Join-Path $testRoot 'bin'
New-Item -ItemType Directory -Path $out -Force | Out-Null
python (Join-Path $testRoot 'prepare_hci_test.py')
if ($LASTEXITCODE -ne 0) { throw 'HCI test preparation failed.' }
foreach ($test in @('spp_protocol_test', 'acl_transport_test', 'bond_storage_test', 'hci_scheduler_test')) {
    $exe = Join-Path $out "$test.exe"
    & $Zig cc -std=c99 -Wall -Wextra -Werror -Wno-sign-compare -Wno-int-to-pointer-cast -I (Join-Path $testRoot 'stubs') -I (Join-Path $testRoot '..\Core\Inc') (Join-Path $testRoot "$test.c") -o $exe
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $test" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "Test failed: $test" }
}
