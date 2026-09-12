#pragma once

#include "API/ARK/Ark.h"

namespace QoL
{
	/**
	 * \brief Le pont client -> serveur de la fenetre d'inventaire.
	 *
	 * Le mod ne peut pas appeler le plugin : un evenement Blueprint « executer
	 * sur le serveur » s'execute dans la machine virtuelle du moteur, hors de
	 * portee du C++. Le pont consiste donc a intercepter `UObject::ProcessEvent`,
	 * par ou passe tout appel d'evenement, et a n'y reconnaitre qu'un seul
	 * pointeur de fonction — celui de `Demander`. Un test de pointeur par appel,
	 * ce qui reste negligeable sur un chemin pourtant tres frequente.
	 *
	 * Chaque joueur recoit son propre relais, dont il est le proprietaire : une
	 * RPC client -> serveur n'est acceptee que sur un acteur possede par
	 * l'appelant. Un acteur unique partage ne pourrait servir qu'un joueur a la
	 * fois.
	 */
	void InstallLink();
	void RemoveLink();

	/** Cree le relais manquant de chaque joueur connecte. Appelee par le minuteur. */
	void TickLinks();

	/**
	 * rief Le relais d'un joueur, ou nullptr s'il n'en a pas encore.
	 *
	 * Sert a `/inv` : sans lui, l'envoi visait le premier acteur de cette
	 * classe trouve dans le monde. Avec un relais par joueur, ce premier
	 * acteur appartient a quelqu'un d'autre, et lui en prendre la propriete
	 * priverait son titulaire de sa touche.
	 */
	AActor* LinkFor(AShooterPlayerController* player);
} // namespace QoL
