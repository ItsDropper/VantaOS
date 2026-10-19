# Copyright (c) 2026 ItsDropper
#
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.

Add-Type -AssemblyName System.Windows.Forms

$text = [System.Windows.Forms.Clipboard]::GetText()

if ([string]::IsNullOrEmpty($text)) {
    exit
}

$text = $text -replace "`r`n", "`n"
$text = $text -replace "`r", "`n"

foreach ($character in $text.ToCharArray()) {
    $ascii = [int][char]$character

    if ($ascii -ge 32 -and $ascii -le 126) {
        Write-Output $character
    }
    elseif ($character -eq "`n") {
        Write-Output "`n"
    }
}