#include "Link.h"

#include <string>
#include <unordered_map>

#include "Logger/Logger.h"

#include "Config.h"
#include "Util.h"
#include "Window.h"

namespace QoL
{
	namespace
	{
		constexpr const char* kHookName = "UObject.ProcessEvent(UFunction*,void*)";

		bool g_installed = false;

		/** Pointeurs des trois evenements du mod, resolus une fois pour toutes */
		UFunction* g_demander = nullptr;
		UFunction* g_chercher = nullptr;
		UFunction* g_ouvrir = nullptr;

		/**
		 * rief Ce que chaque joueur regarde.
		 *
		 * L'etat vit ici plutot que dans le widget, et ce choix en supprime
		 * beaucoup de travail Blueprint : le champ de recherche ignore quel
		 * contenant est ouvert, une ligne cliquee ignore ce qui est cherche.
		 * Chacun n'envoie que ce qu'il connait, et c'est le serveur qui combine.
		 */
		struct View
		{
			std::string term;
			std::string opened;
		};

		std::unordered_map<std::string, View> g_views;

		/** Un relais par joueur, indexe par identifiant EOS */
		std::unordered_map<std::string, TWeakObjectPtr<AActor>> g_links;

		UClass* LinkClass()
		{
			static UClass* cached = nullptr;
			if (cached != nullptr) return cached;

			const std::string& path = GetConfig().window.blueprint_path;
			if (path.empty()) return nullptr;

			cached = UVictoryCore::BPLoadClass(ToFString(path));
			if (cached == nullptr)
			{
				// La forme chargeable porte un `_C` final, absent du chemin d'asset
				const size_t closing = path.rfind('\'');
				if (closing == std::string::npos || closing == 0) return nullptr;
				cached = UVictoryCore::BPLoadClass(
					ToFString(path.substr(0, closing) + "_C" + path.substr(closing)));
			}
			return cached;
		}

		/** Le joueur a qui appartient un relais */
		AShooterPlayerController* OwnerOf(AActor* link)
		{
			if (link == nullptr) return nullptr;
			return static_cast<AShooterPlayerController*>(link->OwnerField().Get());
		}

		void ServirLaDemande(AActor* link)
		{
			AShooterPlayerController* viewer = OwnerOf(link);
			if (viewer == nullptr) return;

			const std::string eos = ToUtf8(AsaApi::IApiUtils::GetEOSIDFromController(viewer));
			const View& vue = g_views[eos];

			const std::vector<WindowRow> rows =
				BuildWindowRows(viewer, GetConfig().window.radius, vue.term, vue.opened);

			// Sans cette trace, un aller-retour reussi et un evenement jamais
			// recu produisent le meme journal : rien. C'est ce vide qui nous a
			// fait chercher la panne du cote du plugin alors qu'elle etait
			// dans le widget.
			Log::GetLog()->info("Pont : redessin ({} lignes, recherche=\"{}\", ouvert=\"{}\")",
				rows.size(), vue.term, vue.opened);

			SendWindowOn(link, rows);
		}

		/** Le seul parametre d'un de ces evenements : une chaine */
		std::string LireArgument(void* parms)
		{
			if (parms == nullptr) return std::string();
			return ToUtf8(*static_cast<const FString*>(parms));
		}

		void Chercher(AActor* link, void* parms)
		{
			AShooterPlayerController* viewer = OwnerOf(link);
			if (viewer == nullptr) return;

			View& vue = g_views[ToUtf8(AsaApi::IApiUtils::GetEOSIDFromController(viewer))];
			vue.term = LireArgument(parms);
			Log::GetLog()->info("Pont : Chercher recu -> \"{}\"", vue.term);

			// Une nouvelle recherche annule le depliage en cours : ses resultats
			// sont deja deplies, garder l'ancien contenant ouvert n'aurait aucun
			// sens et pourrait le montrer vide.
			vue.opened.clear();

			ServirLaDemande(link);
		}

		void Ouvrir(AActor* link, void* parms)
		{
			AShooterPlayerController* viewer = OwnerOf(link);
			if (viewer == nullptr) return;

			View& vue = g_views[ToUtf8(AsaApi::IApiUtils::GetEOSIDFromController(viewer))];
			const std::string demande = LireArgument(parms);

			// Bascule : recliquer le contenant ouvert le referme.
			vue.opened = (vue.opened == demande) ? std::string() : demande;
			Log::GetLog()->info("Pont : Ouvrir recu -> \"{}\" (etat : \"{}\")",
				demande, vue.opened);

			ServirLaDemande(link);
		}

		DECLARE_HOOK(UObject_ProcessEvent, void, UObject*, UFunction*, void*);

