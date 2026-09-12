<#
.SYNOPSIS
    Installe ou met a jour le plugin AsaQoL sur un serveur ARK: Survival Ascended.

.DESCRIPTION
    Verifie qu'AsaApi est present, sauvegarde la configuration existante, puis
    depose les fichiers du plugin. Ne touche jamais a data.json, qui contient
    les maisons et les compteurs des joueurs.

.PARAMETER Serveur
    Racine de l'installation ARK, celle qui contient ShooterGame\.
    Exemple : E:\ServersASA\test1

.PARAMETER Chaud
    Depose AsaQoL.dll.arkapi au lieu de AsaQoL.dll : AsaApi recharge le plugin
    sans arreter le serveur. Sans ce commutateur, le serveur doit etre arrete.

.EXAMPLE
    .\Installer-AsaQoL.ps1 -Serveur E:\ServersASA\test1

.EXAMPLE
    .\Installer-AsaQoL.ps1 -Serveur E:\ServersASA\test1 -Chaud
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Serveur,

    [switch] $Chaud
)

$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'AsaQoL'

function Bilan($etat, $texte) {
    $couleur = switch ($etat) { 'ok' { 'Green' } 'note' { 'Cyan' } default { 'Yellow' } }
    Write-Host ('  [{0,-4}] {1}' -f $etat, $texte) -ForegroundColor $couleur
}

Write-Host ''
Write-Host 'Installation d''AsaQoL' -ForegroundColor White
Write-Host ('-' * 60)

# --- Le serveur existe-t-il ? ------------------------------------------------
$binaires = Join-Path $Serveur 'ShooterGame\Binaries\Win64'
if (-not (Test-Path $binaires)) {
    throw "Introuvable : $binaires`nLe parametre -Serveur doit designer la racine de l'installation ARK, celle qui contient ShooterGame\."
}
Bilan 'ok' "Serveur trouve : $Serveur"

# --- AsaApi est-il installe ? ------------------------------------------------
# C'est le point qui decide de tout. Sans le loader, le serveur demarre
# normalement et AUCUN plugin n'est charge, sans le moindre message.
$loader = Join-Path $binaires 'AsaApiLoader.exe'
if (Test-Path $loader) {
    Bilan 'ok' 'AsaApiLoader.exe present'
} else {
    Bilan 'HALT' 'AsaApiLoader.exe absent : AsaApi n''est pas installe.'
    Write-Host ''
    Write-Host '  Le plugin ne serait jamais charge, et rien ne le signalerait.' -ForegroundColor Yellow
    Write-Host '  Installe d''abord AsaApi : https://github.com/ServersHub/ServerAPI/releases' -ForegroundColor Yellow
    Write-Host '  puis relance ce script.' -ForegroundColor Yellow
    Write-Host ''
    throw 'AsaApi manquant.'
}

# --- Le serveur tourne-t-il ? ------------------------------------------------
$actif = Get-Process -Name 'ArkAscendedServer', 'AsaApiLoader' -ErrorAction SilentlyContinue
if ($actif -and -not $Chaud) {
    Bilan 'HALT' 'Le serveur tourne. Arrete-le, ou relance avec -Chaud pour un rechargement sans coupure.'
    throw 'Serveur en cours d''execution.'
}
if ($actif) { Bilan 'note' 'Serveur en marche : rechargement a chaud' }

# --- Sauvegarde de la configuration en place ---------------------------------
$cible = Join-Path $binaires 'ArkApi\Plugins\AsaQoL'
$configExistante = Join-Path $cible 'config.json'
New-Item -ItemType Directory -Force -Path $cible | Out-Null

if (Test-Path $configExistante) {
    $copie = Join-Path $cible ('config.json.{0:yyyyMMdd-HHmmss}.bak' -f (Get-Date))
    Copy-Item $configExistante $copie
    Bilan 'ok' "Configuration existante sauvegardee : $(Split-Path $copie -Leaf)"
    Bilan 'note' 'Elle est CONSERVEE : tes reglages ne sont pas ecrases.'
} else {
    Copy-Item (Join-Path $source 'config.json') $configExistante
    Bilan 'ok' 'Configuration par defaut posee (niveaux sauvages desactives)'
}

# --- Le binaire --------------------------------------------------------------
$nom = if ($Chaud) { 'AsaQoL.dll.arkapi' } else { 'AsaQoL.dll' }
Copy-Item (Join-Path $source 'AsaQoL.dll') (Join-Path $cible $nom) -Force
Copy-Item (Join-Path $source 'PluginInfo.json') $cible -Force
Bilan 'ok' "Plugin depose : $nom"

# --- Verification ------------------------------------------------------------
Write-Host ('-' * 60)
if ($Chaud) {
    Write-Host 'AsaApi va recharger le plugin dans les secondes qui viennent.' -ForegroundColor White
} else {
    Write-Host 'Demarre le serveur par AsaApiLoader.exe, jamais par ArkAscendedServer.exe.' -ForegroundColor White
}
Write-Host ''
Write-Host 'Puis verifie le chargement dans le dernier journal :' -ForegroundColor White
Write-Host "  $binaires\logs\ArkApi_*.log" -ForegroundColor Gray
Write-Host '  La ligne attendue est : [AsaQoL][info] AsaQoL charge' -ForegroundColor Gray
Write-Host ''
Write-Host 'La fenetre /inv exige en plus le mod CurseForge AsaQoLUI (1650813).' -ForegroundColor White
Write-Host 'Sans lui, /inv se replie sur un affichage en tchat.' -ForegroundColor Gray
Write-Host ''
