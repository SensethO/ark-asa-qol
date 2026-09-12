#pragma once

#include "API/ARK/Ark.h"

namespace QoL
{
	/**
	 * \brief Controle du niveau des creatures sauvages a leur apparition.
	 *
	 * ARK n'offre aucun reglage natif pour un niveau minimum ni pour une
	 * repartition par tranche : `OverrideOfficialDifficulty` ne fixe que le
	 * plafond, les creatures apparaissant ensuite de 1 a ce maximum. Le niveau
	 * est donc redefini a l'apparition, selon des tranches ponderees.
	 */
	void InstallWildLevelHook();
	void RemoveWildLevelHook();

	/** Tire un niveau selon la repartition configuree. Expose pour les tests. */
	int PickWildLevel();

	/**
	 * \brief `qol.wildlevels [reload] [simulate=N]`
	 *
	 * Renvoie la repartition active, et permet de recharger config.json sans
	 * redemarrer le serveur. `simulate` tire N niveaux et renvoie leur
	 * repartition reelle par tranche de 10 : de quoi verifier que ce que le
	 * plugin applique correspond bien a ce qui a ete saisi.
	 */
	void RconWildLevels(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);
} // namespace QoL
