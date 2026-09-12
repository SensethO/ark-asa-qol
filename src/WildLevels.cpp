#include "WildLevels.h"

#include <algorithm>
#include <map>
#include <random>

#include "json.hpp"

#include "Config.h"
#include "Util.h"

namespace QoL
{
	namespace
	{
		constexpr const char* kHookName = "APrimalDinoCharacter.BeginPlay()";

		bool g_installed = false;

		/**
		 * \brief Generateur du tirage.
		 *
		 * Statique et unique : le hook est appele des milliers de fois au
		 * chargement d'une carte, en construire un par appel couterait bien plus
		 * que le tirage lui-meme.
		 */
		std::mt19937& Rng()
		{
			static std::mt19937 rng{std::random_device{}()};
			return rng;
		}

		/**
		 * \brief Vrai si la creature est sauvage.
		 *
		 * Meme regle que le recensement `qol.dinos` : une creature apprivoisee
		 * porte le nom de son dompteur et appartient a une tribu (equipe >= 50000).
		 */
		bool IsWild(APrimalDinoCharacter* dino)
		{
			if (ToUtf8(dino->TamerStringField()).empty() == false) return false;
			return dino->TargetingTeamField() < 50000;
		}
	} // namespace

	// La macro ouvre la definition du detour et declare le pointeur vers la
	// fonction d'origine ; elle doit rester a la portee ou le detour est reference.
	DECLARE_HOOK(APrimalDinoCharacter_BeginPlay, void, APrimalDinoCharacter*);

	int PickWildLevel()
	{
		const WildLevelSettings& settings = GetConfig().wild_levels;

		const int floor_level = (std::max)(1, settings.min_level);
		const int ceiling_level = (std::max)(floor_level, settings.max_level);

		if (settings.bands.empty())
		{
			std::uniform_int_distribution<int> uniform{floor_level, ceiling_level};
			return uniform(Rng());
		}

		// Les parts sont normalisees ici plutot qu'a la lecture : la configuration
		// reste lisible telle que l'administrateur l'a ecrite, meme si elle ne
		// totalise pas exactement 100.
		float total = 0.f;
		for (const WildLevelBand& band : settings.bands) total += band.percent;
		if (total <= 0.f)
		{
			std::uniform_int_distribution<int> uniform{floor_level, ceiling_level};
			return uniform(Rng());
		}

		std::uniform_real_distribution<float> draw{0.f, total};
		float target = draw(Rng());

		const WildLevelBand* chosen = &settings.bands.back();
		for (const WildLevelBand& band : settings.bands)
		{
			target -= band.percent;
			if (target <= 0.f)
			{
				chosen = &band;
				break;
			}
		}

		const int low = (std::max)(floor_level, chosen->from);
		const int high = (std::min)(ceiling_level, (std::max)(low, chosen->to));

		std::uniform_int_distribution<int> within{low, high};
		return within(Rng());
	}

	void __fastcall Hook_APrimalDinoCharacter_BeginPlay(APrimalDinoCharacter* dino)
	{
		// Le hook s'execute sur chaque creature de la carte : tout ce qui precede
		// le repli doit rester trivial, y compris pendant le chargement du monde.
		const WildLevelSettings& settings = GetConfig().wild_levels;
		if (!settings.enabled || dino == nullptr)
		{
			APrimalDinoCharacter_BeginPlay_original(dino);
			return;
		}

		try
		{
			// AbsoluteBaseLevel ne sert qu'a l'apparition : il vaut 0 sur les
			// creatures existantes, et impose le niveau quand il est pose avant que
			// BeginPlay n'initialise les statistiques. Mesure faite sur serveur reel :
			// avec une consigne 100-150, les creatures apparaissent bien entre 100 et
			// 150 aux parts demandees.
			//
			// Aucune distinction n'est faite entre une creature qui apparait et une
			// creature relue de la sauvegarde : le composant de statut porte deja un
			// niveau dans les deux cas au moment de l'appel. Une tentative de les
			// separer sur ce critere n'ecartait pas les creatures relues, elle
			// desactivait la fonctionnalite entiere.
			// Une creature inconsciente est ecartee. Poser AbsoluteBaseLevel fait
			// reinitialiser les statistiques au niveau tire, torpeur comprise : un
			// animal en cours d'apprivoisement, relu de la sauvegarde apres un
			// redemarrage, se reveillait et repartait, progres perdu. Constate sur
			// un triceratops le 2026-09-01.
			if (IsWild(dino) && !dino->bIsSleeping()())
			{
				dino->AbsoluteBaseLevelField() = PickWildLevel();
			}
		}
		catch (...)
		{
			// Une exception ici ne doit jamais empecher la creature d'exister
		}

		APrimalDinoCharacter_BeginPlay_original(dino);
	}

	void InstallWildLevelHook()
	{
		if (g_installed) return;

		if (AsaApi::GetHooks().SetHook(kHookName, &Hook_APrimalDinoCharacter_BeginPlay,
		                               &APrimalDinoCharacter_BeginPlay_original))
		{
			g_installed = true;
			Log::GetLog()->info("Niveaux sauvages : interception de l'apparition active");
		}
		else
		{
			Log::GetLog()->error("Niveaux sauvages : {} introuvable - les niveaux resteront ceux du jeu", kHookName);
		}
	}

	void RemoveWildLevelHook()
	{
		if (!g_installed) return;

		AsaApi::GetHooks().DisableHook(kHookName, &Hook_APrimalDinoCharacter_BeginPlay);
		g_installed = false;
	}

	void RconWildLevels(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		bool reload = false;
		int simulate = 0;
		for (int i = 1; i < args.Num(); ++i)
		{
			const std::string arg = Lowercase(ToUtf8(args[i]));
			if (arg == "reload") reload = true;
			else if (arg.rfind("simulate=", 0) == 0)
				simulate = (std::min)(200000, (std::max)(0, std::atoi(arg.substr(9).c_str())));
		}

		std::string reload_error;
		if (reload)
		{
			try
			{
				if (!LoadConfig()) reload_error = "config.json illisible - reglages precedents conserves";
			}
			catch (const std::exception& error)
			{
				reload_error = error.what();
			}
		}

		const WildLevelSettings& settings = GetConfig().wild_levels;

		nlohmann::json bands = nlohmann::json::array();
		for (const WildLevelBand& band : settings.bands)
			bands.push_back({{"from", band.from}, {"to", band.to}, {"percent", band.percent}});

		nlohmann::json payload{
			{"enabled", settings.enabled},
			{"hooked", g_installed},
			{"minLevel", settings.min_level},
			{"maxLevel", settings.max_level},
			{"bands", bands},
		};

		if (!reload_error.empty()) payload["error"] = reload_error;

		if (simulate > 0)
		{
			// Comptage par tranche de 10 alignee sur les dizaines, unite de lecture
			// demandee cote gestionnaire
			std::map<int, int> per_decade;
			for (int i = 0; i < simulate; ++i) per_decade[(PickWildLevel() / 10) * 10] += 1;

			nlohmann::json sample = nlohmann::json::array();
			for (const auto& [decade, count] : per_decade)
				sample.push_back({
					{"from", decade == 0 ? 1 : decade},
					{"to", decade + 9},
					{"count", count},
					{"percent", 100.0 * count / simulate},
				});

			payload["sampleSize"] = simulate;
			payload["sample"] = sample;
		}

		FString response = ToFString(payload.dump());
		connection->SendMessageW(packet->Id, 0, &response);
	}
} // namespace QoL
