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

### Limite constatée en essai

Les groupes du menu T sont **tenus par le client** du joueur. Ces commandes lisent et modifient l'état côté **serveur**, qui ne reflète pas les groupes définis dans le jeu : un groupe peuplé en jeu peut apparaître vide ici, et `setclass` ne change pas ce que le joueur voit. Seule la liste `available` de `classes` (espèces présentes dans la tribu) est fiable ; un essai de rafraîchissement du client (`ClientRefreshDinoOrderGroup`) n'a pas abouti et n'est pas inclus. Le plugin Stream Deck tient donc sa propre liste de groupes et n'utilise de ce mod que `classes` (`available`) et `qol.dinos`.

### À vérifier en jeu

Les appels `_Implementation` d'ajout, de retrait et de vidage n'ont pas été éprouvés en jeu, comme pour l'envoi d'ordre de la 1.4. Essayer d'abord sur un groupe sans importance.

### Mise à jour

Rechargement à chaud : `AsaQoL-1.5-maj.zip` (un seul fichier, `AsaQoL.dll.arkapi`). Compilé avec le toolset v145 (Visual Studio Build Tools 2026), voir le README pour la réserve sur l'ABI.
