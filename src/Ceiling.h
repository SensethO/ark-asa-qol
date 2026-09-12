#pragma once

namespace QoL
{
	/**
	 * \brief Empeche la mort par la barriere du monde.
	 *
	 * ARK detruit — et non tue — tout ce qui monte trop haut : l'acteur est
	 * supprime avec son inventaire, et aucun sac mortuaire n'est depose. Aucun
	 * reglage de serveur n'atteint cette barriere, qui est un volume place dans
	 * la carte et non une fonction du moteur.
	 *
	 * Le seul recours est donc de redescendre le joueur avant qu'il ne
	 * l'atteigne. Appelee par le minuteur, une fois par seconde.
	 */
	void EnforceCeiling();
} // namespace QoL
