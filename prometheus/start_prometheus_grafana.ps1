param(
    [switch]$Stop,
    [switch]$Status,
    [string]$PrometheusHome = "",
    [string]$PrometheusConfig = "",
    [string]$PrometheusData = "",
    [string]$GrafanaHome = "",
    [int]$PrometheusPort = 9090,
    [int]$GrafanaPort = 3000
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent $ScriptDir
$PampaRoot = Split-Path -Parent $RepoRoot
$LogDir = Join-Path $PampaRoot "observability-logs"

function Join-IfSet {
    param(
        [string]$Base,
        [string]$Child
    )

    if ([string]::IsNullOrWhiteSpace($Base)) {
        return ""
    }

    return Join-Path $Base $Child
}

function Resolve-FirstExistingFile {
    param([string[]]$Candidates)

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        $matches = @(Get-ChildItem -Path $candidate -ErrorAction SilentlyContinue)
        foreach ($match in $matches) {
            if ($match -and -not $match.PSIsContainer) {
                return $match.FullName
            }
        }
    }

    return $null
}

function Resolve-FirstExistingDirectory {
    param([string[]]$Candidates)

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        $matches = @(Get-ChildItem -Path $candidate -Directory -ErrorAction SilentlyContinue)
        foreach ($match in $matches) {
            if ($match -and $match.PSIsContainer) {
                return $match.FullName
            }
        }
    }

    return $null
}

function Test-Http {
    param([string]$Url)

    try {
        $response = Invoke-WebRequest -UseBasicParsing -TimeoutSec 2 -Uri $Url
        return $response.StatusCode -ge 200 -and $response.StatusCode -lt 500
    } catch {
        return $false
    }
}

function Show-Status {
    $prometheus = Get-Process -Name "prometheus" -ErrorAction SilentlyContinue
    $grafana = Get-Process -Name "grafana-server", "grafana" -ErrorAction SilentlyContinue

    Write-Host "Prometheus process: $([bool]$prometheus)"
    Write-Host "Grafana process   : $([bool]$grafana)"
    Write-Host "Prometheus URL    : http://127.0.0.1:$PrometheusPort"
    Write-Host "Grafana URL       : http://127.0.0.1:$GrafanaPort"
    Write-Host "Prometheus ready  : $(Test-Http "http://127.0.0.1:$PrometheusPort/-/ready")"
    Write-Host "Grafana ready     : $(Test-Http "http://127.0.0.1:$GrafanaPort/api/health")"
}

function Stop-Observability {
    Get-Process -Name "prometheus" -ErrorAction SilentlyContinue | Stop-Process -Force
    Get-Process -Name "grafana-server", "grafana" -ErrorAction SilentlyContinue | Stop-Process -Force
    Write-Host "Prometheus/Grafana detenidos si estaban corriendo."
}

if ($Stop) {
    Stop-Observability
    exit 0
}

if ($Status) {
    Show-Status
    exit 0
}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if ([string]::IsNullOrWhiteSpace($PrometheusConfig)) {
    $PrometheusConfig = Join-Path $ScriptDir "prometheus.yml"
}

if (-not (Test-Path -LiteralPath $PrometheusConfig)) {
    throw "No existe el archivo de configuracion Prometheus: $PrometheusConfig"
}

if ([string]::IsNullOrWhiteSpace($PrometheusData)) {
    $PrometheusData = Join-Path $PampaRoot "prometheus-data"
}

New-Item -ItemType Directory -Force -Path $PrometheusData | Out-Null

if ([string]::IsNullOrWhiteSpace($PrometheusHome)) {
    $prometheusExe = Resolve-FirstExistingFile @(
        (Join-Path $PampaRoot "prometheus-*.windows-amd64\prometheus.exe"),
        (Join-Path $PampaRoot "prometheus\prometheus.exe"),
        (Join-Path $ScriptDir "prometheus.exe"),
        (Join-IfSet $env:PROMETHEUS_HOME "prometheus.exe")
    )
} else {
    $prometheusExe = Join-Path $PrometheusHome "prometheus.exe"
}

if (-not $prometheusExe -or -not (Test-Path -LiteralPath $prometheusExe)) {
    throw "No encontre prometheus.exe. Ejecutar con -PrometheusHome 'C:\ruta\a\prometheus'."
}

$PrometheusHome = Split-Path -Parent $prometheusExe

if (-not (Get-Process -Name "prometheus" -ErrorAction SilentlyContinue)) {
    $prometheusArgs = @(
        "--config.file=`"$PrometheusConfig`"",
        "--storage.tsdb.path=`"$PrometheusData`"",
        "--web.listen-address=127.0.0.1:$PrometheusPort"
    )

    Start-Process `
        -FilePath $prometheusExe `
        -ArgumentList $prometheusArgs `
        -WorkingDirectory $PrometheusHome `
        -RedirectStandardOutput (Join-Path $LogDir "prometheus.out.log") `
        -RedirectStandardError (Join-Path $LogDir "prometheus.err.log") `
        -WindowStyle Hidden

    Write-Host "Prometheus iniciado en http://127.0.0.1:$PrometheusPort"
} else {
    Write-Host "Prometheus ya estaba corriendo."
}

if ([string]::IsNullOrWhiteSpace($GrafanaHome)) {
    $grafanaExe = Resolve-FirstExistingFile @(
        (Join-Path $PampaRoot "grafana*\bin\grafana-server.exe"),
        (Join-Path $PampaRoot "grafana\bin\grafana-server.exe"),
        "C:\Program Files\GrafanaLabs\grafana\bin\grafana-server.exe",
        (Join-IfSet $env:GRAFANA_HOME "bin\grafana-server.exe")
    )
} else {
    $grafanaExe = Join-Path $GrafanaHome "bin\grafana-server.exe"
}

if (-not $grafanaExe -or -not (Test-Path -LiteralPath $grafanaExe)) {
    if ((Get-Process -Name "grafana-server", "grafana" -ErrorAction SilentlyContinue) -or (Test-Http "http://127.0.0.1:$GrafanaPort/api/health")) {
        Write-Host "Grafana ya estaba corriendo en http://127.0.0.1:$GrafanaPort"
        Show-Status
        exit 0
    }

    Write-Warning "No encontre grafana-server.exe. Prometheus quedo iniciado. Para Grafana, ejecutar con -GrafanaHome 'C:\ruta\a\grafana'."
    Show-Status
    exit 0
}

$GrafanaHome = Split-Path -Parent (Split-Path -Parent $grafanaExe)

if (-not (Get-Process -Name "grafana-server", "grafana" -ErrorAction SilentlyContinue) -and -not (Test-Http "http://127.0.0.1:$GrafanaPort/api/health")) {
    $grafanaArgs = @(
        "--homepath",
        "`"$GrafanaHome`"",
        "cfg:server.http_addr=127.0.0.1",
        "cfg:server.http_port=$GrafanaPort"
    )

    Start-Process `
        -FilePath $grafanaExe `
        -ArgumentList $grafanaArgs `
        -WorkingDirectory $GrafanaHome `
        -RedirectStandardOutput (Join-Path $LogDir "grafana.out.log") `
        -RedirectStandardError (Join-Path $LogDir "grafana.err.log") `
        -WindowStyle Hidden

    Write-Host "Grafana iniciado en http://127.0.0.1:$GrafanaPort"
} else {
    Write-Host "Grafana ya estaba corriendo."
}

Start-Sleep -Seconds 2
Show-Status
