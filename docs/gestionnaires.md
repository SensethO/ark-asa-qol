***Français** · [English](managers.en.md)*

# Quels gestionnaires de serveur peuvent charger AsaQoL

AsaQoL est un plugin **AsaApi**. Il n'est pas chargé par le gestionnaire, mais
par AsaApi, à l'intérieur du processus du jeu. Un seul fait décide donc de tout :

> **Le gestionnaire lance-t-il `AsaApiLoader.exe`, ou `ArkAscendedServer.exe` ?**

`AsaApiLoader.exe` démarre le jeu *et* injecte AsaApi, qui charge ensuite les
plugins. Un gestionnaire qui lance l'exécutable du jeu directement ne chargera
**jamais** aucun plugin — et c'est le piège : le serveur démarre normalement, les
joueurs se connectent, rien n'indique que les plugins sont absents.

---

## Le test en deux minutes

Valable pour n'importe quel gestionnaire, y compris ceux absents de cette page.

1. Installe AsaApi et AsaQoL (voir [installation.md](installation.md)).
2. Démarre le serveur **par le gestionnaire**.
3. Ouvre le dernier fichier de
   `ShooterGame\Binaries\Win64\logs\ArkApi_*.log`.

| Ce que tu lis | Verdict |
|---|---|
| `[AsaQoL][info] AsaQoL charge` | Le gestionnaire convient. |
| Le dossier `logs\` est vide, ou aucun fichier récent | AsaApi n'a pas été injecté : le gestionnaire lance le mauvais exécutable. |
| `Loaded all plugins` sans ligne `AsaQoL` | AsaApi tourne, mais le plugin n'est pas au bon endroit. |

Autre vérification, pendant que le serveur tourne : le gestionnaire des tâches
doit montrer **`AsaApiLoader.exe`** au-dessus de `ArkAscendedServer.exe`.

---

## Vérifié

| Gestionnaire | Plateforme | Plugins | Comment |
|---|---|---|---|
| **asa-manager** (ce projet) | Windows | **Oui** | Détecte AsaApi et bascule seul. Journalise *« AsaApi détecté : lancement via AsaApiLoader.exe »*. Seul à piloter les 14 commandes `qol.*` par son interface. |
| **AASM** — [ARK Ascended Server Manager](https://arkascendedservermanager.com/asa-api-plugin-setup/) | Windows, Linux | **Oui** | Prise en charge de premier ordre, onglet **ASA-API**. Démarre `AsaApiLoader.exe` à la place d'`ArkAscendedServer.exe`. Payant après essai. |
| **AMP** — [CubeCoders](https://discourse.cubecoders.com/t/ark-survival-ascended-guide/6738) | Windows, Linux | **Oui** | *Configuration → Updates → Runtime Configuration*, puis mise à jour du serveur. **Réserve de l'éditeur** : sous Windows, AMP ne sait plus suivre le processus, les métriques deviennent fausses. |
| **POK-manager** — [Acekorneya](https://github.com/Acekorneya/Ark-Survival-Ascended-Server) | Linux (Docker + Proton) | **Oui, avec réserve** | Installe AsaApi 2.03, empreinte SHA-256 vérifiée, lance le loader sous Wine. **Réserve de l'éditeur** : *« les plugins personnalisés ne sont pas pris en charge et peuvent échouer sous Wine »* — AsaQoL entre dans cette catégorie. Non testé. |
| **WindowsGSM** + [ArkSAwithServerAPI](https://github.com/ohmcodes/WindowsGSM.ArkSAwithServerAPI) | Windows | **Oui** | Extension dédiée au ServerAPI. Le plugin *Permissions* est exigé et n'est pas fourni. L'auteur déconseille la mise à jour automatique au démarrage. |
| **Nitrado** | Hébergement géré | **Non** | N'autorise aucun plugin externe. Pas d'accès aux binaires du serveur. Les mods restent possibles, les plugins non. |

## Non vérifié

Ces gestionnaires existent et gèrent ASA, mais je n'ai pas trouvé de source
fiable sur leur prise en charge d'AsaApi. Applique-leur le test ci-dessus.

- **Arti's ARK: Survival Ascended Server Manager** (Windows, gratuit)
- **ASAM** — justsomebritishguy (Windows)
- **arkservermanager.app** (Windows, libre)
- **ASA Dedicated Manager** — asadedicatedmanager.eu
- **ARK-Ascended-Server-Manager** — Ch4r0ne (Windows, PowerShell)
- **HaruHostGSM** (Windows, Linux)

## Hors de portée

| Cas | Pourquoi |
|---|---|
| **Hébergeurs gérés** — G-Portal, Shockbyte, Host Havoc et assimilés | Pas d'accès au système de fichiers ni au choix de l'exécutable. Un panneau web ne peut pas injecter AsaApi. |
| **Serveur Linux natif** (sans Proton) | AsaApi est un binaire Windows. Il lui faut une couche de compatibilité — c'est exactement ce que fait POK-manager. |
| **Clients / serveurs non dédiés** | AsaApi ne s'injecte que dans le serveur dédié. |

**Exception à retenir** : un hébergeur qui loue une **machine Windows complète**
(VPS, serveur dédié, accès Bureau à distance) n'est pas un hébergeur géré. Tout
y fonctionne, puisque tu choisis toi-même ce qui se lance.

---

## Ce qu'un autre gestionnaire ne vous donnera pas

Même sur un gestionnaire qui charge le plugin, les **14 commandes RCON**
`qol.*` n'auront pas d'interface. Elles restent tapables à la main dans sa
console RCON :

```
qol.announce 30 Redemarrage dans 5 minutes
qol.dinos owner=Petra
qol.wildlevels reload
qol.gamemode
```

Les onglets Créatures, Joueurs, Niveaux sauvages, Piles et Réglages
stratégiques sont propres à **asa-manager**, qui appelle ces commandes et
écrit directement dans `ArkApi\Plugins\AsaQoL\config.json`.
