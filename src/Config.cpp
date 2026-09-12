#include "Config.h"

#include <fstream>

#include "json.hpp"
#include "Logger/Logger.h"
#include "Tools.h"

namespace QoL
{
	namespace
	{
		Config g_config;

		const std::map<std::string, std::string> kDefaultMessages = {
			{"NoPermission", "Cette commande est desactivee sur ce serveur."},
			{"PlayerDead", "Impossible : votre personnage est mort."},
			{"OnDino", "Impossible : descendez de votre monture d'abord."},
			{"Cooldown", "Patientez encore {0} avant de reutiliser cette commande."},
			{"Warmup", "Teleportation dans {0}. Ne bougez pas."},
			{"TeleportDone", "Teleportation effectuee."},
			{"TeleportFailed", "Teleportation impossible : {0}"},
			{"HomeSet", "Point '{0}' enregistre."},
			{"HomeDeleted", "Point '{0}' supprime."},
			{"HomeUnknown", "Aucun point nomme '{0}'. Tapez /homes pour la liste."},
			{"HomeLimit", "Vous avez atteint la limite de {0} points. Supprimez-en un avec /delhome."},
			{"HomeListEmpty", "Vous n'avez aucun point enregistre. Utilisez /sethome <nom>."},
			{"HomeList", "Vos points : {0}"},
			{"TpaSent", "Demande envoyee a {0}. Elle expire dans {1}."},
			{"TpaReceived", "{0} demande a se teleporter vers vous. /tpaccept ou /tpdeny."},
			{"TpaNoRequest", "Aucune demande de teleportation en attente."},
			{"TpaAccepted", "Demande acceptee."},
			{"TpaDenied", "Demande refusee."},
			{"TpaDeniedByTarget", "{0} a refuse votre demande."},
			{"TpaSelf", "Vous ne pouvez pas vous teleporter vers vous-meme."},
			{"PlayerNotFound", "Aucun joueur connecte ne correspond a '{0}'."},
			{"PlayerAmbiguous", "Plusieurs joueurs correspondent a '{0}'. Soyez plus precis."},
			{"KitUnknown", "Kit '{0}' inconnu. Tapez /kit pour la liste."},
			{"KitListEmpty", "Aucun kit disponible."},
			{"KitList", "Kits disponibles : {0}"},
			{"KitExhausted", "Vous avez deja utilise toutes vos utilisations du kit '{0}'."},
			{"KitGiven", "Kit '{0}' recu."},
			{"KitPartial", "Kit '{0}' recu, mais {1} objet(s) n'ont pas pu etre remis (inventaire plein ?)."},
			{"Usage", "Usage : {0}"},
		};

		Kit MakeStarterKit()
		{
			Kit kit;
			kit.name = "starter";
			kit.description = "Equipement de depart";
			kit.cooldown_seconds = 0;
			kit.max_uses = 1;
			kit.items = {
				{"Blueprint'/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponStonePick.PrimalItem_WeaponStonePick'", 1, 0.f, false},
				{"Blueprint'/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponStoneHatchet.PrimalItem_WeaponStoneHatchet'", 1, 0.f, false},
				{"Blueprint'/Game/PrimalEarth/CoreBlueprints/Weapons/PrimalItem_WeaponTorch.PrimalItem_WeaponTorch'", 1, 0.f, false},
				{"Blueprint'/Game/PrimalEarth/CoreBlueprints/Items/Consumables/PrimalItemConsumable_CookedMeat.PrimalItemConsumable_CookedMeat'", 20, 0.f, false},
			};
			return kit;
		}

		nlohmann::json KitToJson(const Kit& kit)
		{
			nlohmann::json items = nlohmann::json::array();
			for (const KitItem& item : kit.items)
			{
				items.push_back({
					{"Blueprint", item.blueprint},
					{"Amount", item.amount},
					{"Quality", item.quality},
					{"ForceBlueprint", item.force_blueprint},
				});
			}

			return {
				{"Name", kit.name},
				{"Description", kit.description},
				{"CooldownSeconds", kit.cooldown_seconds},
				{"MaxUses", kit.max_uses},
				{"Items", items},
			};
		}

