# Le style exact de l'inventaire d'ARK

Valeurs relevées sur `/Game/PrimalEarth/UI/DataListEntryWidgetPrimalItem`, la
cellule d'objet de l'inventaire, exportée depuis le concepteur UMG. Rien ici
n'est déduit.

## Ce que la cellule est réellement

Une **case carrée**, pas une ligne de texte : la racine est
`/Script/ShooterGame.DataListEntryButton_PrimalItem`, une classe **C++** d'ARK,
qui contient un `Overlay` avec un `MenuAnchor` de menu contextuel et une image
de sélection. Sa mécanique n'est donc pas reproductible depuis un mod — son
apparence, si.

## Couleurs

| Rôle | Valeur linéaire |
|---|---|
| Texte d'affichage (le cyan d'ARK) | `R=0.501961 G=0.905882 B=1.000000 A=1.000000` |
| Teinte de la case | `R=0.001518 G=0.015996 B=0.038204 A=0.700000` |
| Fond du bouton | `R=0.000000 G=0.004025 B=0.006995 A=0.500000` |
| Sélection | `R=0.130208 G=0.525453 B=1.000000 A=1.000000` |
| Contour au survol | `R=0.695111 G=0.695111 B=0.695111 A=1.000000` |
| Lueur du texte | `R=0 G=0 B=0 A=0.200000` |

Attention : les propriétés `FColor` d'ARK s'écrivent en **B,G,R,A** dans le
texte exporté — par exemple `DurabilityBarFgColor=(B=136,G=156,R=1,A=255)`.
Les `FLinearColor` ci-dessus sont en R,G,B,A normal.

## Polices

```
DetailTextFont    /Game/PrimalEarth/UI/Fonts/OpenSansSemiBold30-em34
DisplayStringFont /Game/PrimalEarth/UI/Fonts/OpenSansRegular30-em34_Offline
```

Tailles employées : **12** pour la quantité, les munitions et le poids ;
`TextArmorDamageSize=0.4` et `ItemIconScale=0.7` sont des facteurs d'échelle.
`EnableTextGlow=True`.

## Textures

```
Case             /Game/PrimalEarth/UI/UI_Final_Assets/inventory_cell_99size
Sélecteur        /Game/PrimalEarth/UI/UI_Final_Assets/inventory_cell_selector
Cadre normal     /Game/PrimalEarth/UI/Inventory/Textures/ItemNormalBox
Cadre survolé    /Game/PrimalEarth/UI/Inventory/Textures/ItemHighlightBox
Équipé           /Game/PrimalEarth/UI/Inventory/Textures/SlotEquippedBgOverlay
Rareté           /Game/PrimalEarth/UI/UI_Final_Assets/rarity_gradient2
```

## Le point le plus utile : comment ARK étire ses cadres

```
DrawAs=Box
ImageSize=(X=128.000000,Y=128.000000)
Margin=(Left=0.015600,Top=0.015600,Right=0.015600,Bottom=0.015600)
```

`DrawAs=Box` avec une marge de **0,0156** découpe la texture en neuf tranches :
les coins gardent leur taille, les bords s'étirent. C'est ce qui permet à une
case de 128 px de rester nette à n'importe quelle dimension. Une brosse laissée
en `DrawAs=Image` — le défaut — étire tout, coins compris, et le cadre se
déforme.

Le `SlotEquippedBgOverlay` emploie une marge bien plus large, `0.333330`, parce
que son cadre est épais.

## Grammaire de brosse, vérifiée

```
Brush=(TintColor=(SpecifiedColor=(R=..,G=..,B=..,A=..)),
       DrawAs=Box,
       ImageSize=(X=128.000000,Y=128.000000),
       Margin=(Left=0.015600,Top=0.015600,Right=0.015600,Bottom=0.015600),
       ResourceObject="/Script/Engine.Texture2D'/Chemin/Texture.Texture'",
       OutlineSettings=(CornerRadii=(X=4,Y=4,Z=4,W=4),
                        Color=(SpecifiedColor=(R=..,G=..,B=..,A=..)),
                        Width=1.000000,
                        RoundingType=FixedRadius,
                        bUseBrushTransparency=True))
```

