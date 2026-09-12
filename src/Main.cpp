#include "API/ARK/Ark.h"

#include "Commands.h"
#include "Config.h"
#include "Link.h"
#include "Storage.h"
#include "Weights.h"
#include "WildLevels.h"

// Appelee par AsaApi au chargement du plugin
extern "C" __declspec(dllexport) void Plugin_Init()
{
	Log::Get().Init(PROJECT_NAME);

	// Toute exception qui s'echapperait d'ici ferait renoncer AsaApi au chargement :
	// le serveur perdrait l'ensemble des commandes a cause d'un seul detail.
	// Mieux vaut un plugin partiellement configure qu'un plugin absent.
	try
	{
		QoL::LoadConfig();
	}
	catch (const std::exception& error)
	{
		Log::GetLog()->error("Configuration illisible ({}) - valeurs par defaut", error.what());
	}

	try
	{
		QoL::Storage::Get().Load();
	}
	catch (const std::exception& error)
	{
		Log::GetLog()->error("Donnees illisibles ({}) - demarrage sans historique", error.what());
	}

	QoL::RegisterCommands();

	// L'interception est posee meme si la fonctionnalite est desactivee : elle se
	// contente alors de relayer l'appel, ce qui permet de l'activer depuis le
	// gestionnaire sans redemarrer le serveur.
	QoL::InstallWildLevelHook();

	// Le pont vers le mod : sans lui, la touche F3 et le bouton de
	// fermeture appellent dans le vide.
	QoL::InstallLink();

	// Le catalogue d'objets n'existe pas encore : les donnees de jeu ne sont
	// lisibles qu'une fois le monde ouvert. L'application est donc differee.
	QoL::ScheduleStartupWeights();

	Log::GetLog()->info("{} charge", PROJECT_NAME);
}

// Called by AsaApi when the plugin is unloaded
extern "C" __declspec(dllexport) void Plugin_Unload()
{
	QoL::RemoveWildLevelHook();
	QoL::RemoveLink();
	QoL::UnregisterCommands();
	QoL::Storage::Get().Save();

	Log::GetLog()->info("{} unloaded", PROJECT_NAME);
}