		nlohmann::json BuildDefaultJson()
		{
			nlohmann::json messages = nlohmann::json::object();
			for (const auto& [key, value] : kDefaultMessages)
				messages[key] = value;

			return {
				{"General", {
					{"SenderName", g_config.sender_name},
					{"Prefix", g_config.prefix},
				}},
				{"Homes", {
					{"Enabled", true},
					{"MaxHomes", 3},
					{"CooldownSeconds", 300},
					{"WarmupSeconds", 10},
					{"AllowWhileRidingDino", false},
				}},
				{"Tpa", {
					{"Enabled", true},
					{"CooldownSeconds", 180},
					{"WarmupSeconds", 10},
					{"AllowWhileRidingDino", false},
					{"RequestTimeoutSeconds", 60},
				}},
				{"Kits", {
					{"Enabled", true},
					{"List", nlohmann::json::array({KitToJson(MakeStarterKit())})},
				}},
				{"Announce", {
					{"Enabled", true},
					{"DefaultSeconds", 60.0},
					{"MaxSeconds", 300.0},
					{"Scale", 1.3},
					{"Color", {{"R", 1.0}, {"G", 0.85}, {"B", 0.2}, {"A", 1.0}}},
				}},
				{"Window", {
					{"Enabled", true},
					{"BlueprintPath", ""},
					{"FunctionName", "ShowInventoryWindow"},
					{"Title", "Inventaires"},
					{"Radius", 30000.0},
				}},
				{"Weight", {
					{"Enabled", false},
					{"Multiplier", 1.0},
					{"StartupDelaySeconds", 30},
				}},
				{"WildLevels", {
					{"Enabled", false},
					{"MinLevel", 1},
					{"MaxLevel", 150},
					{"Bands", nlohmann::json::array()},
				}},
				{"Messages", messages},
			};
		}

		/**
		 * \brief Lit une section de config.json en isolant ses erreurs.
		 *
		 * Une valeur du mauvais type leve une exception nlohmann. Sans cette
		 * isolation, elle remonte jusqu'a Plugin_Init et AsaApi renonce a charger
		 * le plugin : une coquille dans un fichier edite a la main suffirait a
		 * priver le serveur de toutes ses commandes. Chaque section est donc
		 * independante, et celle qui echoue est nommee dans le journal.
		 */
		template <typename Fn>
		void ReadSection(const char* name, Fn&& read)
		{
			try
			{
				read();
			}
			catch (const std::exception& error)
			{
				Log::GetLog()->warn("Section '{}' de config.json ignoree ({}) - valeurs par defaut conservees",
					name, error.what());
			}
		}

		void ReadTeleportRules(const nlohmann::json& node, TeleportRules& rules)
		{
			rules.enabled = node.value("Enabled", rules.enabled);
			rules.cooldown_seconds = node.value("CooldownSeconds", rules.cooldown_seconds);
			rules.warmup_seconds = node.value("WarmupSeconds", rules.warmup_seconds);
			rules.allow_while_riding_dino = node.value("AllowWhileRidingDino", rules.allow_while_riding_dino);
		}
	} // namespace

	const std::string& Config::Msg(const std::string& key) const
	{
		const auto it = messages.find(key);
		if (it != messages.end())
			return it->second;

		return key;
	}

	std::string PluginFilePath(const std::string& filename)
	{
		return AsaApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/" + PROJECT_NAME + "/" + filename;
	}

	const Config& GetConfig()
	{
		return g_config;
	}

