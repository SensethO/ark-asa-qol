## AsaQoL 1.5 — composition des groupes de créatures

### Nouveauté

`qol.dinogroup` sait maintenant **lire et modifier la composition des groupes du menu T**. Un groupe y est défini par **espèces** : toute créature de ces espèces en fait partie.

| Commande | Effet |
|---|---|
| `qol.dinogroup <eosId> classes` | Espèces de chacun des 10 groupes, et groupe actuellement sélectionné |
| `qol.dinogroup <eosId> members <groupe>` | Créatures de la tribu qui font partie du groupe : espèce, nom, niveau, points par statistique, coordonnées |
| `qol.dinogroup <eosId> setclass <groupe> <espèce>` | Ajoute une espèce au groupe |
| `qol.dinogroup <eosId> removeclass <groupe> <espèce>` | Retire une espèce du groupe |
| `qol.dinogroup <eosId> clear <groupe>` | Vide les espèces du groupe |

- L'espèce se donne par son nom (celui de `qol.dinos`, par exemple `Baryonyx`) et doit exister au moins une fois dans la tribu du joueur : la classe est retrouvée sur une créature réelle.
- Groupe de `1` à `10`, comme dans l'interface.

### Correctif 1.5.1 : les groupes reflètent bien la partie en jeu

Une première version relisait toujours des groupes vides. Cause : le jeu reçoit la classe d'une espèce **par adresse** (TSubclassOf), alors que les en-têtes d'AsaApi la déclarent par valeur ; les appels natifs passaient donc une valeur sans rapport. Les appels passent maintenant l'adresse d'un TSubclassOf. Constaté sur le serveur de test : les groupes relus correspondent aux ajouts faits en jeu, et `setclass` met à jour le menu T du joueur.

Nouveau : `qol.dinogroup <eosId> adddino|rmdino <groupe> <espece>|<nom>|<niveau d'origine>|<sexe 0/1>` ajoute ou retire une créature précise (le serveur n'expose pas d'identifiant : la créature est retrouvée par espèce, nom, niveau d'origine et sexe ; deux créatures identiques sont indiscernables).

### À vérifier en jeu

Les appels `_Implementation` d'ajout, de retrait et de vidage n'ont pas été éprouvés en jeu, comme pour l'envoi d'ordre de la 1.4. Essayer d'abord sur un groupe sans importance.

### Mise à jour

Rechargement à chaud : `AsaQoL-1.5-maj.zip` (un seul fichier, `AsaQoL.dll.arkapi`). Compilé avec le toolset v145 (Visual Studio Build Tools 2026), voir le README pour la réserve sur l'ABI.
