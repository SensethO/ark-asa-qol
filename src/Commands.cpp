#include "Commands.h"

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "API/ARK/Ark.h"
#include "ICommands.h"
#include "Timer.h"
#include "Logger/Logger.h"

#include "Ceiling.h"
#include "Config.h"
#include "Inspect.h"
#include "Items.h"
#include "Link.h"
#include "Storage.h"
#include "Weights.h"
#include "WildLevels.h"
#include "Util.h"
#include "Window.h"

namespace QoL
{
	namespace
	{
		struct TpaRequest
		{
			std::string requester_eos;
			int64_t expires_at = 0;
		};

		// Keyed by the EOS ID of the player who must answer, so /tpaccept is a simple lookup
		std::map<std::string, TpaRequest> g_tpa_requests;

		// A player may only have one teleport in flight; a newer one invalidates the older token
		std::map<std::string, uint64_t> g_pending_warmups;
		uint64_t g_warmup_counter = 0;

		const std::vector<FString> kCommandNames = {
			"/help", "/sethome", "/home", "/delhome", "/homes",
			"/tpa", "/tpaccept", "/tpdeny", "/kit", "/players", "/pos", "/inv"
		};

		std::string EosOf(AShooterPlayerController* player)
		{
			return ToUtf8(AsaApi::IApiUtils::GetEOSIDFromController(player));
		}

		void Reply(AShooterPlayerController* player, const std::string& key,
			const std::vector<std::string>& args = {})
		{
			SendMsg(player, Format(GetConfig().Msg(key), args));
		}

		/**
		 * \brief Shared preconditions for anything that moves a player
		 */
		bool CanTeleport(AShooterPlayerController* player, const TeleportRules& rules)
		{
			if (PlayerIsDead(player))
			{
				Reply(player, "PlayerDead");
				return false;
			}

			if (!rules.allow_while_riding_dino && PlayerIsRiding(player))
			{
				Reply(player, "OnDino");
				return false;
			}

			return true;
		}

		bool CheckCooldown(AShooterPlayerController* player, const std::string& eos,
			const std::string& key, int cooldown_seconds)
		{
			const int64_t left = Storage::Get().CooldownLeft(eos, key, cooldown_seconds);
			if (left > 0)
			{
				Reply(player, "Cooldown", {FormatDuration(left)});
				return false;
			}

			return true;
		}

		void FinishTeleport(const std::string& eos, uint64_t token, FVector destination,
			const TeleportRules& rules, const std::string& cooldown_key)
		{
			const auto pending = g_pending_warmups.find(eos);
			if (pending == g_pending_warmups.end() || pending->second != token)
				return; // Superseded by a newer teleport, or cancelled

			g_pending_warmups.erase(pending);

			// The controller is re-resolved rather than captured: the player may have disconnected
			AShooterPlayerController* player = AsaApi::GetApiUtils().FindPlayerFromEOSID(ToFString(eos));
			if (player == nullptr)
				return;

			if (!CanTeleport(player, rules))
				return;

			if (!AsaApi::IApiUtils::TeleportToPos(player, destination))
			{
				Reply(player, "TeleportFailed", {"position invalide"});
				return;
			}

			Storage::Get().StampCooldown(eos, cooldown_key);
			Reply(player, "TeleportDone");
		}

		void StartTeleport(AShooterPlayerController* player, const std::string& eos, const FVector& destination,
			const TeleportRules& rules, const std::string& cooldown_key)
		{
			const uint64_t token = ++g_warmup_counter;
			g_pending_warmups[eos] = token;

			if (rules.warmup_seconds <= 0)
			{
				FinishTeleport(eos, token, destination, rules, cooldown_key);
				return;
			}

			Reply(player, "Warmup", {FormatDuration(rules.warmup_seconds)});

			API::Timer::Get().DelayExecute(
				[eos, token, destination, rules, cooldown_key]()
				{
					FinishTeleport(eos, token, destination, rules, cooldown_key);
				},
				rules.warmup_seconds);
		}

