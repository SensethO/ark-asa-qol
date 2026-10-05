#pragma once

#include <string>

#include "API/ARK/Ark.h"

namespace QoL
{
	/** Description normalisee d'un objet d'inventaire */
	struct ItemLine
	{
		std::string name;
		std::string item_class;
		int quantity = 0;
		bool engram = false;
		bool skin = false;
	};

	/**
	 * \brief Categorise un objet.
	 *
	 * Partage entre les commandes RCON et la fenetre en jeu : la distinction
	 * entre objets portes, engrammes et skins ne doit exister qu'a un seul endroit.
	 */
	ItemLine ClassifyItem(UPrimalItem* item);

	/** True si la classe descend d'APrimalStructureItemContainer */
	bool IsContainerClass(UClass* cls);

	/** Position d'un acteur quelconque, structures comprises */
	FVector ActorPosition(AActor* actor);

	/** Reduit un chemin de blueprint a un nom lisible */
	std::string ShortName(const std::string& blueprint);

	/**
	 * \brief Commandes RCON d'inspection, renvoyant du JSON.
	 *
	 * Elles servent au gestionnaire de serveur : position en direct, inventaire
	 * et constructions. Exposees en RCON uniquement, ces donnees relevant de
	 * l'administration.
	 */
	void RconPlayers(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);
	void RconInventory(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);
	void RconStructures(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);
	void RconContainers(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);

	/**
	 * rief `qol.stored <eosId> [rayon]` — creatures rangees et oeufs fecondes.
	 *
	 * Une creature en cryopode et un oeuf feconde sont des OBJETS, pas des
	 * acteurs : `qol.dinos` ne les voit donc pas, et c'est ce trou que cette
	 * commande comble.
	 *
	 * Pour l'oeuf, la genetique est lisible telle quelle : espece, points de
	 * niveau et mutations sont des champs de `UPrimalItem`.
	 *
	 * Pour le cryopode, la creature vit dans `CustomItemDatas`, dont le bloc
	 * d'octets est une sauvegarde serialisee qu'on ne sait pas relire. Mais la
	 * structure porte aussi des chaines, des flottants et des classes typees :
	 * la commande en rapporte la DISPOSITION plutot que d'en deviner les
	 * index. On ecrira le lecteur sur ce qu'on aura vu.
	 */
	void RconStored(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);

	/**
	 * rief `qol.species <ClasseDeCreature>` — sonde, en lecture seule.
	 *
	 * Le niveau de naissance d'une creature se deduit de sa TORPEUR, seule
	 * statistique qu'aucun joueur ne peut monter : ni a l'apprivoisement, ni
	 * par l'empreinte, ni au niveau. Une inconnue, une equation.
	 *
	 *     torpeur = base x (1 + increment x niveau)
	 *
	 * Encore faut-il la base et l'increment de l'espece. Ils vivent sur le
	 * composant de statut, qu'on espere atteindre par l'objet par defaut de la
	 * classe — mais un composant est instancie par acteur, et rien ne garantit
	 * qu'il existe sur un objet par defaut.
	 *
	 * Cette commande ne suppose donc rien : elle rapporte ce qu'elle atteint,
	 * etape par etape. On ecrira le calcul sur ce qu'elle aura montre.
	 */
	void RconSpecies(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);
	void RconDinos(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);

	/**
	 * rief `qol.creative [show=0|1]` — drapeau d'affichage du mode creatif.
	 *
	 * Le jeu porte ce reglage a deux endroits : sur le mode de jeu, alimente par
	 * `Game.ini`, et sur l'etat de jeu, qui est replique vers les clients. Le menu
	 * pause etant dessine par le client, c'est le second qui commande l'affichage
	 * du bouton. La commande lit les deux avant de les modifier : l'ecart entre
	 * eux, s'il existe, explique a lui seul un reglage sans effet.
	 */
	/**
	 * rief `qol.deathcache <eosId> [rayon]` — depouilles et caches mortuaires.
	 *
	 * Un sac mortuaire n'est pas une construction ; il echappe donc a
	 * `qol.containers`, qui n'interroge que l'octree des structures. Celle-ci
	 * balaie l'octree spatial complet et filtre sur le nom de classe.
	 */
	void RconDeathCache(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);

	/** `qol.dinogroup` : groupes de creatures du menu T (choix du groupe + ordre) */
	void RconDinoGroup(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);

	/**
	 * rief `qol.gamemode` — valeurs reellement actives sur le mode de jeu.
	 *
	 * Un reglage ecrit dans la mauvaise section d'un fichier .ini est ignore
	 * sans le moindre message : la configuration semble juste et n'a aucun
	 * effet. Cette commande lit la valeur que le serveur applique vraiment,
	 * ce qui transforme une supposition sur l'emplacement d'une cle en fait
	 * observable. Lecture seule.
	 */
	void RconGameMode(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);

	void RconCreative(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);
} // namespace QoL
