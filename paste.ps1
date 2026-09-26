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