		void CmdSetHome(AShooterPlayerController* player, FString* message, int, int)
		{
			const Config& config = GetConfig();
			if (!config.homes.enabled)
			{
				Reply(player, "NoPermission");
				return;
			}

			if (PlayerIsDead(player))
			{
				Reply(player, "PlayerDead");
				return;
			}

			const TArray<FString> args = SplitArgs(message);
			const std::string name = args.Num() > 1 ? JoinFrom(args, 1) : "home";

			const FVector position = AsaApi::IApiUtils::GetPosition(player);
			if (position.IsNearlyZero())
			{
				Reply(player, "TeleportFailed", {"position invalide"});
				return;
			}

			HomePoint home;
			home.name = name;
			home.x = position.X;
			home.y = position.Y;
			home.z = position.Z;

			const std::string eos = EosOf(player);
			if (!Storage::Get().SetHome(eos, home, config.max_homes))
			{
				Reply(player, "HomeLimit", {std::to_string(config.max_homes)});
				return;
			}

			Reply(player, "HomeSet", {name});
		}

		void CmdHome(AShooterPlayerController* player, FString* message, int, int)
		{
			const Config& config = GetConfig();
			if (!config.homes.enabled)
			{
				Reply(player, "NoPermission");
				return;
			}

			const TArray<FString> args = SplitArgs(message);
			const std::string name = args.Num() > 1 ? JoinFrom(args, 1) : "home";
			const std::string eos = EosOf(player);

			const HomePoint* home = Storage::Get().FindHome(eos, name);
			if (home == nullptr)
			{
				Reply(player, "HomeUnknown", {name});
				return;
			}

			if (!CanTeleport(player, config.homes) || !CheckCooldown(player, eos, "home", config.homes.cooldown_seconds))
				return;

			StartTeleport(player, eos, FVector{home->x, home->y, home->z}, config.homes, "home");
		}

		void CmdDelHome(AShooterPlayerController* player, FString* message, int, int)
		{
			const TArray<FString> args = SplitArgs(message);
			if (args.Num() < 2)
			{
				Reply(player, "Usage", {"/delhome <nom>"});
				return;
			}

			const std::string name = JoinFrom(args, 1);
			if (!Storage::Get().DeleteHome(EosOf(player), name))
			{
				Reply(player, "HomeUnknown", {name});
				return;
			}

			Reply(player, "HomeDeleted", {name});
		}

		void CmdHomes(AShooterPlayerController* player, FString*, int, int)
		{
			const PlayerRecord& record = Storage::Get().Record(EosOf(player));
			if (record.homes.empty())
			{
				Reply(player, "HomeListEmpty");
				return;
			}

			std::string list;
			for (const HomePoint& home : record.homes)
			{
				if (!list.empty())
					list += ", ";
				list += home.name;
			}

			Reply(player, "HomeList", {list});
		}

		void CmdTpa(AShooterPlayerController* player, FString* message, int, int)
		{
			const Config& config = GetConfig();
			if (!config.tpa.enabled)
			{
				Reply(player, "NoPermission");
				return;
			}

			const TArray<FString> args = SplitArgs(message);
			if (args.Num() < 2)
			{
				Reply(player, "Usage", {"/tpa <joueur>"});
				return;
			}

			const std::string query = JoinFrom(args, 1);
			const TArray<AShooterPlayerController*> matches =
				AsaApi::GetApiUtils().FindPlayerFromCharacterName(ToFString(query), ESearchCase::IgnoreCase, false);

			if (matches.Num() == 0)
			{
				Reply(player, "PlayerNotFound", {query});
				return;
			}

			if (matches.Num() > 1)
			{
				Reply(player, "PlayerAmbiguous", {query});
				return;
			}

			AShooterPlayerController* target = matches[0];
			const std::string eos = EosOf(player);
			const std::string target_eos = EosOf(target);

			if (target_eos == eos)
			{
				Reply(player, "TpaSelf");
				return;
			}

			if (!CanTeleport(player, config.tpa) || !CheckCooldown(player, eos, "tpa", config.tpa.cooldown_seconds))
				return;

			g_tpa_requests[target_eos] = TpaRequest{eos, Now() + config.tpa_request_timeout_seconds};

			const std::string requester_name = ToUtf8(AsaApi::IApiUtils::GetCharacterName(player));
			const std::string target_name = ToUtf8(AsaApi::IApiUtils::GetCharacterName(target));

			Reply(player, "TpaSent", {target_name, FormatDuration(config.tpa_request_timeout_seconds)});
			Reply(target, "TpaReceived", {requester_name});
		}

