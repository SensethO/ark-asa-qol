# AsaQoL

Plugin serveur **ARK: Survival Ascended** ajoutant des commandes de confort tapées directement dans le chat du jeu : points de retour personnels, téléportation entre joueurs sur demande, et kits d'objets.

Construit sur [AsaApi](https://github.com/ArkServerApi/AsaApi) (v1.19), à partir du [template officiel](https://github.com/MolluskARK/ASA-Plugin-Template).

## Où aller

| | |
|---|---|
| **Installer** | [Français](docs/installation.md) · [English](docs/installation.en.md) — AsaApi, le plugin, le mod, et comment vérifier que le chargement a eu lieu |
| **Gestionnaires compatibles** | [Français](docs/gestionnaires.md) · [English](docs/managers.en.md) — lesquels chargent les plugins, lesquels non, et le test de deux minutes qui tranche pour n'importe quel autre |
| **[Télécharger](https://github.com/SensethO/ark-asa-qol/releases/latest)** | `AsaQoL-1.1.zip` (installation), `AsaQoL-1.1-maj.zip` (rechargement à chaud) |
| **[Gestionnaire de serveur](https://github.com/SensethO/asa-manager)** | Interface web qui pilote ce plugin : profils, SteamCMD, RCON, mods, sauvegardes |

**Le fait qui décide de tout** : le plugin n'est pas chargé par le gestionnaire,
mais par AsaApi, à l'intérieur du processus du jeu. Il faut donc que le serveur
soit lancé par `AsaApiLoader.exe` et non par `ArkAscendedServer.exe`. Un
gestionnaire qui lance le second ne chargera jamais aucun plugin — sans le
moindre message d'erreur.

---

## Contraintes à connaître avant de commencer

- **Binaire Windows.** AsaApi s'injecte dans l'exécutable du serveur ASA, qui est un binaire Windows. Il tourne néanmoins sous **Proton/Wine** : [POK-manager](https://github.com/Acekorneya/Ark-Survival-Ascended-Server) le fait en conteneur Linux, avec cette réserve de ses auteurs — *« les plugins personnalisés ne sont pas pris en charge et peuvent échouer sous Wine »*, ce qu'AsaQoL est. Non testé de notre côté.
- **Il faut administrer la machine.** Un hébergement géré (Nitrado, G-Portal…) ne convient pas : sans accès à `ShooterGame\Binaries\Win64\` ni choix de l'exécutable lancé, AsaApi ne peut pas être injecté. Un VPS ou un serveur dédié Windows, si.
- **Ce n'est pas un mod de contenu.** Aucun nouvel objet, créature ou structure : ces éléments-là relèvent de l'ARK Dev Kit et des Blueprints. Ce plugin agit côté serveur sur des mécaniques existantes.
- **Le RCON ne peut pas faire ce travail.** Il n'expose aucune téléportation de joueur arbitraire, et `GetChat` vide le tampon de chat — un bot qui l'interroge prive les joueurs de l'affichage normal de leurs messages. D'où le choix d'un plugin natif.

---

## Commandes

| Commande | Effet |
|---|---|
| `/help` | Liste les commandes actives |
| `/sethome [nom]` | Enregistre la position actuelle (nom par défaut : `home`) |
| `/home [nom]` | Téléporte vers un point enregistré |
| `/homes` | Liste vos points |
| `/delhome <nom>` | Supprime un point |
| `/tpa <joueur>` | Demande à se téléporter vers un joueur |
| `/tpaccept` | Accepte la demande reçue |
| `/tpdeny` | Refuse la demande reçue |
| `/kit` | Liste les kits et les utilisations restantes |
| `/kit <nom>` | Récupère un kit |
| `/players` | Joueurs connectés |
| `/pos` | Vos coordonnées de carte (lat/lon) |

Les noms de points peuvent contenir des espaces (`/sethome ma base nord`).

### Commandes RCON

| Commande | Effet |
|---|---|
| `qol.announce [durée_en_secondes] <message>` | Affiche un bandeau sur l'écran de tous les joueurs, pour la durée demandée |
| `qol.players` | JSON : joueurs connectés, position monde et coordonnées carte, tribu, état |
| `qol.inventory <eosId>` | JSON : inventaire du joueur, engrammes distingués des objets réels |
| `qol.containers <eosId> [rayon]` | JSON : coffres, structures à inventaire et montures de la tribu, avec contenu et coordonnées |
| `qol.dinos [filter=…] [species=…] [minlevel=…] [radius=…] [offset=…] [limit=…]` | JSON : recensement des créatures — espèce, statut, niveau, sexe, tribu, coordonnées |
| `qol.structures <eosId> [rayon]` | JSON : constructions de la tribu autour du joueur, avec coordonnées |
| `qol.wildlevels [reload] [simulate=N]` | JSON : niveaux imposés aux créatures sauvages ; `reload` relit `config.json` sans redémarrer, `simulate` tire N niveaux à blanc et renvoie leur répartition par tranche de 10 |

Les trois commandes d'inspection existent parce que le RCON natif d'ARK ne sait, seul, que lister les noms des joueurs : ni position, ni inventaire, ni construction.

**Contenu de `qol.inventory`** — l'inventaire d'ARK ne contient pas que ce que le joueur porte. Sur un compte fourni en contenus, un personnage débutant présente plus de 230 entrées pour six objets réels : le reste est constitué des engrammes appris et de tous les skins et costumes possédés, qu'ARK dépose en permanence dans l'inventaire.

Chaque entrée est donc étiquetée `engram`, `skin` et `type`, et la réponse compte séparément `carried`, `skins` et `engrams`. Deux pièges à connaître :

- `bHideFromInventoryDisplay` **n'est pas posé** sur ces entrées : il ne sert à rien pour les filtrer.
- Les costumes de créatures échappent à `bIsItemSkin` et sont classés en équipement. Seul le préfixe de classe `PrimalItemCostume_` les identifie de façon sûre — un filtre sur le nom affiché dépendrait de la langue du serveur.

**Recensement des créatures** — deux mesures ont dicté la conception :

- Une carte officielle peuplée compte environ **26 000 créatures**. Le RCON transporte 200 entrées en 1,3 s, mais échoue au-delà de 600 : la réponse est donc **paginée** et le filtrage par espèce et par niveau se fait **côté serveur de jeu**. Chercher « rex » sur la carte entière renvoie 24 correspondances en 2,3 s.
- `AbsoluteBaseLevel` sert à fixer le niveau au spawn et **vaut 0 sur les créatures existantes**. Le niveau réel se lit dans `UPrimalCharacterStatusComponent`, en deux parties : `BaseCharacterLevel` et `ExtraCharacterLevel` (niveaux gagnés après apprivoisement).

**Détection des contenants** — `IsA(APrimalStructureItemContainer::StaticClass())` ne fonctionne pas : cette classe n'expose pas `GetPrivateStaticClass`, et l'appel natif à `StaticClass` ne se résout pas, si bien qu'aucun coffre n'était reconnu. La hiérarchie de classes est donc remontée via `SuperStructField` et comparée par nom, ce qui ne dépend d'aucun symbole. Le résultat est mis en cache par classe.

**Portée de `qol.structures`** — la recherche est une requête spatiale centrée sur le joueur, limitée à sa tribu, avec un rayon par défaut de 30 000 unités (environ 300 m). Parcourir la carte entière provoquerait une pause serveur visible dès qu'une base est un peu développée. La réponse est plafonnée à 400 constructions et signale `truncated` quand elle est tronquée ; le nombre réellement trouvé reste indiqué dans `matched`.

La durée est facultative : sans elle, `DefaultSeconds` de la configuration s'applique. Elle est plafonnée par `MaxSeconds` pour qu'une faute de frappe ne fige pas un bandeau sur l'écran des joueurs.

Cette fonction est exposée **uniquement en RCON, jamais en commande de chat** : le RCON exige déjà le mot de passe administrateur, alors qu'une commande de chat permettrait à n'importe quel joueur de couvrir l'écran des autres.

C'est la seule voie permettant une durée réglable : la commande native `Broadcast` d'ARK affiche bien un message à l'écran, mais sa durée est fixée par le jeu. Le mécanisme sous-jacent est `ClientServerNotification`, dont AsaApi expose le paramètre de durée.

```jsonc
"Announce": {
  "Enabled": true,
  "DefaultSeconds": 60,   // durée si la commande n'en précise pas
  "MaxSeconds": 300,      // plafond de sécurité
  "Scale": 1.3,           // taille du texte, 1.0 = messages serveur standard
  "Color": { "R": 1.0, "G": 0.85, "B": 0.2, "A": 1.0 }
}
```

### La configuration ne peut pas empêcher le plugin de se charger

Chaque section de `config.json` est lue de façon isolée : une valeur du mauvais type n'invalide que sa section, et le journal la nomme. `Plugin_Init` ne laisse par ailleurs aucune exception s'échapper.

Ce n'est pas une précaution théorique. Une exception échappée fait renoncer AsaApi au plugin entier — le serveur perd toutes ses commandes — et le message d'erreur d'AsaApi ne dit pas quel plugin ni quelle ligne. Pire, le défaut était invisible au premier démarrage : `config.json` n'existant pas encore, tout le code de lecture était court-circuité. Le plugin se chargeait parfaitement, puis échouait au démarrage suivant.

La cause était une **référence pendante** : `json.value("Messages", …).items()` renvoie un proxy vers un objet temporaire, détruit avant la première itération. La boucle lisait de la mémoire libérée. L'objet est désormais nommé avant d'être parcouru.

### Garde-fous appliqués

- Aucune téléportation si le personnage est mort.
- Refus par défaut si le joueur est sur une monture (`AllowWhileRidingDino`).
- Délai d'amorçage configurable avant le déplacement ; relancer une commande annule le déplacement précédent.
- Le temps de recharge n'est consommé qu'en cas de téléportation réussie.
- Les demandes `/tpa` expirent seules et sont purgées chaque seconde.
- Le contrôleur du joueur est ré-résolu au moment du déplacement à partir de son identifiant EOS : une déconnexion pendant l'amorçage annule proprement l'opération.

---

## Construction

### Prérequis

- Visual Studio 2022 avec la charge de travail « Développement Desktop en C++ »
- Le sous-module AsaApi : `git clone --recurse-submodules`
- `AsaApi.lib` placé dans `extern\AsaApi\out_lib\`

Deux façons d'obtenir `AsaApi.lib` :

1. **Version publiée** (recommandé) — récupérer la release AsaApi correspondant au commit du sous-module depuis [GitHub](https://github.com/ArkServerApi/AsaApi/releases), et copier `Lib\AsaApi.lib` dans `extern\AsaApi\out_lib\`. C'est l'option qui garantit la compatibilité ABI avec le `AsaApi.dll` installé sur le serveur.
2. **Compilation depuis les sources** — ouvrir `extern\AsaApi\AsaApi.sln`. Cette voie exige vcpkg et quatre dépendances lourdes (`openssl`, `detours`, `poco`, `minizip`).

### Compiler le plugin

Configuration `Release`, plateforme `x64`. La sortie est écrite dans `out\` :

- `AsaQoL.dll` — le plugin
- `AsaQoL.dll.arkapi` — copie servant au rechargement à chaud
- `AsaQoL.pdb` — symboles de débogage

### Compilation sans Visual Studio 2022

Le projet cible le toolset `v143` (VS 2022) et déclare `fmt` via vcpkg. Sur une machine équipée uniquement de **Visual Studio Build Tools 2026** (toolset `v145`) et sans vcpkg, `build\fmt.props` fournit `fmt` en mode header-only et neutralise le manifeste vcpkg :

```bash
git clone --depth 1 --branch 10.2.1 https://github.com/fmtlib/fmt.git extern/fmt
```

```bash
MSBuild.exe Plugin.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /p:ForceImportBeforeCppTargets=build\fmt.props
```

Cette voie a servi à valider la compilation des sources. Pour un binaire destiné à la production, préférez le toolset officiel `v143` : `AsaApi.lib` et le `AsaApi.dll` du serveur sont construits avec lui, et faire coïncider les toolsets évite tout risque d'incompatibilité ABI sur les types standard échangés à la frontière de DLL.

---

## Installation sur le serveur

1. Installer AsaApi dans `ShooterGame\Binaries\Win64\` ainsi que le [redistribuable MSVC x64](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
2. Créer le dossier `ShooterGame\Binaries\Win64\ArkApi\Plugins\AsaQoL\`.
3. Y déposer `AsaQoL.dll` et `configs\PluginInfo.json`.
4. Démarrer le serveur. Au premier lancement, le plugin écrit un `config.json` par défaut à côté de la DLL.
5. Ajuster `config.json`, puis redéposer `AsaQoL.dll.arkapi` pour recharger le plugin sans redémarrer.

---

## Configuration — `config.json`

```jsonc
{
  "General": {
    "SenderName": "Serveur",   // nom affiché comme expéditeur dans le chat
    "Prefix": "[QoL] "         // préfixe de chaque message
  },
  "Homes": {
    "Enabled": true,
    "MaxHomes": 3,             // 0 = illimité
    "CooldownSeconds": 300,
    "WarmupSeconds": 10,       // 0 = téléportation immédiate
    "AllowWhileRidingDino": false
  },
  "Tpa": {
    "Enabled": true,
    "CooldownSeconds": 180,
    "WarmupSeconds": 10,
    "AllowWhileRidingDino": false,
    "RequestTimeoutSeconds": 60
  },
  "Kits": {
    "Enabled": true,
    "List": [
      {
        "Name": "starter",
        "Description": "Equipement de depart",
        "CooldownSeconds": 0,
        "MaxUses": 1,          // -1 = illimité
        "Items": [
          {
            "Blueprint": "Blueprint'/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponStonePick.PrimalItem_WeaponStonePick'",
            "Amount": 1,
            "Quality": 0,
            "ForceBlueprint": false
          }
        ]
      }
    ]
  },
  "Messages": { }              // tous les textes joueur, surchargeables un par un
}
```

**Chemins de blueprint** — le format attendu est celui de la commande console `GiveItem`, soit `Blueprint'/Game/…/NomItem.NomItem'`. Les chemins fournis dans le kit d'exemple sont ceux des objets de base ; vérifiez-les sur votre serveur avant mise en production, et notez qu'un objet issu d'un mod aura un chemin propre à ce mod. Un objet dont le chemin est invalide est simplement ignoré, et le joueur reçoit un message indiquant le nombre d'objets non remis.

**Messages** — toute clé absente de `Messages` retombe sur le texte intégré. Les paramètres s'écrivent `{0}`, `{1}`. La substitution est faite par le plugin et non par `fmt` : une accolade isolée dans un texte d'administration ne peut pas provoquer d'erreur à l'exécution.

---

## Niveaux des créatures sauvages

ARK **ne sait pas** imposer un niveau minimum aux créatures sauvages, ni pondérer les niveaux : `OverrideOfficialDifficulty` et `DifficultyOffset` ne fixent qu'un plafond (niveau max = 30 × difficulté), les créatures apparaissant ensuite de 1 à ce maximum. Le plugin comble ce manque en redéfinissant le niveau au moment de l'apparition.

```jsonc
"WildLevels": {
  "Enabled": true,
  "MinLevel": 100,
  "MaxLevel": 150,
  "Bands": [                      // vide = tirage uniforme entre Min et Max
    { "From": 100, "To": 109, "Percent": 40 },
    { "From": 110, "To": 119, "Percent": 25 },
    { "From": 120, "To": 129, "Percent": 20 },
    { "From": 130, "To": 139, "Percent": 10 },
    { "From": 140, "To": 150, "Percent": 5  }
  ]
}
```

Les parts n'ont pas besoin de totaliser 100 : elles sont normalisées au tirage, pour qu'une saisie approximative ne désactive pas silencieusement des tranches.

**Fonctionnement** — `APrimalDinoCharacter::BeginPlay()` est intercepté ; pour une créature sauvage, `AbsoluteBaseLevel` est posé avant l'appel d'origine, ce qui impose le niveau avant l'initialisation des statistiques. Le détour est trivial quand `Enabled` est à `false`, et toute exception y est absorbée : une créature doit exister même si le tirage échoue.

**Périmètre** — seules les créatures qui apparaissent après l'activation sont concernées. Celles déjà présentes gardent leur niveau jusqu'à leur disparition, et en reçoivent un nouveau au redémarrage du serveur, puisqu'elles repassent alors par `BeginPlay`. Pour appliquer la consigne à toute la carte immédiatement : `DestroyWildDinos`.

**Aucune distinction n'est possible entre une créature qui apparaît et une créature relue de la sauvegarde** : dans les deux cas le composant de statut porte déjà un niveau au moment de l'appel. Une option « ne pas toucher aux créatures existantes » bâtie sur ce critère a été écrite puis retirée — elle n'écartait pas les créatures relues, elle neutralisait la fonctionnalité entière sans le dire.

**Mesuré sur serveur réel** — consigne 100-150 avec la répartition ci-dessus, après `DestroyWildDinos` et repopulation : échantillon de 200 créatures sauvages, minimum 100, maximum 150, parts relevées 42,5 / 18,5 / 23,5 / 12,5 / 3,0 %. Le tirage à blanc du plugin sur 20 000 valeurs donne 39,8 / 25,4 / 20,1 / 10,0 / 4,6 %.

---

## Données — `data.json`

Écrit à côté de `config.json`, indexé par identifiant EOS (les points survivent donc à la recréation d'un personnage).

```jsonc
{
  "Players": {
    "<eos_id>": {
      "Homes":     [ { "Name": "base", "X": 0.0, "Y": 0.0, "Z": 0.0 } ],
      "Cooldowns": { "home": 1723456789, "tpa": 0, "kit.starter": 0 },
      "KitUses":   { "starter": 1 }
    }
  }
}
```

Sauvegarde toutes les 30 secondes si quelque chose a changé, plus une sauvegarde au déchargement du plugin. L'écriture passe par un fichier temporaire renommé ensuite, pour qu'un arrêt brutal en cours d'écriture ne tronque pas les données. Si `data.json` est illisible au démarrage, le plugin refuse de l'écraser et le signale dans les logs : renommez le fichier à la main pour repartir de zéro.

---

## État de validation

- **Compilation vérifiée** : les unités de compilation passent sans erreur ni avertissement contre les en-têtes AsaApi v1.19, avec MSVC v14.50.
- **Édition de liens vérifiée** : `AsaQoL.dll` exporte `Plugin_Init` et `Plugin_Unload`, les deux symboles qu'AsaApi recherche au chargement. Lié contre le `AsaApi.lib` de la release 2.03.
- **Chargement vérifié sur un serveur réel** : déployé sur un serveur ASA avec AsaApi 2.03, le journal confirme `AsaQoL loaded` puis `Loaded plugin AsaQoL V1`, et le plugin écrit bien son `config.json` par défaut au premier démarrage.
- **Compatibilité binaire v145 / v143 confirmée en pratique** : malgré un toolset différent de celui d'AsaApi, et un runtime C++ 14.44 dans le dossier du serveur, le plugin se charge et s'exécute. Le cas reste à surveiller après une mise à jour majeure d'AsaApi.
- **Niveaux sauvages vérifiés sur serveur réel** : consigne 100-150 appliquée, faune renouvelée, relevé conforme à la répartition demandée. C'est la seule fonctionnalité du plugin dont l'effet en jeu a été mesuré et non seulement observé.
- **Commandes en jeu non éprouvées avec des joueurs** : les garde-fous de téléportation et les kits sont implémentés mais n'ont pas été exercés en session réelle.

---

## Structure du code

| Fichier | Rôle |
|---|---|
| `src/Main.cpp` | `Plugin_Init` / `Plugin_Unload` |
| `src/Commands.cpp` | Enregistrement et implémentation des commandes, tick périodique |
| `src/Config.cpp` | Lecture de `config.json`, génération du fichier par défaut |
| `src/WildLevels.cpp` | Interception de l'apparition des créatures sauvages, tirage du niveau, commande `qol.wildlevels` |
| `src/Storage.cpp` | Persistance des points, temps de recharge et compteurs de kits |
| `src/Util.cpp` | Conversions FString/UTF-8, envoi de messages, substitution `{n}` |
