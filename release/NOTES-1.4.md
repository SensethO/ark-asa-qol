## AsaQoL 1.4 — groupes de créatures du menu T

### Nouveauté

**`qol.dinogroup`** (RCON) : donne un ordre à un groupe de créatures du menu T, sans passer par le jeu.

| Commande | Effet |
|---|---|
| `qol.dinogroup <eosId> list` | Noms des 10 groupes et groupe actuellement sélectionné |
| `qol.dinogroup <eosId> <groupe> <ordre>` | Sélectionne le groupe puis donne l'ordre |

- **Groupe** : `1` à `10` (numérotation de l'interface), ou `all` pour ne filtrer par aucun groupe.
- **Ordre** : `follow`, `stay`, `aggressive`, `passive`, `neutral`, `passiveflee`, `attack`.
- L'`eosId` est celui renvoyé par `qol.players`. Le joueur doit être connecté et avoir un personnage vivant.

Le plugin rejoue ce que fait le jeu quand le joueur siffle : il sélectionne le groupe sur le joueur, puis déclenche l'ordre sur son personnage. La portée de l'ordre et le filtrage par groupe sont donc ceux du jeu. Le groupe reste sélectionné après la commande, comme en jeu.

Exemple :

```
qol.dinogroup 0002bc37754f47b79091aa689acadbed 1 stay
```

### Compatibilité

- AsaApi 1.19 ou plus récent, comme la 1.3. Aucun changement de `config.json`.
- Compilé avec le toolset MSVC `v145` (VS Build Tools 2026), comme les versions précédentes.

### Vérification

Testé sur un serveur de test (carte The Island) : la commande est acceptée, le groupe est bien sélectionné et les créatures du groupe obéissent à `stay` et `follow`. Les autres ordres (`aggressive`, `passive`, `neutral`, `passiveflee`, `attack`) et `all` n'ont pas été essayés individuellement.

### Installation

- **Première installation** : `AsaQoL-1.4.zip`, voir `installation.md`.
- **Mise à jour d'un serveur qui tourne** : `AsaQoL-1.4-maj.zip` (rechargement à chaud, la DLL est nommée `AsaQoL.dll.arkapi`).

### Pour piloter depuis un Stream Deck

Le plugin [ark-asa-streamdeck](https://github.com/SensethO/ark-asa-streamdeck) (dépôt à publier) envoie ces commandes par RCON, sans simulation de touches.

---

## AsaQoL 1.4 (English summary)

New RCON command **`qol.dinogroup`**: give an order to a tame group from the in-game T menu.

- `qol.dinogroup <eosId> list` lists the 10 groups and the selected one.
- `qol.dinogroup <eosId> <1-10|all> <follow|stay|aggressive|passive|neutral|passiveflee|attack>` selects the group and issues the order, like the in-game whistle: range and group filtering are the game's own.
- Get the `eosId` from `qol.players`. The player must be online with a living character.
- Needs AsaApi 1.19+. No config change. Built with MSVC `v145`.
- Tested on a test server: `stay` and `follow` confirmed in game; the other orders and `all` were not individually tested.
- Install with `AsaQoL-1.4.zip`; hot-update a running server with `AsaQoL-1.4-maj.zip`.