		void CmdTpAccept(AShooterPlayerController* player, FString*, int, int)
		{
			const Config& config = GetConfig();
			const std::string eos = EosOf(player);

			const auto request = g_tpa_requests.find(eos);
			if (request == g_tpa_requests.end() || request->second.expires_at < Now())
			{
				if (request != g_tpa_requests.end())
					g_tpa_requests.erase(request);

				Reply(player, "TpaNoRequest");
				return;
			}

			const std::string requester_eos = request->second.requester_eos;
			g_tpa_requests.erase(request);

			AShooterPlayerController* requester = AsaApi::GetApiUtils().FindPlayerFromEOSID(ToFString(requester_eos));
			if (requester == nullptr)
			{
				Reply(player, "PlayerNotFound", {"?"});
				return;
			}

			Reply(player, "TpaAccepted");

			if (!CanTeleport(requester, config.tpa))
				return;

			// The destination is sampled when the warmup starts; TeleportToPlayer would re-check liveness
			const FVector destination = AsaApi::IApiUtils::GetPosition(player);
			if (destination.IsNearlyZero())
			{
				Reply(requester, "TeleportFailed", {"position invalide"});
				return;
			}

			StartTeleport(requester, requester_eos, destination, config.tpa, "tpa");
		}

		void CmdTpDeny(AShooterPlayerController* player, FString*, int, int)
		{
			const std::string eos = EosOf(player);

			const auto request = g_tpa_requests.find(eos);
			if (request == g_tpa_requests.end())
			{
				Reply(player, "TpaNoRequest");
				return;
			}

			AShooterPlayerController* requester =
				AsaApi::GetApiUtils().FindPlayerFromEOSID(ToFString(request->second.requester_eos));

			g_tpa_requests.erase(request);
			Reply(player, "TpaDenied");

			if (requester != nullptr)
				Reply(requester, "TpaDeniedByTarget", {ToUtf8(AsaApi::IApiUtils::GetCharacterName(player))});
		}

		void CmdKit(AShooterPlayerController* player, FString* message, int, int)
		{
			const Config& config = GetConfig();
			if (!config.kits_enabled || config.kits.empty())
			{
				Reply(player, "KitListEmpty");
				return;
			}

			const TArray<FString> args = SplitArgs(message);
			const std::string eos = EosOf(player);

			if (args.Num() < 2)
			{
				std::string list;
				for (const Kit& kit : config.kits)
				{
					if (!list.empty())
						list += ", ";

					list += kit.name;
					if (kit.max_uses > 0)
						list += " (" + std::to_string(kit.max_uses - Storage::Get().KitUses(eos, kit.name)) + " restant)";
				}

				Reply(player, "KitList", {list});
				return;
			}

			const std::string name = JoinFrom(args, 1);
			const Kit* kit = nullptr;
			for (const Kit& candidate : config.kits)
			{
				if (candidate.name == name)
				{
					kit = &candidate;
					break;
				}
			}

			if (kit == nullptr)
			{
				Reply(player, "KitUnknown", {name});
				return;
			}

			if (PlayerIsDead(player))
			{
				Reply(player, "PlayerDead");
				return;
			}

			if (kit->max_uses >= 0 && Storage::Get().KitUses(eos, kit->name) >= kit->max_uses)
			{
				Reply(player, "KitExhausted", {kit->name});
				return;
			}

			if (!CheckCooldown(player, eos, "kit." + kit->name, kit->cooldown_seconds))
				return;

			int failed = 0;
			for (const KitItem& item : kit->items)
			{
				FString blueprint = ToFString(item.blueprint);
				if (!player->GiveItem(&blueprint, item.amount, item.quality, item.force_blueprint, false, 0.f))
					++failed;
			}

			// The use is recorded even on partial delivery: re-running would duplicate whatever did land
			Storage::Get().RecordKitUse(eos, kit->name);
			Storage::Get().StampCooldown(eos, "kit." + kit->name);

			if (failed > 0)
			{
				Reply(player, "KitPartial", {kit->name, std::to_string(failed)});
				Log::GetLog()->warn("Kit '{}' had {} item(s) fail for {}", kit->name, failed, eos);
			}
			else
			{
				Reply(player, "KitGiven", {kit->name});
			}
		}

