#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "API/ARK/Ark.h"
#include "Tools.h"

namespace QoL
{
	/**
	 * \brief Current unix time in seconds, used for every cooldown and expiry check
	 */
	inline int64_t Now()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}

	inline std::string ToUtf8(const FString& str)
	{
		return AsaApi::Tools::Utf8Encode(*str);
	}

	inline FString ToFString(const std::string& str)
	{
		return FString(AsaApi::Tools::Utf8Decode(str).c_str());
	}

	inline std::wstring ToWide(const std::string& str)
	{
		return AsaApi::Tools::Utf8Decode(str);
	}

	/**
	 * \brief Splits a raw chat message into its arguments. Index 0 is the command itself.
	 */
	inline TArray<FString> SplitArgs(const FString* message)
	{
		TArray<FString> parsed;
		message->ParseIntoArray(parsed, L" ", true);
		return parsed;
	}

	/**
	 * \brief Rejoins arguments from `from` onwards, so home names may contain spaces
	 */
	inline std::string JoinFrom(const TArray<FString>& args, int from)
	{
		std::string result;
		for (int i = from; i < args.Num(); ++i)
		{
			if (!result.empty())
				result += " ";
			result += ToUtf8(args[i]);
		}
		return result;
	}

	/**
	 * \brief Personnage incarne par un joueur, sans passer par GetPlayerCharacter().
	 *
	 * La version 93.15 du jeu a fait disparaitre du cache d'offsets d'AsaApi
	 * plusieurs fonctions dont nous dependions : GetPlayerCharacter(), IsDead()
	 * et GetRidingDino(). Les appeler leve une exception qu'aucun appelant ne
	 * rattrape, et le serveur tombe — c'est exactement ce qui s'est produit.
	 *
	 * Les champs correspondants, eux, sont toujours publies. Les lire est de
	 * surcroit plus durable : un decalage de champ survit aux versions bien
	 * mieux qu'un symbole de fonction, que le compilateur de Wildcard peut
	 * inliner d'une compilation a l'autre sans que rien ne le signale.
	 */
	inline AShooterCharacter* PlayerCharacter(AShooterPlayerController* player)
	{
		if (player == nullptr) return nullptr;
		return static_cast<AShooterCharacter*>(player->CharacterField().Get());
	}

	/** Un joueur sans personnage incarne est traite comme mort, comme le faisait le SDK */
	inline bool PlayerIsDead(AShooterPlayerController* player)
	{
		AShooterCharacter* character = PlayerCharacter(player);
		return character == nullptr || character->bIsDead()();
	}

	inline bool PlayerIsRiding(AShooterPlayerController* player)
	{
		AShooterCharacter* character = PlayerCharacter(player);
		return character != nullptr && character->RidingDinoField().Get() != nullptr;
	}

	/**
	 * \brief Sends a chat message without letting user-supplied text reach fmt as a format string
	 */
	void SendMsg(AShooterPlayerController* player, const std::string& text);

	/**
	 * \brief Formats a duration as "3m 20s" for cooldown messages
	 */
	std::string FormatDuration(int64_t seconds);

	/** Minuscules ASCII, pour des comparaisons insensibles a la casse */
	std::string Lowercase(std::string value);

	/**
	 * \brief Substitutes {0}, {1}, ... in an admin-authored message template.
	 * Deliberately not fmt: a stray brace in config.json must not throw at runtime.
	 */
	std::string Format(const std::string& templ, const std::vector<std::string>& args);

	inline std::string Format(const std::string& templ)
	{
		return Format(templ, {});
	}
} // namespace QoL
