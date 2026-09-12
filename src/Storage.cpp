#include "Storage.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "json.hpp"
#include "Config.h"
#include "Logger/Logger.h"
#include "Util.h"

namespace QoL
{
	Storage& Storage::Get()
	{
		static Storage instance;
		return instance;
	}

	void Storage::Load()
	{
		players_.clear();
		dirty_ = false;

		const std::string path = PluginFilePath("data.json");
		std::ifstream file(path);
		if (!file.is_open())
			return; // No data yet: a fresh install, not an error

		nlohmann::json json;
		try
		{
			file >> json;
		}
		catch (const std::exception& error)
		{
			// On refuse de repartir d'un fichier a moitie lu : l'ecraser effacerait
			// les points de retour de tous les joueurs
			Log::GetLog()->error("data.json est corrompu ({}). Renommez-le pour repartir a zero.", error.what());
			return;
		}

		// Un fichier vide se lit comme un JSON nul, sur lequel value() leve une exception
		if (!json.is_object())
		{
			Log::GetLog()->warn("data.json ne contient pas d'objet : demarrage sans historique.");
			return;
		}

		// Les objets sont nommes avant d'etre parcourus : items() renvoie un proxy
		// vers l'objet, et un temporaire issu de value() serait detruit avant la
		// premiere iteration, faisant lire de la memoire liberee
		const auto players = json.value("Players", nlohmann::json::object());

		for (const auto& [eos_id, entry] : players.items())
		{
			if (!entry.is_object()) continue;
			PlayerRecord record;

			for (const auto& home : entry.value("Homes", nlohmann::json::array()))
			{
				HomePoint point;
				point.name = home.value("Name", std::string{});
				if (point.name.empty())
					continue;

				point.x = home.value("X", 0.0);
				point.y = home.value("Y", 0.0);
				point.z = home.value("Z", 0.0);
				record.homes.push_back(point);
			}

			const auto cooldowns = entry.value("Cooldowns", nlohmann::json::object());
			for (const auto& [key, value] : cooldowns.items())
			{
				if (value.is_number()) record.cooldowns[key] = value.get<int64_t>();
			}

			const auto kit_uses = entry.value("KitUses", nlohmann::json::object());
			for (const auto& [key, value] : kit_uses.items())
			{
				if (value.is_number()) record.kit_uses[key] = value.get<int>();
			}

			players_[eos_id] = std::move(record);
		}

		Log::GetLog()->info("Donnees chargees pour {} joueur(s)", players_.size());
	}

	void Storage::Save()
	{
		if (!dirty_)
			return;

		nlohmann::json players = nlohmann::json::object();
		for (const auto& [eos_id, record] : players_)
		{
			nlohmann::json homes = nlohmann::json::array();
			for (const HomePoint& home : record.homes)
				homes.push_back({{"Name", home.name}, {"X", home.x}, {"Y", home.y}, {"Z", home.z}});

			nlohmann::json cooldowns = nlohmann::json::object();
			for (const auto& [key, value] : record.cooldowns)
				cooldowns[key] = value;

			nlohmann::json kit_uses = nlohmann::json::object();
			for (const auto& [key, value] : record.kit_uses)
				kit_uses[key] = value;

			players[eos_id] = {{"Homes", homes}, {"Cooldowns", cooldowns}, {"KitUses", kit_uses}};
		}

		const std::string path = PluginFilePath("data.json");
		const std::string temp_path = path + ".tmp";

		// Write to a temp file first so a crash mid-write cannot truncate the live data
		{
			std::ofstream out(temp_path, std::ios::trunc);
			if (!out.is_open())
			{
				Log::GetLog()->error("Could not open {} for writing", temp_path);
				return;
			}

			out << nlohmann::json{{"Players", players}}.dump(2);
			if (!out.good())
			{
				Log::GetLog()->error("Failed while writing {}", temp_path);
				return;
			}
		}

		std::error_code ec;
		std::filesystem::rename(temp_path, path, ec);
		if (ec)
		{
			Log::GetLog()->error("Could not replace {} ({})", path, ec.message());
			return;
		}

		dirty_ = false;
	}

	PlayerRecord& Storage::Record(const std::string& eos_id)
	{
		return players_[eos_id];
	}

	const HomePoint* Storage::FindHome(const std::string& eos_id, const std::string& name) const
	{
		const auto player = players_.find(eos_id);
		if (player == players_.end())
			return nullptr;

		const auto& homes = player->second.homes;
		const auto it = std::find_if(homes.begin(), homes.end(),
			[&name](const HomePoint& home) { return home.name == name; });

		return it != homes.end() ? &*it : nullptr;
	}

	bool Storage::SetHome(const std::string& eos_id, const HomePoint& home, int max_homes)
	{
		PlayerRecord& record = players_[eos_id];

		const auto it = std::find_if(record.homes.begin(), record.homes.end(),
			[&home](const HomePoint& existing) { return existing.name == home.name; });

		if (it != record.homes.end())
		{
			*it = home; // Overwriting an existing point never counts against the limit
		}
		else
		{
			if (max_homes > 0 && static_cast<int>(record.homes.size()) >= max_homes)
				return false;

			record.homes.push_back(home);
		}

		dirty_ = true;
		return true;
	}

	bool Storage::DeleteHome(const std::string& eos_id, const std::string& name)
	{
		const auto player = players_.find(eos_id);
		if (player == players_.end())
			return false;

		auto& homes = player->second.homes;
		const auto it = std::find_if(homes.begin(), homes.end(),
			[&name](const HomePoint& home) { return home.name == name; });

		if (it == homes.end())
			return false;

		homes.erase(it);
		dirty_ = true;
		return true;
	}

	int64_t Storage::CooldownLeft(const std::string& eos_id, const std::string& key, int cooldown_seconds) const
	{
		if (cooldown_seconds <= 0)
			return 0;

		const auto player = players_.find(eos_id);
		if (player == players_.end())
			return 0;

		const auto stamp = player->second.cooldowns.find(key);
		if (stamp == player->second.cooldowns.end())
			return 0;

		const int64_t elapsed = Now() - stamp->second;
		if (elapsed < 0)
			return 0; // Clock moved backwards; treat the cooldown as expired rather than locking the player out

		return elapsed >= cooldown_seconds ? 0 : cooldown_seconds - elapsed;
	}

	void Storage::StampCooldown(const std::string& eos_id, const std::string& key)
	{
		players_[eos_id].cooldowns[key] = Now();
		dirty_ = true;
	}

	int Storage::KitUses(const std::string& eos_id, const std::string& kit_name) const
	{
		const auto player = players_.find(eos_id);
		if (player == players_.end())
			return 0;

		const auto uses = player->second.kit_uses.find(kit_name);
		return uses != player->second.kit_uses.end() ? uses->second : 0;
	}

	void Storage::RecordKitUse(const std::string& eos_id, const std::string& kit_name)
	{
		++players_[eos_id].kit_uses[kit_name];
		dirty_ = true;
	}
} // namespace QoL