		void CmdPlayers(AShooterPlayerController* player, FString*, int, int)
		{
			std::string list;
			int count = 0;

			const auto& controllers = AsaApi::GetApiUtils().GetWorld()->PlayerControllerListField();
			for (TWeakObjectPtr<APlayerController> controller : controllers)
			{
				auto* shooter = static_cast<AShooterPlayerController*>(controller.Get());
				if (shooter == nullptr)
					continue;

				const std::string name = ToUtf8(AsaApi::IApiUtils::GetCharacterName(shooter));
				if (name.empty())
					continue;

				if (!list.empty())
					list += ", ";

				list += name;
				++count;
			}

			SendMsg(player, std::to_string(count) + " joueur(s) en ligne : " + list);
		}

		void CmdPos(AShooterPlayerController* player, FString*, int, int)
		{
			if (PlayerIsDead(player))
			{
				Reply(player, "PlayerDead");
				return;
			}

			const FVector position = AsaApi::IApiUtils::GetPosition(player);
			const AsaApi::MapCoords coords = AsaApi::GetApiUtils().FVectorToCoords(position);

			char buffer[64];
			snprintf(buffer, sizeof buffer, "Lat %.1f / Lon %.1f", coords.y, coords.x);
			SendMsg(player, buffer);
		}

		void CmdHelp(AShooterPlayerController* player, FString*, int, int)
		{
			const Config& config = GetConfig();

			SendMsg(player, "Commandes disponibles :");
			if (config.homes.enabled)
				SendMsg(player, "/sethome <nom> - /home <nom> - /homes - /delhome <nom>");
			if (config.tpa.enabled)
				SendMsg(player, "/tpa <joueur> - /tpaccept - /tpdeny");
			if (config.kits_enabled)
				SendMsg(player, "/kit [nom]");

			SendMsg(player, "/players - /pos - /help");
		}

