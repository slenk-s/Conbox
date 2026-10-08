param([ValidateSet('rx','protocol','app','port','view','echo','echo_u1','echo_u2','oled','store','all')][string]$Suite='all')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out=Join-Path $root 'MDK-ARM/build_tmp/barcode'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$common=@('Core/SRC/host_protocol.c','Core/SRC/barcode_store.c','Core/SRC/barcode_app.c')
$sources=@{
 rx=@('Core/SRC/barcode_rx.c')
 protocol=@('Core/SRC/host_protocol.c')
 app=$common+@('tests/barcode/fake_port.c')
 store=@('Core/SRC/barcode_store.c')
 port=$common+@('Core/SRC/barcode_rx.c','Core/SRC/barcode_port.c','tests/barcode/hal_stub.c')
 oled=@('OLED/oled.c')
 view=$common+@('tests/barcode/fake_port.c','Core/SRC/barcode_view.c','tests/barcode/oled_stub.c')
 echo=@('Core/SRC/barcode_view.c','tests/barcode/oled_stub.c')
 echo_u1=$common+@('Core/SRC/barcode_rx.c','Core/SRC/barcode_port.c','Core/SRC/barcode_view.c','tests/barcode/hal_stub.c','tests/barcode/oled_stub.c')
 echo_u2=$common+@('Core/SRC/barcode_rx.c','Core/SRC/barcode_port.c','Core/SRC/barcode_view.c','tests/barcode/hal_stub.c','tests/barcode/oled_stub.c')
}
# Extra preprocessor symbols per suite. echo checks view formatting only;
# echo_u1/echo_u2 drive the real port layer to prove channel selection.
$defines=@{
 echo=@('BARCODE_ECHO_MODE')
 echo_u1=@('BARCODE_ECHO_MODE')
 echo_u2=@('BARCODE_ECHO_MODE','BARCODE_ECHO_PORT=1')
}
# Suites that reuse another suite's test file.
$testfile=@{ echo_u1='echo_port'; echo_u2='echo_port' }
$names=if($Suite -eq 'all'){@('rx','protocol','store','app','port','view','echo','echo_u1','echo_u2','oled')}else{@($Suite)}
Push-Location $root
try {
 foreach($name in $names){
  $exe=Join-Path $out "test_$name.exe"
  $stem=if($testfile.ContainsKey($name)){$testfile[$name]}else{$name}
  $args=@('-std=c99','-Wall','-Wextra','-Werror','-pedantic','-g','-DBARCODE_HOST_TEST','-Itests/barcode/oled_hw','-IOLED','-ICore/INC','-Itests/barcode',"tests/barcode/test_$stem.c")
  if($defines.ContainsKey($name)){$args+=($defines[$name]|ForEach-Object {'-D'+$_})}
  $args+=$sources[$name]+@('-o',$exe)
  & 'gcc' @args
  if($LASTEXITCODE -ne 0){throw "$name compilation failed"}
  & $exe
  if($LASTEXITCODE -ne 0){throw "$name assertions failed"}
 }
 Write-Host "PASS: $($names.Count) suite(s)"
} finally {Pop-Location}
