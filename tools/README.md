# Écrire un graphe Blueprint depuis l'extérieur du Dev Kit

Un graphe Blueprint se copie et se colle **en texte**. C'est la seule voie
d'automatisation praticable pour ce projet : elle évite des dizaines de
manipulations à la souris, chacune étant une occasion de se tromper de broche.

## Pourquoi pas l'API Python

Le Dev Kit embarque `PythonScriptPlugin`, déjà activé, et il s'exécute sans
interface :

```bash
ShooterGameEditor-Cmd.exe ShooterGame.uproject -run=pythonscript -script="mon_script.py" -unattended -nosplash
```

L'API donne accès aux assets, aux propriétés, aux structures et aux cuissons.
Elle **n'expose pas les nœuds de graphe** — `K2Node_CustomEvent` et consorts
sont absents de `dir(unreal)`. Créer un événement ou relier deux broches par
script est donc hors de portée.

`RemoteControl` est également hors de portée, pour une autre raison : les
sources sont présentes dans `Engine/Plugins/VirtualProduction/RemoteControl`
mais **aucun binaire n'est compilé** pour cet éditeur. Le Dev Kit est livré
précompilé ; l'activer ferait échouer le démarrage sur un module introuvable.

## La voie du presse-papiers

1. Générer le texte T3D des nœuds voulus (`gen_showwindow.py` en donne le
   modèle)
2. `Get-Content fichier.t3d -Raw | Set-Clipboard`
3. Dans le graphe, cliquer dans le vide et **Ctrl+V**

Le chemin inverse sert à apprendre : sélectionner des nœuds qui fonctionnent,
**Ctrl+C**, puis `Get-Clipboard -Raw` livre leur grammaire exacte.

## Règles apprises à la dure

**Ne jamais écrire de grammaire de mémoire.** Sur les douze nœuds du premier
essai, les dix repris d'un export réel sont passés ; les trois écrits d'après la
signature C++ supposée ont échoué. Toujours faire copier un exemplaire du nœud
inconnu avant de le générer.

**Chaque liaison s'écrit des deux côtés.** Le `LinkedTo` d'une broche ne suffit
pas : la broche d'en face doit porter le lien réciproque, sinon Unreal la laisse
orpheline sans rien signaler. `gen_showwindow.py` vérifie la symétrie avant
d'écrire.

**Les types de broche doivent coïncider exactement**, `bIsReference` et
`bIsConst` compris. Une divergence fait rejeter la liaison au collage, et le
message parle d'un « type indéterminé » plutôt que du vrai motif.

**Les entrées d'un événement se déclarent deux fois** : par une broche
`CustomProperties Pin` *et* par une ligne `CustomProperties UserDefinedPin`.
Sans la seconde, le nœud se recrée sans paramètre.

**La réplication tient dans un nombre.** `FunctionFlags=218235072`
(`0x0D0200C0`) vaut « Exécuter sur le client propriétaire » + « Fiable » :
`Net | NetReliable | Public | NetClient | BlueprintCallable | BlueprintEvent`.

**Un `K2Node_BreakStruct` exige les noms manglés** des champs
(`Tab_2_2948453E...`), pas les noms lisibles. Ils se relèvent dans un export, ou
dans le `.uasset` de la structure.

**Les nœuds de tableau ne sont pas des appels de fonction ordinaires :**

| Nœud | Classe | Broches |
|---|---|---|
| Length | `K2Node_CallArrayFunction` + `bDefaultsToPureFunc=True` | `self`, `TargetArray`, `ReturnValue` |
| Get (a copy) | `K2Node_GetArrayItem` + `bReturnByRefDesired=False` | `Array`, `Dimension 1`, `Output` |

`KismetArrayLibrary::Array_Get` existe mais est marquée
*BlueprintInternalUseOnly* : un collage qui l'invoque est **rejeté en silence**,
le nœud n'apparaît simplement pas.

## Widgets UMG : la meme voie, une autre grammaire

Le concepteur UMG copie et colle lui aussi en texte. Un widget conteneur declare
ses emplacements **deux fois** : une declaration vide, puis un bloc portant
`LayoutData` / `Parent` / `Content`, et les reference enfin par `Slots(N)`. Les
enfants sont des blocs independants au premier niveau, designes par `Content=`.
La racine copiee est accompagnee d'un `WidgetSlotPair` qui la nomme.

Seules les proprietes **differentes du defaut** sont ecrites.

Grammaire relevee sur des exports reels :

```
LayoutData=(Offsets=(Left=..,Top=..,Right=..,Bottom=..),Alignment=(X=0.5,Y=0.5))
Padding=(Left=16.000000,Top=16.000000,Right=16.000000,Bottom=16.000000)
Size=(SizeRule=Fill)
Brush=(ImageType=FullColor,ImageSize=(X=512,Y=512),
       ResourceObject="/Script/Engine.Texture2D'/Chemin/Texture.Texture'")
Text=NSLOCTEXT("[32 hex]", "32 hex", "le texte")
Font=(Size=28.000000)
bIsVariable=True
DisplayLabel="Nom lisible"
```

Classes verifiees : `CanvasPanel`, `CanvasPanelSlot`, `VerticalBox`,
`VerticalBoxSlot`, `HorizontalBox`, `TextBlock`, `Image`, et
`/Script/UMGEditor.WidgetSlotPair`.

**Un collage refuse ne dit rien.** Ctrl+V ne produit simplement aucun widget, sans
message ni entree de journal. **Reduire au prouve, puis reintroduire un element
a la fois** est la seule facon d'avancer ici.

**L'ordre des champs de `LayoutData` compte.** Un premier jet ecrivait
`Anchors` avant `Offsets` et etait rejete en bloc ; l'ordre des exports du jeu
est `Offsets`, puis `Anchors`, puis `Alignment`, et la meme mise en page passe
alors sans broncher — `ScrollBox` comprise, que j'avais soupconnee a tort.

**La racine d'un collage doit etre un conteneur.** Un `TextBlock` seul est refuse
en silence ; enveloppe dans une `VerticalBox`, le meme texte passe du premier
coup.

`Anchors` reste **non verifie** : le selecteur d'ancrage du concepteur n'ecrit
rien tant qu'on ne clique pas la grille, et l'alignement seul ne suffit pas a
centrer.

**Attention a « Remplacer par »** dans la hierarchie : l'operation conserve le nom
et les liaisons, mais **perd `bIsVariable`**. A recocher apres coup, sinon le
graphe ne voit plus le widget.

## Fichiers

- `gen_showwindow.py` — générateur de la chaîne `ShowInventoryWindow`
- `exemple-graphe-qolrow.t3d` — export de référence : événement répliqué,
  widget, `Format Text`, `Break`
- `exemple-graphe-tableau.t3d` — export de référence : `Length` et
  `Get (a copy)`
