param (
    [string]$uf2,
    [string]$volumeName
)

Write-Host "Looking for drive with label: $volumeName"

$drives = Get-WmiObject Win32_LogicalDisk

$target = $drives | Where-Object {
    $_.VolumeName -eq $volumeName
}

if ($target -eq $null) {
    Write-Error "Drive '$volumeName' not found!"
    Write-Host ""
    Write-Host "Available drives:"
    
    foreach ($d in $drives) {
        Write-Host ("{0}  ({1})" -f $d.DeviceID, $d.VolumeName)
    }

    exit 1
}

$dest = "$($target.DeviceID)\"

Write-Host "Copying to $dest"
Copy-Item $uf2 $dest -Force

Write-Host "Upload complete"