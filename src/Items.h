#pragma once

#include <string>

#include "API/ARK/Ark.h"

namespace QoL
{
	/**
	 * \brief `qol.items [search=...] [offset=N] [limit=N]`
	 *
	 * Catalogue des objets du serveur, lu dans le `MasterItemList` du jeu plutot
	 * que dans une liste ecrite a la main : il refletera donc toujours la version
	 * installee, mods compris.
	 */
	void RconItems(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);

	/**
	 * \brief `qol.give <eosId> <blueprint> [quantite] [qualite] [bp]`
	 *
	 * Remet un objet a un joueur connecte. Reserve au RCON : donner des objets
	 * releve de l'administration, une commande de chat l'ouvrirait a tous.
	 */
	void RconGive(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);

	/**
	 * \brief Classes d'objets du catalogue reellement en vigueur.
	 *
	 * Expose parce que le reglage du poids parcourt la meme liste : la dupliquer
	 * ferait diverger les deux chemins des qu'un mod remplace les donnees de jeu.
	 * Renvoie nullptr si aucun `MasterItemList` n'est lisible.
	 */
	TArray<UClass*>* FindItemClasses(std::string* source = nullptr);
} // namespace QoL