	bool LoadConfig()
	{
		g_config = Config{};
		g_config.messages = kDefaultMessages;

		const std::string path = PluginFilePath("config.json");

		std::ifstream file(path);
		if (!file.is_open())
		{
			// First run: materialise a documented default so admins have something to edit
			g_config.kits.push_back(MakeStarterKit());

			std::ofstream out(path);
			if (out.is_open())
			{
				out << BuildDefaultJson().dump(4);
				Log::GetLog()->info("Default config written to {}", path);
			}
			else
			{
				Log::GetLog()->warn("Could not create {} - running with built-in defaults", path);
			}

			return true;
		}

		nlohmann::json json;
		try
		{
			file >> json;
		}
		catch (const std::exception& error)
		{
			Log::GetLog()->error("config.json is invalid ({}) - running with built-in defaults", error.what());
			g_config.kits.push_back(MakeStarterKit());
			return false;
		}

		ReadSection("General", [&]
		{
			const auto general = json.value("General", nlohmann::json::object());
			g_config.sender_name = general.value("SenderName", g_config.sender_name);
			g_config.prefix = general.value("Prefix", g_config.prefix);
		});

		ReadSection("Homes", [&]
		{
			const auto homes = json.value("Homes", nlohmann::json::object());
			ReadTeleportRules(homes, g_config.homes);
			g_config.max_homes = homes.value("MaxHomes", g_config.max_homes);
		});

		ReadSection("Tpa", [&]
		{
			const auto tpa = json.value("Tpa", nlohmann::json::object());
			ReadTeleportRules(tpa, g_config.tpa);
			g_config.tpa_request_timeout_seconds =
				tpa.value("RequestTimeoutSeconds", g_config.tpa_request_timeout_seconds);
		});

		ReadSection("Kits", [&]
		{
			const auto kits = json.value("Kits", nlohmann::json::object());
			g_config.kits_enabled = kits.value("Enabled", g_config.kits_enabled);

			for (const auto& entry : kits.value("List", nlohmann::json::array()))
			{
				Kit kit;
				kit.name = entry.value("Name", std::string{});
				if (kit.name.empty())
				{
					Log::GetLog()->warn("Kit sans nom ignore");
					continue;
				}

				kit.description = entry.value("Description", std::string{});
				kit.cooldown_seconds = entry.value("CooldownSeconds", 0);
				kit.max_uses = entry.value("MaxUses", -1);

				for (const auto& item : entry.value("Items", nlohmann::json::array()))
				{
					KitItem kit_item;
					kit_item.blueprint = item.value("Blueprint", std::string{});
					if (kit_item.blueprint.empty())
					{
						Log::GetLog()->warn("Kit '{}' : objet sans Blueprint ignore", kit.name);
						continue;
					}

					kit_item.amount = item.value("Amount", 1);
					kit_item.quality = item.value("Quality", 0.f);
					kit_item.force_blueprint = item.value("ForceBlueprint", false);
					kit.items.push_back(kit_item);
				}

				g_config.kits.push_back(kit);
			}
		});

		ReadSection("Announce", [&]
		{
			const auto announce = json.value("Announce", nlohmann::json::object());
			g_config.announce.enabled = announce.value("Enabled", g_config.announce.enabled);
			g_config.announce.default_seconds = announce.value("DefaultSeconds", g_config.announce.default_seconds);
			g_config.announce.max_seconds = announce.value("MaxSeconds", g_config.announce.max_seconds);
			g_config.announce.scale = announce.value("Scale", g_config.announce.scale);

			const auto color = announce.value("Color", nlohmann::json::object());
			g_config.announce.color_r = color.value("R", g_config.announce.color_r);
			g_config.announce.color_g = color.value("G", g_config.announce.color_g);
			g_config.announce.color_b = color.value("B", g_config.announce.color_b);
			g_config.announce.color_a = color.value("A", g_config.announce.color_a);
		});

		ReadSection("Window", [&]
		{
			const auto window = json.value("Window", nlohmann::json::object());
			g_config.window.enabled = window.value("Enabled", g_config.window.enabled);
			g_config.window.blueprint_path = window.value("BlueprintPath", g_config.window.blueprint_path);
			g_config.window.function_name = window.value("FunctionName", g_config.window.function_name);
			g_config.window.title = window.value("Title", g_config.window.title);
			g_config.window.radius = window.value("Radius", g_config.window.radius);
		});

		ReadSection("Weight", [&]
		{
			const auto weight = json.value("Weight", nlohmann::json::object());
			g_config.weight.enabled = weight.value("Enabled", g_config.weight.enabled);
			g_config.weight.multiplier = weight.value("Multiplier", g_config.weight.multiplier);
			g_config.weight.startup_delay = weight.value("StartupDelaySeconds", g_config.weight.startup_delay);
		});

		ReadSection("WildLevels", [&]
		{
			const auto wild = json.value("WildLevels", nlohmann::json::object());
			g_config.wild_levels.enabled = wild.value("Enabled", g_config.wild_levels.enabled);
			g_config.wild_levels.min_level = wild.value("MinLevel", g_config.wild_levels.min_level);
			g_config.wild_levels.max_level = wild.value("MaxLevel", g_config.wild_levels.max_level);

			// Objet nomme avant parcours : voir la note de la section Messages
			const auto bands = wild.value("Bands", nlohmann::json::array());
			if (bands.is_array() && !bands.empty())
			{
				std::vector<WildLevelBand> parsed;
				for (const auto& entry : bands)
				{
					WildLevelBand band;
					band.from = entry.value("From", 1);
					band.to = entry.value("To", band.from + 9);
					band.percent = entry.value("Percent", 0.f);
					if (band.to < band.from) std::swap(band.from, band.to);
					if (band.percent > 0.f) parsed.push_back(band);
				}
				g_config.wild_levels.bands = std::move(parsed);
			}
		});

		ReadSection("Messages", [&]
		{
			// L'objet est nomme, et non issu directement de json.value(...) : items()
			// renvoie un proxy vers l'objet parcouru, et un temporaire serait detruit
			// avant la premiere iteration. La boucle lisait alors de la memoire liberee,
			// ce qui produisait des cles vides et l'exception qui empechait le plugin
			// de se charger.
			const auto messages = json.value("Messages", nlohmann::json::object());

			for (const auto& [key, value] : messages.items())
			{
				// Une entree non textuelle est ignoree plutot que de faire echouer la section
				if (value.is_string()) g_config.messages[key] = value.get<std::string>();
				else Log::GetLog()->warn("Message '{}' ignore : ce n'est pas une chaine", key);
			}
		});

		Log::GetLog()->info("Configuration chargee : {} kit(s), homes={}, tpa={}, annonces={}",
			g_config.kits.size(), g_config.homes.enabled, g_config.tpa.enabled, g_config.announce.enabled);

		return true;
	}
} // namespace QoL
