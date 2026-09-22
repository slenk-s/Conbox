$ErrorActionPreference='Stop'
# Compatibility entry: the initialization-only baseline has been replaced by the new application.
Write-Host 'MIGRATED: empty-project assertions replaced by barcode project checks.'
& (Join-Path $PSScriptRoot '../barcode/check_project.ps1')
