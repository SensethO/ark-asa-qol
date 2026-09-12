# Étude — fenêtre d'inventaire en jeu via un mod Dev Kit

Faisabilité d'une interface en jeu (fenêtre à onglets, recherche, tri, coordonnées) pour consulter les inventaires des avatars et des contenants, avec les règles d'accès demandées : un membre simple ne voit que son propre avatar, le chef de tribu voit tout.

---

## Verdict

**C'est faisable, et le mécanisme est prouvé** — mais cela déborde du plugin serveur : il faut un mod client construit dans l'ARK Dev Kit, installé par chaque joueur.

Le plugin AsaQoL existant reste utile : il devient la **source de données**. Le mod n'apporte que l'affichage.

---

## Pourquoi le plugin seul ne suffit pas

Un plugin AsaApi est une DLL chargée dans le processus **serveur**. Il peut lire l'état du monde, exécuter des commandes et envoyer du texte. Il n'a aucun accès au moteur de rendu du client : ni fenêtre, ni bouton, ni champ de saisie. Ces éléments sont des widgets UMG qui vivent dans le client, et un serveur ne peut pas en fabriquer à distance.

Les seules sorties dont il dispose vers un joueur sont le chat et la notification à l'écran — du texte, non interactif.

---

## Le pont serveur → client

AsaApi documente un mécanisme précis, utilisé par le mod officiel « AsaApiUtils ». Il est visible dans `AsaApiModUtils.hpp` :

1. Le **mod** publie un acteur singleton, chargé par chemin de blueprint :

```cpp
UVictoryCore::BPLoadClass("Blueprint'/AsaApiUtils/ApiUtilsSingleton.ApiUtilsSingleton'");
```

   Le plugin le retrouve aussi via une liste d'acteurs nommée (`GetCustomActorList(world, "ApiUtilsCCA")`).

2. Le **plugin** appelle une fonction Blueprint de ce singleton :

```cpp
Singleton->ProcessEvent(Singleton->FindFunctionChecked(FName("AddNotification")), &params);
```

3. Les paramètres transitent par un struct C++ qui doit **refléter exactement** la signature de la fonction Blueprint — même ordre, mêmes types, y compris les paramètres de retour :

```cpp
struct AsaApiUtilsNotification
{
    FString NotificationId;      // paramètre de sortie
    FString Text;
    TArray<FString> RecipientEOS; // ciblage par joueur
    FLinearColor BackgroundColor;
    FLinearColor TextColor;
    double DisplayScale;
    double DisplayTime;
    Position TextJustification;
    Position NotificationScreenPosition;
    bool bAddToChat;
};
```

`RecipientEOS` montre l'essentiel : **le ciblage par joueur est natif**. C'est ce qui permet d'appliquer les règles d'accès — n'envoyer les données d'un membre qu'au chef, et à personne d'autre.

Ce n'est donc pas un bricolage : c'est le schéma d'intégration prévu par l'API.

---

## Architecture cible

```
   Joueur tape /inv en jeu
             │
             ▼
   ┌───────────────────────┐   le plugin décide QUI a le droit de voir QUOI
   │  Plugin AsaQoL (C++)  │   (chef de tribu via FTribeData::OwnerPlayerDataID)
   │  collecte + filtre    │
   └───────────┬───────────┘
               │ ProcessEvent("ShowInventoryWindow", &params)
               ▼
   ┌───────────────────────┐
   │ Singleton du mod (BP) │   côté serveur, puis RPC client ciblé
   └───────────┬───────────┘
               │ Client RPC (RecipientEOS)
               ▼
   ┌───────────────────────┐
   │  Widget UMG (client)  │   onglets, recherche, tri, coordonnées
   └───────────────────────┘
```

**Le contrôle d'accès reste côté serveur**, dans le plugin. C'est le point important : un mod client peut être modifié par un joueur, il ne doit donc jamais recevoir de données qu'il n'a pas le droit de voir. Le plugin n'envoie que ce qui est autorisé — filtrer dans le widget serait une faille.

La collecte est **déjà écrite** : `qol.inventory`, `qol.containers` et `qol.structures` produisent exactement ces données, coordonnées comprises. Il ne resterait qu'à les router vers le singleton au lieu du RCON.

---

## Ce qu'il faut construire dans le Dev Kit

| Élément | Nature | Rôle |
|---|---|---|
| Acteur singleton | Blueprint | Point d'entrée appelable par le plugin |
| Fonction `ShowInventoryWindow` | Événement BP | Reçoit la charge utile et cible les destinataires |
| Structure de données | Struct BP | Doit correspondre au struct C++, champ par champ |
| Widget principal | UMG | Fenêtre, onglets, champ de recherche, en-têtes triables |
| Sous-widget de ligne | UMG | Une ligne d'objet ou de contenant |
| Déclencheur | BP ou touche | Ouvre la fenêtre côté client |

Le plus délicat n'est pas le widget : c'est **l'alignement du struct**. Un champ décalé ou un type différent entre le Blueprint et le C++ provoque une corruption mémoire, souvent un crash serveur — sans message clair.