		/**
		 * \brief Affiche un bandeau a l'ecran de tous les joueurs, pour la duree demandee.
		 *
		 * Expose uniquement en RCON, jamais en commande de chat : le RCON exige deja
		 * le mot de passe administrateur, alors qu'une commande de chat permettrait a
		 * n'importe quel joueur de couvrir l'ecran des autres.
		 *
		 * Syntaxe : qol.announce [duree_en_secondes] <message>
		 * La duree est facultative ; sans elle, celle de la configuration s'applique.
		 */
		void RconAnnounce(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
		{
			const Config& config = GetConfig();

			const auto reply = [connection, packet](const std::string& text)
			{
				FString response = ToFString(text);
				connection->SendMessageW(packet->Id, 0, &response);
			};

			if (!config.announce.enabled)
			{
				reply("Les annonces sont desactivees dans config.json.");
				return;
			}

			TArray<FString> args;
			packet->Body.ParseIntoArray(args, L" ", true);

			if (args.Num() < 2)
			{
				reply("Usage : qol.announce [duree_en_secondes] <message>");
				return;
			}

			float seconds = config.announce.default_seconds;
			int first_word = 1;

			// Un premier argument entierement numerique est lu comme une duree
			const std::string maybe_duration = ToUtf8(args[1]);
			if (!maybe_duration.empty() &&
				maybe_duration.find_first_not_of("0123456789.") == std::string::npos)
			{
				seconds = std::strtof(maybe_duration.c_str(), nullptr);
				first_word = 2;
			}

			const std::string message = JoinFrom(args, first_word);
			if (message.empty())
			{
				reply("Usage : qol.announce [duree_en_secondes] <message>");
				return;
			}

			// Une duree aberrante figerait un bandeau sur l'ecran des joueurs
			if (seconds <= 0.f) seconds = config.announce.default_seconds;
			if (seconds > config.announce.max_seconds) seconds = config.announce.max_seconds;

			const FLinearColor color{
				config.announce.color_r,
				config.announce.color_g,
				config.announce.color_b,
				config.announce.color_a,
			};

			const std::wstring wide = ToWide(message);
			AsaApi::GetApiUtils().SendNotificationToAll(color, config.announce.scale, seconds, nullptr, L"{}", wide.c_str());

			Log::GetLog()->info("Annonce diffusee ({:.0f} s) : {}", seconds, message);
			reply("Annonce diffusee pendant " + std::to_string(static_cast<int>(seconds)) + " s.");
		}

		/**
		 * \brief Runs once a second: drops expired TPA requests and flushes pending writes
		 */
		void OnTimer()
		{
			const int64_t now = Now();
			for (auto it = g_tpa_requests.begin(); it != g_tpa_requests.end();)
				it = it->second.expires_at < now ? g_tpa_requests.erase(it) : std::next(it);

			// Un joueur qui vient de se connecter n'a pas encore de relais
			TickLinks();

			// La barriere du monde detruit sans deposer de sac : mieux vaut
			// redescendre le joueur que lui rendre son inventaire ensuite.
			EnforceCeiling();

			static int64_t last_save = 0;
			if (now - last_save >= 30)
			{
				last_save = now;
				Storage::Get().Save();
			}
		}
		/**
		 * rief Enveloppe une commande pour qu'une exception n'abatte pas le serveur.
		 *
		 * AsaApi resout les symboles du jeu par un cache d'offsets telecharge, et
		 * `GetAddress` leve quand un symbole a disparu de la version courante. Ni
		 * le repartiteur de commandes ni le crochet RCON ne rattrapent : la version
		 * 93.15 a ainsi transforme une simple lecture de la liste des joueurs en
		 * arret brutal du serveur. Une commande qui echoue doit rester une commande
		 * qui echoue.
		 */
		template <typename F>
		auto GuardRcon(const char* name, F handler)
		{
			return [name, handler](RCONClientConnection* connection, RCONPacket* packet, UWorld* world)
			{
				try
				{
					handler(connection, packet, world);
				}
				catch (const std::exception& error)
				{
					Log::GetLog()->error("{} a echoue : {}", name, error.what());
					FString reponse = ToFString(std::string("{\"error\":\"commande indisponible sur cette version du serveur\"}"));
					connection->SendMessageW(packet->Id, 0, &reponse);
				}
				catch (...)
				{
					Log::GetLog()->error("{} a echoue (exception inconnue)", name);
				}
			};
		}

		template <typename F>
		auto GuardChat(const char* name, F handler)
		{
			return [name, handler](AShooterPlayerController* player, FString* message, int a, int b)
			{
				try
				{
					handler(player, message, a, b);
				}
				catch (const std::exception& error)
				{
					Log::GetLog()->error("{} a echoue : {}", name, error.what());
					SendMsg(player, "Commande indisponible pour le moment.");
				}
				catch (...)
				{
					Log::GetLog()->error("{} a echoue (exception inconnue)", name);
				}
			};
		}
	} // namespace