Un style de bouton se décline en quatre états — `Normal`, `Hovered`, `Pressed`,
`Disabled` — chacun portant une brosse complète, plus `NormalPadding` et
`PressedPadding`.

## Le cadre : `InventoryPanel`

Relevé sur `/Game/PrimalEarth/UI/Inventory/InventoryPanel` — 132 widgets, dont
26 images, 24 `SizeBox` et 15 `Overlay`. La racine est une `UMG.Border` dont le
fond est **transparent** (`TintColor` alpha 0) : le cadre visible vient des
images enfants, pas de la bordure racine.

### Cadres génériques d'ARK

```
/Game/PrimalEarth/UI/Textures/BaseBorderNormal
/Game/PrimalEarth/UI/Textures/BaseBorderHighlight
/Game/PrimalEarth/UI/BaseBorderBroken
      DrawAs=Box   Margin=(Left=0.333300,Top=0.333300,Right=0.333300,Bottom=0.333300)
```

`Inventory_panel`, que nous employons aujourd'hui, n'est pas le fond de cet
écran — il sert ailleurs. Les trois textures ci-dessus sont le vrai cadre.

### Boutons — utilisables pour les onglets

```
/Game/PrimalEarth/UI/UI_Final_Assets/button_style1_default
                                     button_style1_hover
                                     button_style1_pressed
      DrawAs=Box   Margin=(Left=0.816000,Top=0.720000,Right=0.784000,Bottom=0.720000)
```

### En-tête d'onglets de l'inventaire

```
/Game/PrimalEarth/UI/Inventory/Textures/3_tab_header_1st_selec
                                        3_tab_header_2nd_selec
                                        3_tab_header_3rd_selec
                                        InventoryTabSeparator
                                        InventoryTabSeparatorMirrored
```

### Autres éléments utiles

```
search_box_edit    Margin=(Top=0.544000,Right=0.768000)
T_DropdownBox      Margin=(0.0156)
chevron            Margin=(0.0156)
SplittingLine      /Game/PrimalEarth/UI/Textures/SplittingLine
```

Sur 104 brosses relevées, **28 sont dessinées en `Box`** — ce sont exactement
celles qui doivent s'étirer sans se déformer.

## Texte : la grammaire complète, enfin vérifiée

C'est ce relevé qui a livré l'écriture d'une police sur un `TextBlock`, la
dernière qui manquait :

```
Font=(FontObject="/Script/Engine.Font'/Game/PrimalEarth/UI/Fonts/GoBoldRegular30.GoBoldRegular30'",
      OutlineSettings=(OutlineColor=(R=0.000000,G=0.000000,B=0.000000,A=0.400000)),
      TypefaceFontName="",
      Size=26.000000)
ColorAndOpacity=(SpecifiedColor=(R=0.533333,G=0.913725,B=1.000000,A=1.000000))
ShadowColorAndOpacity=(R=0.000000,G=0.000000,B=0.000000,A=0.500000)
Justification=Center
```

Les trois registres employés par l'écran :

| Usage | Police | Taille |
|---|---|---|
| Titres et onglets | `GoBoldRegular30` | 26 |
| Texte courant | `OpenSansRegular30-em34` | 18 |
| Boutons | `OpenSansRegular30-em34` | 15 |

Tous avec le même cyan `0.533333, 0.913725, 1.000000` et le même contour noir à
40 %. Note : la cellule d'objet emploie un cyan très légèrement différent
(`0.501961, 0.905882, 1.0`) — ARK n'est pas parfaitement homogène.

`ColorAndOpacity=(ColorUseRule=UseColor_Foreground)` apparaît aussi : le texte
hérite alors de la couleur de son bouton parent.
