# Installer AsaQoL

Trois briques, souvent confondues, et qui s'installent séparément :

| | Quoi | Où | Qui l'installe |
|---|---|---|---|
| **1** | **AsaApi** | `ShooterGame\Binaries\Win64\` | Toi, ou le gestionnaire |
| **2** | **AsaQoL** (ce dépôt) | `…\Win64\ArkApi\Plugins\AsaQoL\` | Toi |
| **3** | **AsaQoLUI** (mod) | Téléchargé par ARK | ARK, par son identifiant |

Le plugin **fonctionne sans le mod** : les commandes de tchat marchent, et
`/inv` se replie sur un affichage textuel. Le mod n'apporte que la fenêtre
graphique.

---

## Étape 1 — AsaApi

**SteamCMD ne l'installe pas.** C'est l'oubli le plus fréquent, et le plus
silencieux : sans AsaApi, le serveur démarre normalement et aucun plugin
n'est chargé.

Prends la version officielle sur
[ServersHub/ServerAPI](https://github.com/ServersHub/ServerAPI/releases) et
décompresse-la dans `ShooterGame\Binaries\Win64\`. Elle ajoute six fichiers :

```
AsaApiLoader.exe        AsaApiLoader.pdb        config.json
libcrypto-3-x64.dll     libssl-3-x64.dll        msdia140.dll
ArkApi\                 (dossier)
```

Ces six-là et rien d'autre : les `boost_*`, `tbb`, `msvcp140*` et `vcruntime*`
que tu verras à côté sont livrés par le jeu. Cette liste vient de la
comparaison du dossier avec les manifestes d'installation Steam.

Il faut aussi le **Microsoft Visual C++ 2015-2022 x64 Redistributable**.

**À partir de là, le serveur se lance par `AsaApiLoader.exe`**, jamais par
`ArkAscendedServer.exe`. Voir [gestionnaires.md](gestionnaires.md) pour savoir
si le tien le fait.

## Étape 2 — Le plugin

### Par le script, serveur arrêté

```powershell
.\Installer-AsaQoL.ps1 -Serveur E:\ServersASA\test1
```

Le script vérifie qu'AsaApi est présent et **refuse d'aller plus loin s'il
manque** — plutôt que de déposer des fichiers qui ne seront jamais lus. Il
sauvegarde une configuration existante avant d'y toucher, et ne touche jamais
à `data.json`, qui contient les maisons et les compteurs des joueurs.

### Par le script, serveur en marche

```powershell
.\Installer-AsaQoL.ps1 -Serveur E:\ServersASA\test1 -Chaud
```

Dépose `AsaQoL.dll.arkapi` : AsaApi sauvegarde le monde, décharge le plugin et
le recharge, sans coupure. C'est aussi la façon de mettre à jour.

### À la main

Décompresse `AsaQoL-1.1.zip` dans :

```
<serveur>\ShooterGame\Binaries\Win64\ArkApi\Plugins\
```

Tu dois obtenir un dossier `AsaQoL\` contenant `AsaQoL.dll`, `config.json` et
`PluginInfo.json`.

Pour une **mise à jour** sans écraser ta configuration, utilise plutôt
`AsaQoL-1.1-maj.zip`, qui ne contient que le binaire, déjà nommé
`AsaQoL.dll.arkapi`.

## Étape 3 — Le mod, si tu veux la fenêtre

Ajoute **`1650813`** à la liste des mods du serveur. ARK le télécharge seul.

Un mod ASA **ne peut pas être installé localement** : le serveur délègue au
cœur CurseForge, ignore un dossier `Mods\` déposé à la main, interroge le
catalogue, et **s'arrête** sur un identifiant introuvable.

---

## Vérifier

Dernier fichier de `ShooterGame\Binaries\Win64\logs\ArkApi_*.log` :

```
[AsaQoL][info] Configuration chargee : 1 kit(s), homes=true, tpa=true, annonces=true
[AsaQoL][info] Pont client : interception posee
[AsaQoL][info] AsaQoL charge
[API][info] Loaded plugin AsaQoL V1.1
```

En jeu : `/help` liste les commandes. En RCON : `qol.players` répond.

Si le dossier `logs\` est vide, ce n'est pas le plugin qui est en cause —
AsaApi n'a pas été injecté du tout. Reviens à l'étape 1.

---

## Configuration

`ArkApi\Plugins\AsaQoL\config.json`, relu au rechargement du plugin.

| Section | Effet |
|---|---|
| `General` | Préfixe des messages, nom de l'expéditeur |
| `Homes`, `Tpa` | Délais, temps d'attente, nombre de maisons |
| `Kits` | Contenu et délai de chaque kit |
| `Announce` | Couleur, durée et taille du bandeau |
| `Window` | Fenêtre `/inv` : rayon de recherche, chemin du mod |
| `WildLevels` | Niveaux des créatures sauvages |

**`WildLevels` arrive désactivé**, et c'est délibéré : il change la nature
d'une partie en imposant un plancher de niveau aux créatures sauvages. Ce
n'est pas un réglage neutre, il ne doit s'activer que sur décision.

Une réserve à connaître avant de l'activer : l'interception écarte les
créatures **inconscientes**, faute de quoi un animal en cours d'apprivoisement
voyait ses statistiques recalculées au redémarrage — torpeur comprise — se
réveillait et partait, progrès perdu.

---

## Désinstaller

Supprime le dossier `ArkApi\Plugins\AsaQoL\` et redémarre. Rien d'autre n'est
touché : le plugin n'écrit ni dans les sauvegardes, ni dans les `.ini`.

Retire aussi le mod `1650813` de la liste si tu l'avais ajouté.
