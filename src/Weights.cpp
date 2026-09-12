#include "Weights.h"

#include <cstdlib>
#include <string>
#include <unordered_map>

#include "json.hpp"
#include "Logger/Logger.h"
#include "Timer.h"

#include "Config.h"
#include "Items.h"
#include "Util.h"

namespace QoL
{
	namespace
	{
		/**
		 * Poids d'origine, releves a la premiere application.
		 *
		 * Indexes par classe : les `UClass*` restent valides pour toute la duree
		 * de vie du serveur, les objets de donnees n'etant jamais dechargees.
		 * Sans ce releve, appliquer 0,5 puis 0,5 donnerait 0,25 et le poids du jeu
		 * serait perdu jusqu'au redemarrage.
		 */
		std::unordered_map<UClass*, float> g_original;

		/** Multiplicateur en vigueur, tel qu'applique pour la derniere fois */
		double g_applied = 1.0;

		/** Nombre de tentatives de l'application differee au demarrage */
		int g_attempts = 0;

		constexpr int kMaxAttempts = 10;
		constexpr int kRetryDelay = 30;
	} // namespace

	WeightResult ApplyItemWeights(double multiplier)
	{
		WeightResult result;
		result.multiplier = multiplier;

		TArray<UClass*>* classes = FindItemClasses();
		if (classes == nullptr) return result;

		result.catalog_found = true;
		result.total = classes->Num();

		for (int i = 0; i < classes->Num(); ++i)
		{
			UClass* item_class = (*classes)[i];
			if (item_class == nullptr) continue;

			UObject* defaults = item_class->ClassDefaultObjectField();
			if (defaults == nullptr) continue;

			auto* item = static_cast<UPrimalItem*>(defaults);
			float& weight = item->BaseItemWeightField();

			// Le poids d'origine n'est releve qu'une fois : c'est lui, et non la
			// valeur courante, qui sert de base a chaque nouveau reglage
			const auto found = g_original.find(item_class);
			const float base = found != g_original.end()
				? found->second
				: (g_original[item_class] = weight);

			weight = static_cast<float>(base * multiplier);
			++result.touched;
		}

		g_applied = multiplier;
		return result;
	}

	void ScheduleStartupWeights()
	{
		const Config& config = GetConfig();
		if (!config.weight.enabled) return;

		const int delay = g_attempts == 0 ? config.weight.startup_delay : kRetryDelay;
		++g_attempts;

		API::Timer::Get().DelayExecute([]
		{
			const Config& current = GetConfig();
			if (!current.weight.enabled) return;

			const WeightResult result = ApplyItemWeights(current.weight.multiplier);

			if (result.catalog_found)
			{
				Log::GetLog()->info("Poids des objets : facteur {} applique a {} classe(s) sur {}",
					current.weight.multiplier, result.touched, result.total);
				return;
			}

			// Le monde n'est pas encore ouvert : on repasse plus tard plutot que
			// de renoncer, mais pas indefiniment — un catalogue introuvable au
			// bout de plusieurs minutes signale un probleme, pas une lenteur
			if (g_attempts < kMaxAttempts)
			{
				Log::GetLog()->info("Poids des objets : catalogue indisponible, nouvel essai dans {} s", kRetryDelay);
				ScheduleStartupWeights();
				return;
			}

			Log::GetLog()->error("Poids des objets : catalogue introuvable apres {} tentatives, abandon", kMaxAttempts);
		}, delay);
	}

	void RconWeight(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		nlohmann::json report{
			{"configured", GetConfig().weight.enabled},
			{"configuredMultiplier", GetConfig().weight.multiplier},
			{"appliedMultiplier", g_applied},
			{"knownClasses", static_cast<int>(g_original.size())},
		};

		const auto reply = [&]
		{
			FString response = ToFString(report.dump());
			connection->SendMessageW(packet->Id, 0, &response);
		};

		if (args.Num() < 2)
		{
			report["applied"] = false;
			return reply();
		}

		const double multiplier = std::atof(ToUtf8(args[1]).c_str());
		if (multiplier <= 0.0 || multiplier > 100.0)
		{
			report["applied"] = false;
			report["error"] = "Facteur attendu entre 0 (exclu) et 100";
			return reply();
		}

		const WeightResult result = ApplyItemWeights(multiplier);

		report["applied"] = result.catalog_found;
		report["appliedMultiplier"] = g_applied;
		report["total"] = result.total;
		report["touched"] = result.touched;
		report["knownClasses"] = static_cast<int>(g_original.size());

		if (!result.catalog_found)
		{
			report["error"] = "Catalogue introuvable : le monde n'est peut-etre pas encore ouvert";
		}

		reply();

		Log::GetLog()->info("Poids des objets : facteur {} applique a {} classe(s)",
			multiplier, result.touched);
	}
} // namespace QoL
