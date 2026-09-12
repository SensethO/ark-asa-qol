# Spécification du mod d'interface — à reproduire dans l'ARK Dev Kit

Contrat que le mod doit respecter pour que le plugin AsaQoL puisse lui envoyer les inventaires. Le côté serveur est **déjà écrit et compilé** ; il ne reste que la partie Dev Kit.

---

## Principe

Le plugin serveur collecte les données, **applique le contrôle d'accès**, puis appelle une fonction Blueprint sur un acteur singleton publié par le mod. Le mod n'a plus qu'à afficher ce qu'il reçoit.

```
/inv en jeu  →  plugin (collecte + droits)  →  singleton du mod  →  widget UMG
```

**Le mod ne filtre rien.** Il reçoit uniquement ce que le joueur a le droit de voir : un mod client est modifiable par un joueur, filtrer côté widget serait une faille. Cette règle n'est pas négociable.

---

## 1. La structure de données

Créer une structure Blueprint nommée **`AsaQoLWindowRow`** avec **exactement ces sept champs, dans cet ordre** :

| # | Nom du champ | Type Blueprint | Type C++ correspondant |
|---|---|---|---|
| 1 | `Tab` | String | `FString` |
| 2 | `Group` | String | `FString` |
| 3 | `Name` | String | `FString` |
| 4 | `Quantity` | Integer | `int` |
| 5 | `Lat` | Float | `double` |
| 6 | `Lon` | Float | `double` |
| 7 | `Detail` | String | `FString` |

> Dans Unreal Engine 5, le type « Float » d'un Blueprint est en réalité un **double**. C'est bien `double` côté C++, pas `float` — se tromper décale tous les champs suivants.

**Valeurs attendues dans `Tab`** — elles deviennent les onglets de la fenêtre :

- `Avatars` — ce que portent les personnages
- `Coffres` — coffres, forges, parcelles et autres structures à inventaire
- `Montures` — inventaires des créatures apprivoisées

`Group` contient le nom du porteur ou du contenant : c'est la clé de regroupement à l'intérieur d'un onglet. `Detail` contient le nom de classe de l'objet, utile pour un tri technique ou une icône. `Lat` et `Lon` valent 0 quand la position n'a pas de sens.

---

## 2. L'acteur singleton

Créer un **Blueprint Actor** nommé `AsaQoLSingleton`, placé de façon à être chargé avec le mod, et lui ajouter une fonction :

```
ShowInventoryWindow(RecipientEOS : Array of String,
                    Title        : String,
                    Rows         : Array of AsaQoLWindowRow)
```

L'ordre des paramètres est imposé — il correspond au struct C++ :

```cpp
struct ShowInventoryWindow_Params
{
    TArray<FString> RecipientEOS;
    FString Title;
    TArray<WindowRow> Rows;
};
```

`RecipientEOS` contient les identifiants EOS des joueurs à qui afficher la fenêtre. La fonction s'exécute **sur le serveur** : elle doit ensuite déclencher un RPC client vers ces joueurs uniquement.

Le mod peut aussi enregistrer le singleton dans une liste d'acteurs nommée, comme le fait AsaApiUtils avec `ApiUtilsCCA`, ce qui accélère sa localisation.

---

## 3. Le widget

Rien n'est imposé côté présentation. Ce qui est attendu fonctionnellement :

- **Onglets** construits à partir des valeurs distinctes de `Tab`
- **Regroupement** par `Group` à l'intérieur d'un onglet
- **Recherche** filtrant sur `Name`, `Group` et `Detail`
- **Tri** par `Name` et par `Quantity`
- **Coordonnées** affichées quand `Lat`/`Lon` ne sont pas nuls

---

## 4. Raccorder le plugin

Une fois le mod publié, renseigner dans `config.json` du plugin :

```jsonc
"Window": {
  "Enabled": true,
  "BlueprintPath": "Blueprint'/AsaQoLUI/AsaQoLSingleton.AsaQoLSingleton'",
  "FunctionName": "ShowInventoryWindow",
  "Title": "Inventaires",
  "Radius": 30000
}
```

Le chemin dépend du nom sous lequel le mod est publié — il suit la forme utilisée par AsaApiUtils. **Tant que `BlueprintPath` est vide, le pont reste inactif** et `/inv` se rabat automatiquement sur un affichage textuel paginé dans le chat. Aucune configuration n'est donc nécessaire avant que le mod existe.

---

## 5. Le piège principal, et comment l'éviter

Un décalage entre la structure Blueprint et le struct C++ ne produit **pas d'erreur** : il fait lire de la mémoire arbitraire, ce qui se traduit par des valeurs aberrantes ou un crash du serveur, sans message exploitable.

Procédez par validation incrémentale plutôt que d'écrire les sept champs d'un coup :

1. Créez d'abord une fonction de test à **un seul paramètre String**, appelée par le plugin, qui affiche simplement le texte reçu. Vérifiez que le pont fonctionne.
2. Ajoutez `RecipientEOS` et vérifiez que seul le bon joueur est ciblé.
3. Ajoutez la structure avec **un seul champ**, puis les champs un par un, en contrôlant les valeurs affichées à chaque ajout.

Un champ mal typé se repère immédiatement à cette étape ; noyé dans sept champs, il est très difficile à isoler.

---

## 6. Ce qui est déjà fait côté serveur

| Élément | État |
|---|---|
| Collecte des inventaires portés | Fait, cosmétiques et engrammes exclus |
| Collecte des coffres et montures | Fait, avec coordonnées |
| Détection du chef de tribu | Fait, via `FTribeData::OwnerPlayerDataID` |
| Contrôle d'accès membre / chef | Fait, appliqué avant tout envoi |
| Recherche du singleton et appel `ProcessEvent` | Fait, chemin configurable |
| Commande `/inv` avec repli textuel paginé | Fait |

Aucune modification du plugin ne sera nécessaire à la publication du mod : il suffira de renseigner `BlueprintPath`.

---

## 7. Règles d'accès appliquées

| Situation | Ce que le joueur reçoit |
|---|---|
| Membre simple | Uniquement l'inventaire de son propre avatar |
| Chef de tribu | Son avatar, les avatars des membres **connectés**, les coffres et les montures de la tribu |
| Joueur sans tribu | Traité comme chef de lui-même : son avatar, ses coffres et ses montures |

**Limite non contournable** : un membre déconnecté n'a plus de personnage dans le monde, son inventaire est donc illisible. Le chef ne voit que les membres actuellement connectés.