	void RegisterCommands()
	{
		AsaApi::ICommands& commands = AsaApi::GetCommands();

		commands.AddChatCommand("/help", GuardChat("/help", &CmdHelp));
		commands.AddChatCommand("/sethome", GuardChat("/sethome", &CmdSetHome));
		commands.AddChatCommand("/home", GuardChat("/home", &CmdHome));
		commands.AddChatCommand("/delhome", GuardChat("/delhome", &CmdDelHome));
		commands.AddChatCommand("/homes", GuardChat("/homes", &CmdHomes));
		commands.AddChatCommand("/tpa", GuardChat("/tpa", &CmdTpa));
		commands.AddChatCommand("/tpaccept", GuardChat("/tpaccept", &CmdTpAccept));
		commands.AddChatCommand("/tpdeny", GuardChat("/tpdeny", &CmdTpDeny));
		commands.AddChatCommand("/kit", GuardChat("/kit", &CmdKit));
		commands.AddChatCommand("/inv", GuardChat("/inv", &CmdInventoryWindow));
		commands.AddChatCommand("/players", GuardChat("/players", &CmdPlayers));
		commands.AddChatCommand("/pos", GuardChat("/pos", &CmdPos));

		commands.AddRconCommand("qol.announce", GuardRcon("qol.announce", &RconAnnounce));
		commands.AddRconCommand("qol.players", GuardRcon("qol.players", &RconPlayers));
		commands.AddRconCommand("qol.inventory", GuardRcon("qol.inventory", &RconInventory));
		commands.AddRconCommand("qol.structures", GuardRcon("qol.structures", &RconStructures));
		commands.AddRconCommand("qol.containers", GuardRcon("qol.containers", &RconContainers));
		commands.AddRconCommand("qol.dinos", GuardRcon("qol.dinos", &RconDinos));
		commands.AddRconCommand("qol.deathcache", GuardRcon("qol.deathcache", &RconDeathCache));
		commands.AddRconCommand("qol.wildlevels", GuardRcon("qol.wildlevels", &RconWildLevels));
		commands.AddRconCommand("qol.windowtest", GuardRcon("qol.windowtest", &RconWindowTest));
		commands.AddRconCommand("qol.items", GuardRcon("qol.items", &RconItems));
		commands.AddRconCommand("qol.give", GuardRcon("qol.give", &RconGive));
		commands.AddRconCommand("qol.creative", GuardRcon("qol.creative", &RconCreative));
		commands.AddRconCommand("qol.gamemode", GuardRcon("qol.gamemode", &RconGameMode));
		commands.AddRconCommand("qol.weight", GuardRcon("qol.weight", &RconWeight));

		commands.AddOnTimerCallback("QoL.Tick", &OnTimer);
	}

	void UnregisterCommands()
	{
		AsaApi::ICommands& commands = AsaApi::GetCommands();

		for (const FString& name : kCommandNames)
			commands.RemoveChatCommand(name);

		commands.RemoveRconCommand("qol.announce");
		commands.RemoveRconCommand("qol.players");
		commands.RemoveRconCommand("qol.inventory");
		commands.RemoveRconCommand("qol.structures");
		commands.RemoveRconCommand("qol.containers");
		commands.RemoveRconCommand("qol.dinos");
		commands.RemoveRconCommand("qol.deathcache");
		commands.RemoveRconCommand("qol.wildlevels");
		commands.RemoveRconCommand("qol.windowtest");
		commands.RemoveRconCommand("qol.items");
		commands.RemoveRconCommand("qol.give");
		commands.RemoveRconCommand("qol.creative");
		commands.RemoveRconCommand("qol.gamemode");
		commands.RemoveRconCommand("qol.weight");
		commands.RemoveOnTimerCallback("QoL.Tick");

		API::Timer::Get().UnloadAllTimers();

		g_tpa_requests.clear();
		g_pending_warmups.clear();
	}
} // namespace QoL
