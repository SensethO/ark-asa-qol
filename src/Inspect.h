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