---

## Prérequis

| | Détail |
|---|---|
| **ARK Dev Kit** | Gratuit via l'Epic Games Launcher. Environ **200 Go** installés, plus l'espace de cuisson des mods |
| **Machine** | Éditeur Unreal Engine 5 : au moins 32 Go de RAM confortables, SSD, GPU correct |
| **Compte CurseForge** | Obligatoire : c'est le seul canal de distribution des mods ASA |
| **Temps de cuisson** | Chaque itération du mod demande une recompilation, de plusieurs minutes à plus d'une heure |

---

## Distribution et contraintes pour les joueurs

- Le mod doit figurer dans la liste `-mods=` du serveur — le gestionnaire sait déjà le faire, onglet Mods.
- **Chaque joueur doit l'avoir installé.** ARK le télécharge automatiquement à la connexion, mais le mod devient une dépendance de ton serveur : un joueur qui refuse ou dont le téléchargement échoue ne peut pas jouer.
- Cela change la nature de ton serveur : il passe de « vanilla » à « moddé » dans le navigateur de serveurs, ce qui filtre une partie des joueurs.

---

## Maintenance

C'est le point que je souligne le plus, parce qu'il est durable :

- **À chaque mise à jour majeure d'ARK**, le mod doit être rouvert dans le Dev Kit, recuit et republié.
- **AsaApi doit lui aussi suivre** : tant qu'une version compatible n'est pas publiée, le serveur tourne sans plugins — donc sans la source de données du mod.
- Les deux dépendances se cumulent : la fenêtre ne fonctionne que si le mod **et** le plugin **et** AsaApi sont tous à jour en même temps.

Le plugin seul, lui, ne dépend que d'AsaApi.

---

## Ce que je peux faire, et ce que je ne peux pas

**Je peux :**

- Écrire tout le côté serveur : collecte, contrôle d'accès chef/membre, structs C++, appels `ProcessEvent`.
- Définir précisément le contrat de données à reproduire côté Blueprint.
- Rédiger la marche à suivre pour le Dev Kit, étape par étape.

**Je ne peux pas :**

- **Construire les Blueprints et les widgets UMG.** Le Dev Kit est un éditeur graphique : les assets sont des fichiers binaires créés à la souris, pas du code que j'écris. Je n'y ai pas accès depuis ici, et il n'est pas installé sur ta machine.
- Cuire ni publier le mod sur CurseForge.

C'est la limite franche de cette voie : la moitié du travail t'incombe, dans un outil que tu ne connais peut-être pas encore.

---

## Charge et risques

| Étape | Ordre de grandeur |
|---|---|
| Installation et prise en main du Dev Kit | 1 à 2 jours, essentiellement du téléchargement et de la découverte |
| Singleton + structure de données + pont testé | 1 jour |
| Widget UMG avec onglets, recherche, tri | 2 à 4 jours selon l'exigence visuelle |
| Cuisson, publication, tests multijoueurs | 1 jour |
| Adaptation du plugin serveur | Quelques heures — je m'en charge |

**Risques principaux :** l'alignement des structs (crash serveur silencieux), la courbe d'apprentissage d'UMG si tu n'en as jamais fait, et la charge de maintenance à chaque patch.

---

## Alternatives à considérer

1. **Réutiliser le mod AsaApiUtils existant.** Il est déjà publié et gère notifications, sphères et lignes. Mais il n'expose **aucune fenêtre générique** : il ne permettrait pas les onglets et la recherche. Il servirait tout au plus à afficher un résumé mieux mis en forme.

2. **Commandes de chat.** Même fonction, forme dégradée : `/inv`, `/inv <joueur>`, `/coffres` avec pagination, recherche par mot-clé et tri. Disponible immédiatement, aucune installation joueur, aucune dépendance supplémentaire.

3. **Le gestionnaire web.** Il fait **déjà** ce que tu décris — onglets, recherche, tri, coordonnées, contenu des coffres. Il est hors du jeu et réservé à l'administrateur, mais rien n'empêche d'y ouvrir un accès en lecture aux chefs de tribu : le système de comptes et de rôles existe déjà.

---

## Recommandation

La voie du mod Dev Kit est la seule qui donne exactement la fenêtre décrite. Elle est réaliste, mais son coût réel n'est pas le développement : c'est **la dépendance imposée à tous les joueurs et la maintenance à chaque patch d'ARK**.

Si l'objectif est que les joueurs disposent de la fonction rapidement, les commandes de chat la livrent aujourd'hui, sans rien imposer à personne. Si l'objectif est le confort visuel et que tu acceptes de maintenir un mod, la voie Dev Kit tient debout — et le travail serveur déjà fait y sera intégralement réutilisé.

Une troisième piste mérite examen avant de s'engager : ouvrir le gestionnaire web aux chefs de tribu. L'interface existe, elle est déjà exactement celle que tu décris, et cela ne coûterait qu'un rôle supplémentaire et un filtre par tribu.
