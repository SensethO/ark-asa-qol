#pragma once

#include "API/ARK/Ark.h"

namespace QoL
{
	/** Issue d'un reglage de poids, telle qu'elle est rapportee au RCON */
	struct WeightResult
	{
		bool catalog_found = false;
		/** Classes du catalogue, y compris celles dont l'objet par defaut est illisible */
		int total = 0;
		/** Classes reellement modifiees */
		int touched = 0;
		double multiplier = 1.0;
	};

	/**
	 * \brief Applique un multiplicateur au poids de base de tous les objets.
	 *
	 * ARK n'expose aucun reglage de poids : seules les piles sont configurables.
	 * Le multiplicateur est donc ecrit sur l'objet par defaut de chaque classe du
	 * `MasterItemList`, ce qui vaut pour les objets creees ensuite.
	 *
	 * Les poids d'origine sont memorises a la premiere application, et toute
	 * application ulterieure repart d'eux. Sans cela, appliquer 0,5 deux fois
	 * donnerait 0,25 et il deviendrait impossible de revenir au poids du jeu.
	 */
	WeightResult ApplyItemWeights(double multiplier);

	/**
	 * \brief Programme l'application prevue par la configuration.
	 *
	 * Le catalogue n'existe pas encore au chargement du plugin : les donnees de
	 * jeu ne sont lisibles qu'une fois le monde ouvert. L'application est donc
	 * differee, puis reessayee tant que le catalogue reste introuvable.
	 */
	void ScheduleStartupWeights();

	/**
	 * \brief `qol.weight [facteur]`
	 *
	 * Sans argument, rapporte l'etat courant. Avec un facteur, l'applique
	 * immediatement — `1` restitue les poids d'origine.
	 */
	void RconWeight(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);
} // namespace QoL
