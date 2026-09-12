#pragma once

#include <map>
#include <string>
#include <vector>

#include "API/ARK/Ark.h"

namespace QoL
{
	struct HomePoint
	{
		std::string name;
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
	};

	/**
	 * \brief Per-player persisted state, keyed by EOS ID so it survives character remakes
	 */
	struct PlayerRecord
	{
		std::vector<HomePoint> homes;
		std::map<std::string, int64_t> cooldowns; // command key -> unix time of last use
		std::map<std::string, int> kit_uses;      // kit name -> times redeemed
	};

	class Storage
	{
	public:
		static Storage& Get();

		void Load();
		/**
		 * \brief Writes to disk only when something changed since the last save
		 */
		void Save();

		PlayerRecord& Record(const std::string& eos_id);

		const HomePoint* FindHome(const std::string& eos_id, const std::string& name) const;
		bool SetHome(const std::string& eos_id, const HomePoint& home, int max_homes);
		bool DeleteHome(const std::string& eos_id, const std::string& name);

		/**
		 * \brief Remaining cooldown in seconds, 0 when the command is ready
		 */
		int64_t CooldownLeft(const std::string& eos_id, const std::string& key, int cooldown_seconds) const;
		void StampCooldown(const std::string& eos_id, const std::string& key);

		int KitUses(const std::string& eos_id, const std::string& kit_name) const;
		void RecordKitUse(const std::string& eos_id, const std::string& kit_name);

		void MarkDirty() { dirty_ = true; }

	private:
		Storage() = default;

		std::map<std::string, PlayerRecord> players_;
		bool dirty_ = false;
	};
} // namespace QoL
