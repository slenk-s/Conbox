$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$failures=[System.Collections.Generic.List[string]]::new()
$count=0
function Check([bool]$ok,[string]$name){$script:count++;if($ok){Write-Host "PASS: $name"}else{$failures.Add($name);Write-Host "FAIL: $name"}}
$main=Get-Content -LiteralPath "$root/Core/SRC/main.c" -Raw
Check ($main -match 'OLED_Init\(\);[\s\S]*BarcodeView_Init\(\);[\s\S]*BarcodeApp_Init\(HAL_GetTick\(\)\);[\s\S]*BarcodePort_Init\(\);') 'OLED initialization finishes before startup clock'
Check ($main -match 'while\s*\(1\)[\s\S]*BarcodePort_Poll\(\);[\s\S]*BarcodeView_Poll\(\);') 'main services serial/timers before OLED'
$init=@('HAL_Init','Studio_RCC_Init','Studio_GPIO_Init','Studio_USART2_Init','Studio_USART1_Init','Studio_TIM1_Init','Studio_TIM3_Init')
$positions=@($init|ForEach-Object {$main.IndexOf("$_();")})
Check (($positions|Where-Object {$_ -lt 0}).Count -eq 0) 'retained hardware initialization calls exist'
Check (($positions -join ',') -eq (($positions|Sort-Object) -join ',')) 'retained initialization order unchanged'
foreach($ext in @('uvprojx','uvoptx')){
 $text=Get-Content -LiteralPath "$root/MDK-ARM/T1.$ext" -Raw
 Check ($text -notmatch 'stationbox|siacp|uart1_self_test') "$ext excludes deleted application"
 $xml=[xml]$text
 foreach($node in $xml.SelectNodes('//FilePath | //PathWithFileName')){
  Check (Test-Path -LiteralPath (Join-Path "$root/MDK-ARM" $node.InnerText) -PathType Leaf) "$ext source exists: $($node.InnerText)"
 }
 if($ext -eq 'uvprojx'){
  foreach($name in @('barcode_store','barcode_rx','host_protocol','barcode_app','barcode_port','barcode_view')){
   Check (@($xml.SelectNodes('//FileName')|Where-Object {$_.InnerText -eq "$name.c"}).Count -eq 1) "$name compiled exactly once"
  }
 }
}
$it=Get-Content -LiteralPath "$root/Core/SRC/py32f003_it.c" -Raw
Check ($it -match 'HAL_UART_IRQHandler\(&husart1\)') 'USART1 IRQ forwarding retained'
Check ($it -match 'HAL_UART_IRQHandler\(&husart2\)') 'USART2 IRQ forwarding retained'
$core=(Get-ChildItem -LiteralPath "$root/Core/SRC" -Filter *.c|Get-Content -Raw)-join [Environment]::NewLine
foreach($cb in @('HAL_UART_RxCpltCallback','HAL_UART_TxCpltCallback','HAL_UART_ErrorCallback')){
 Check ([regex]::Matches($core,"void\s+$cb\s*\(").Count -eq 1) "$cb defined exactly once"
}
foreach($name in @('stationbox','siacp','uart1_self_test')){
 Check (-not(Test-Path -LiteralPath "$root/Core/SRC/$name.c")) "$name source still removed"
 Check (-not(Test-Path -LiteralPath "$root/Core/INC/$name.h")) "$name header still removed"
}
$app=(Get-ChildItem -LiteralPath "$root/Core/SRC" -Filter 'barcode_*.c'|Get-Content -Raw)-join [Environment]::NewLine
Check ($app -notmatch 'HAL_Delay|HAL_UART_Transmit\(|malloc\(|OLED_Refresh\(|OLED_Scroll') 'application avoids blocking operations and dynamic allocation'
Check ($app -notmatch 'HAL_FLASH_') 'dedup never writes Flash'
Check ((Get-Content "$root/Core/SRC/usart.c" -Raw) -match 'husart1.Init.BaudRate = 9600;[\s\S]*husart2.Init.BaudRate = 9600;') 'UART rates retained at 9600'
if($failures.Count){throw "$($failures.Count) project checks failed"}
Write-Host "PASS: $count project checks"
