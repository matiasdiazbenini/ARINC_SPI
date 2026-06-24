param(
    [Parameter(Mandatory = $true)]
    [string]$PdfPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Runtime.WindowsRuntime

[void][Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
[void][Windows.Data.Pdf.PdfDocument, Windows.Data.Pdf, ContentType = WindowsRuntime]
[void][Windows.Storage.Streams.InMemoryRandomAccessStream, Windows.Storage.Streams, ContentType = WindowsRuntime]
[void][Windows.Storage.Streams.DataReader, Windows.Storage.Streams, ContentType = WindowsRuntime]

$asTaskMethods = [System.WindowsRuntimeSystemExtensions].GetMethods() |
    Where-Object { $_.Name -eq "AsTask" }

$asTaskOperation = $asTaskMethods |
    Where-Object {
        $_.IsGenericMethod -and
        $_.GetParameters().Count -eq 1 -and
        $_.ToString() -match "IAsyncOperation``1"
    } |
    Select-Object -First 1

$asTaskAction = $asTaskMethods |
    Where-Object {
        -not $_.IsGenericMethod -and
        $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name -eq "IAsyncAction"
    } |
    Select-Object -First 1

function Wait-WinRtOperation {
    param(
        [Parameter(Mandatory = $true)]$Operation,
        [Parameter(Mandatory = $true)][Type]$ResultType
    )
    $method = $asTaskOperation.MakeGenericMethod($ResultType)
    $task = $method.Invoke($null, @($Operation))
    $task.Wait()
    return $task.Result
}

function Wait-WinRtAction {
    param([Parameter(Mandatory = $true)]$Action)
    $task = $asTaskAction.Invoke($null, @($Action))
    $task.Wait()
}

$resolvedPdf = (Resolve-Path -LiteralPath $PdfPath).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$resolvedOutput = (Resolve-Path -LiteralPath $OutputDirectory).Path

$storageFile = Wait-WinRtOperation `
    ([Windows.Storage.StorageFile]::GetFileFromPathAsync($resolvedPdf)) `
    ([Windows.Storage.StorageFile])

$pdf = Wait-WinRtOperation `
    ([Windows.Data.Pdf.PdfDocument]::LoadFromFileAsync($storageFile)) `
    ([Windows.Data.Pdf.PdfDocument])

for ($index = 0; $index -lt $pdf.PageCount; $index++) {
    $page = $pdf.GetPage($index)
    $stream = [Windows.Storage.Streams.InMemoryRandomAccessStream]::new()
    try {
        Wait-WinRtAction ($page.RenderToStreamAsync($stream))
        $reader = [Windows.Storage.Streams.DataReader]::new($stream.GetInputStreamAt(0))
        try {
            [void](Wait-WinRtOperation ($reader.LoadAsync([uint32]$stream.Size)) ([uint32]))
            $bytes = [byte[]]::new([int]$stream.Size)
            $reader.ReadBytes($bytes)
            $outputPath = Join-Path $resolvedOutput ("page-{0:D2}.png" -f ($index + 1))
            [System.IO.File]::WriteAllBytes($outputPath, $bytes)
            Write-Output $outputPath
        }
        finally {
            $reader.Dispose()
        }
    }
    finally {
        $stream.Dispose()
        $page.Dispose()
    }
}