		void Hook_UObject_ProcessEvent(UObject* self, UFunction* function, void* parms)
		{
			// Chemin brulant : un seul test de pointeur avant de relayer.
			if (function != nullptr
				&& (function == g_demander || function == g_chercher || function == g_ouvrir))
			{
				// Une exception qui s'echapperait d'ici traverserait le moteur
				// jusqu'a l'arret du serveur, comme l'a montre la 93.15.
				try
				{
					AActor* link = static_cast<AActor*>(self);
					if (function == g_chercher) Chercher(link, parms);
					else if (function == g_ouvrir) Ouvrir(link, parms);
					else ServirLaDemande(link);
				}
				catch (const std::exception& error)
				{
					Log::GetLog()->error("Demander a echoue : {}", error.what());
				}
				catch (...)
				{
					Log::GetLog()->error("Demander a echoue (exception inconnue)");
				}
			}

			UObject_ProcessEvent_original(self, function, parms);
		}
	} // namespace

	AActor* LinkFor(AShooterPlayerController* player)
	{
		if (player == nullptr) return nullptr;

		const std::string eos = ToUtf8(AsaApi::IApiUtils::GetEOSIDFromController(player));
		auto found = g_links.find(eos);
		if (found == g_links.end()) return nullptr;

		AActor* link = found->second.Get();
		return (link != nullptr && OwnerOf(link) == player) ? link : nullptr;
	}

	void TickLinks()
	{
		UClass* cls = LinkClass();
		if (cls == nullptr) return;

		// La fonction n'existe qu'une fois la classe du mod chargee
		if (g_demander == nullptr)
		{
			g_demander = cls->FindFunctionByName(FName("Demander"), EIncludeSuperFlag::IncludeSuper);
			if (g_demander == nullptr) return;

			// Absents d'une version anterieure du mod : leur manque prive de la
			// recherche et du repliage, sans empecher le reste de fonctionner.
			g_chercher = cls->FindFunctionByName(FName("Chercher"), EIncludeSuperFlag::IncludeSuper);
			g_ouvrir = cls->FindFunctionByName(FName("Ouvrir"), EIncludeSuperFlag::IncludeSuper);

			// Resolus en silence jusqu'ici : un evenement absent du mod publie
			// faisait ignorer chaque clic sans un mot, ce qui est indiscernable
			// d'un clic qui n'arrive pas.
			Log::GetLog()->info("Pont : evenements du mod -> Demander={}, Chercher={}, Ouvrir={}",
				g_demander != nullptr, g_chercher != nullptr, g_ouvrir != nullptr);
		}

		UWorld* world = AsaApi::GetApiUtils().GetWorld();
		if (world == nullptr) return;

		for (TWeakObjectPtr<APlayerController> controller : world->PlayerControllerListField())
		{
			auto* pc = static_cast<AShooterPlayerController*>(controller.Get());
			if (pc == nullptr) continue;

			const std::string eos = ToUtf8(AsaApi::IApiUtils::GetEOSIDFromController(pc));
			if (eos.empty()) continue;

			auto found = g_links.find(eos);
			if (found != g_links.end() && found->second.Get() != nullptr
				&& OwnerOf(found->second.Get()) == pc)
			{
				continue;
			}

			FActorSpawnParameters spawn{};
			spawn.Owner = pc;
			spawn.SpawnCollisionHandlingOverride[0] = 1;  // AlwaysSpawn
			spawn.bNoFail = 1;

			FVector position = AsaApi::IApiUtils::GetPosition(pc);
			FRotator rotation{0, 0, 0};

			AActor* link = world->SpawnActor(cls, &position, &rotation, &spawn);
			if (link == nullptr)
			{
				Log::GetLog()->error("Relais impossible a creer pour {}", eos);
				continue;
			}

			// Sans cela le relais ne parviendrait pas au client, et la touche F3
			// n'aurait aucun acteur possede sur lequel emettre son appel.
			link->SetReplicates(true);
			link->bAlwaysRelevant() = true;
			link->bOnlyRelevantToOwner() = true;

			g_links[eos] = GetWeakReference(link);
			Log::GetLog()->info("Relais cree pour {}", eos);
		}
	}

	void InstallLink()
	{
		if (g_installed) return;

		if (!AsaApi::GetHooks().SetHook(kHookName, &Hook_UObject_ProcessEvent,
		                                &UObject_ProcessEvent_original))
		{
			Log::GetLog()->error("Interception de ProcessEvent impossible : F3 et le bouton de fermeture resteront inertes");
			return;
		}

		g_installed = true;
		Log::GetLog()->info("Pont client : interception posee");
	}

	void RemoveLink()
	{
		if (!g_installed) return;

		AsaApi::GetHooks().DisableHook(kHookName, &Hook_UObject_ProcessEvent);
		g_installed = false;
		g_demander = nullptr;
		g_chercher = nullptr;
		g_ouvrir = nullptr;
		g_links.clear();
		g_views.clear();
	}
} // namespace QoL
