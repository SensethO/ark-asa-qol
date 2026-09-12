#include "Items.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "json.hpp"
#include "Logger/Logger.h"

#include "Util.h"

namespace QoL
{
	namespace
	{
		/**
		 * \brief Chemins possibles de l'objet de donnees du jeu.
		 *
		 * AsaApi n'expose aucun acces a `UPrimalGlobals`, l'instance vivante n'est
		 * donc pas atteignable. On passe par l'objet par defaut de la classe, qui
		 * porte les memes listes : elles sont figees a la compilation du blueprint.
		 */
		const char* kGameDataPaths[] = {
			"Blueprint'/Game/PrimalEarth/CoreBlueprints/PrimalGameData_BP.PrimalGameData_BP'",
			"Blueprint'/Game/PrimalEarth/CoreBlueprints/TestGameData_BP.TestGameData_BP'",
		};

		struct Catalog
		{
			std::string source;
			TArray<UClass*>* items = nullptr;
		};

		/** Lit le MasterItemList porte par l'objet par defaut d'une classe */
		TArray<UClass*>* ReadMasterList(UClass* game_data_class)
		{
			if (game_data_class == nullptr) return nullptr;

			UObject* defaults = game_data_class->ClassDefaultObjectField();
			if (defaults == nullptr) return nullptr;

			// Le champ n'est pas declare dans les en-tetes d'AsaApi : il est lu par
			// son nom, comme le fait la bibliotheque elle-meme. TSubclassOf a la
			// meme disposition memoire qu'un UClass*.
			auto* list = GetNativePointerField<TArray<UClass*>*>(defaults, "UPrimalGameData.MasterItemList");
			if (list == nullptr || list->Num() == 0) return nullptr;

			return list;
		}

		/**
		 * rief Retrouve le catalogue reellement en vigueur.
		 *
		 * Un mod peut remplacer les donnees de jeu : le journal l'annonce alors par
		 * « Using mod asset Package ... as PrimalGameDataOverride ». Lire le
		 * `PrimalGameData` de base ignorerait ses objets. La surcharge declaree par
		 * les reglages du monde est donc consultee en premier.
		 */
		/**
		 * rief Reunit toutes les sources d'objets des donnees de jeu.
		 *
		 * `MasterItemList` ne suffit pas : elle ne portait que 654 entrees, sans
		 * les fleches tranquillisantes ni les pieces d'ascenseur, pourtant du jeu
		 * de base. L'essentiel des objets fabricables n'y figure pas — ils sont
		 * declares comme engrammes, et les mods ajoutent les leurs dans un champ
		 * encore separe. Les trois listes sont donc fusionnees, doublons ecartes.
		 *
		 * Chaque lecture est protegee : un champ absent d'une version doit faire
		 * perdre une source, pas le catalogue entier.
		 */
		/** Mis a vrai par `reload=1` : le prochain appel rebatit le catalogue */
		bool g_catalog_stale = false;

		TArray<UClass*>* MergedCatalog(UObject* game_data)
		{
			static TArray<UClass*> merged;
			static bool built = false;

			if (g_catalog_stale)
			{
				built = false;
				g_catalog_stale = false;
			}

			if (built && merged.Num() > 0) return &merged;
			if (game_data == nullptr) return nullptr;

			merged.Reset();
			std::unordered_set<UClass*> seen;

			const char* sources[] = {
				"UPrimalGameData.MasterItemList",
			};

			for (const char* field : sources)
			{
				try
				{
					auto* list = GetNativePointerField<TArray<UClass*>*>(game_data, field);
					if (list == nullptr) continue;

					for (int i = 0; i < list->Num(); ++i)
					{
						UClass* cls = (*list)[i];
						if (cls != nullptr && seen.insert(cls).second) merged.Add(cls);
					}
				}
				catch (...)
				{
					Log::GetLog()->warn("Source d'objets illisible : {}", field);
				}
			}

			// `MasterItemList` ne porte que les objets que les donnees de jeu
			// declarent explicitement — 654 entrees, sans les fleches
			// tranquillisantes ni les pieces d'ascenseur.
			//
			// Deux tentatives d'elargissement ont echoue, chacune en abattant le
			// serveur, et chacune pour une raison differente :
			//
			//  - lire `EngramBlueprintClasses` comme un tableau de `UClass*` :
			//    sa disposition memoire n'est pas celle-la, les pointeurs lus
			//    sont invalides ;
			//  - appeler `UVictoryCore::GetAllClassesOfType` : elle s'appuie sur
			//    le registre d'assets, qui n'est pas initialise sur un serveur
			//    dedie, et l'appel part dans le vide.
			//
			// Reste `GetDerivedClasses`, du moteur : elle parcourt la hierarchie
			// de classes deja chargee, sans rien demander au registre d'assets.
			//
			// Chaque etape est journalisee avant d'etre tentee. Si le serveur
			// tombe malgre tout, la derniere ligne ecrite designera l'appel
			// fautif — ce qui a manque aux deux tentatives precedentes, ou il a
			// fallu deduire la cause d'une pile sans reperes.
			try
			{
				Log::GetLog()->info("Catalogue : recherche de la classe de base");
				UClass* base = UPrimalItem::StaticClass();

				if (base == nullptr)
				{
					Log::GetLog()->warn("Catalogue : classe de base introuvable");
				}
				else
				{
					Log::GetLog()->info("Catalogue : enumeration des classes derivees");

					TArray<UClass*> derived;
					NativeCall<void, const UClass*, TArray<UClass*>*, bool>(
						nullptr,
						"Global.GetDerivedClasses(UClass*,TArray<UClass*,TSizedDefaultAllocator<32>>&,bool)",
						base, &derived, true);

					Log::GetLog()->info("Catalogue : {} classes derivees trouvees", derived.Num());

					for (int i = 0; i < derived.Num(); ++i)
					{
						UClass* cls = derived[i];
						if (cls != nullptr && seen.insert(cls).second) merged.Add(cls);
					}
				}
			}
			catch (const std::exception& error)
			{
				Log::GetLog()->warn("Catalogue : enumeration indisponible ({})", error.what());
			}
			catch (...)
			{
				Log::GetLog()->warn("Catalogue : enumeration indisponible");
			}

			built = merged.Num() > 0;
			return built ? &merged : nullptr;
		}

		Catalog FindCatalog()
		{
			Catalog catalog;

			// Les donnees effectives du serveur : celles que les mods remplacent
			// quand ils en fournissent. Charger `PrimalGameData_BP` par son chemin
			// donnait la liste du jeu nu — 654 objets, sans les ajouts des mods, et
			// sans meme certains objets de base qui n'y figurent que par extension.
			if (UPrimalGameData* live = AsaApi::GetApiUtils().GetGameData())
			{
				if (TArray<UClass*>* list = MergedCatalog(static_cast<UObject*>(live)))
				{
					catalog.source = ToUtf8(AsaApi::IApiUtils::GetBlueprint(static_cast<UObject*>(live)));
					if (catalog.source.empty()) catalog.source = "UPrimalGlobals.PrimalGameData";
					catalog.items = list;
					return catalog;
				}
			}

			if (UWorld* world = AsaApi::GetApiUtils().GetWorld())
			{
				if (auto* settings = static_cast<APrimalWorldSettings*>(world->GetWorldSettings(false, false)))
				{
					UClass* overriden = settings->PrimalGameDataOverrideField().uClass;
					if (TArray<UClass*>* list = ReadMasterList(overriden))
					{
						catalog.source = ToUtf8(AsaApi::IApiUtils::GetClassBlueprint(overriden));
						catalog.items = list;
						return catalog;
					}
				}
			}

			for (const char* path : kGameDataPaths)
			{
				if (TArray<UClass*>* list = ReadMasterList(UVictoryCore::BPLoadClass(ToFString(path))))
				{
					catalog.source = path;
					catalog.items = list;
					return catalog;
				}
			}

			return catalog;
		}

	} // namespace

	TArray<UClass*>* FindItemClasses(std::string* source)
	{
		const Catalog catalog = FindCatalog();
		if (source != nullptr) *source = catalog.source;
		return catalog.items;
	}

	namespace
	{
		struct ItemInfo
		{
			std::string name;
			/** EPrimalItemType tel que declare par le jeu ; -1 si illisible */
			int type = -1;
		};

		/** Nom et type de l'objet, lus sur son objet par defaut */
		ItemInfo Describe(UClass* item_class)
		{
			ItemInfo info;
			if (item_class == nullptr) return info;

			UObject* defaults = item_class->ClassDefaultObjectField();
			if (defaults == nullptr) return info;

			// Garde-fou : le defaut de classe d'un objet valide se reclame de sa
			// propre classe. Un pointeur lu de travers echoue ici plutot que
			// quelques lignes plus bas, au premier acces a un champ.
			if (defaults->ClassField() != item_class) return info;

			auto* item = static_cast<UPrimalItem*>(defaults);
			info.name = ToUtf8(item->DescriptiveNameBaseField());
			info.type = static_cast<int>(item->MyItemTypeField().GetValue());
			return info;
		}

		void Reply(RCONClientConnection* connection, RCONPacket* packet, const nlohmann::json& payload)
		{
			FString response = ToFString(payload.dump());
			connection->SendMessageW(packet->Id, 0, &response);
		}
	} // namespace

	void RconItems(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		std::string search;
		int offset = 0;
		int limit = 100;

		for (int i = 1; i < args.Num(); ++i)
		{
			const std::string arg = ToUtf8(args[i]);
			const size_t equals = arg.find('=');
			if (equals == std::string::npos) continue;

			const std::string key = Lowercase(arg.substr(0, equals));
			const std::string value = arg.substr(equals + 1);

			if (key == "reload") { if (value == "1") g_catalog_stale = true; }
			else if (key == "search") search = Lowercase(value);
			else if (key == "offset") offset = (std::max)(0, std::atoi(value.c_str()));
			else if (key == "limit") limit = (std::min)(300, (std::max)(1, std::atoi(value.c_str())));
		}

		const Catalog catalog = FindCatalog();
		if (catalog.items == nullptr)
		{
			Reply(connection, packet, nlohmann::json{
				{"error", "Catalogue introuvable : MasterItemList n'a pu etre lu sur aucun chemin connu"},
				{"total", 0},
				{"items", nlohmann::json::array()},
			});
			return;
		}

		nlohmann::json items = nlohmann::json::array();
		int matched = 0;
		bool truncated = false;

		for (int i = 0; i < catalog.items->Num(); ++i)
		{
			UClass* item_class = (*catalog.items)[i];
			if (item_class == nullptr) continue;

			const std::string path = ToUtf8(AsaApi::IApiUtils::GetClassBlueprint(item_class));
			if (path.empty()) continue;

			const ItemInfo info = Describe(item_class);

			// La recherche porte sur le nom affiche et sur le chemin : l'un est
			// lisible, l'autre est ce que tapent les administrateurs habitues
			if (!search.empty()
				&& Lowercase(info.name).find(search) == std::string::npos
				&& Lowercase(path).find(search) == std::string::npos)
				continue;

			if (matched++ < offset) continue;
			if (static_cast<int>(items.size()) >= limit)
			{
				truncated = true;
				continue;
			}

			// Le type vient du jeu : c'est sa propre classification, plus fiable
			// qu'une deduction sur le nom de classe
			items.push_back({{"i", i}, {"n", info.name}, {"p", path}, {"t", info.type}});
		}

		Reply(connection, packet, nlohmann::json{
			{"source", catalog.source},
			{"total", catalog.items->Num()},
			{"matched", matched},
			{"offset", offset},
			{"returned", items.size()},
			{"truncated", truncated},
			{"items", items},
		});

		Log::GetLog()->info("Catalogue d'objets : {} entrees, {} correspondances", catalog.items->Num(), matched);
	}

	void RconGive(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		if (args.Num() < 3)
		{
			Reply(connection, packet, nlohmann::json{
				{"error", "Usage : qol.give <eosId> <blueprint> [quantite] [qualite] [bp]"}});
			return;
		}

		const std::string eos = ToUtf8(args[1]);
		FString blueprint = args[2];

		const int quantity = args.Num() > 3 ? (std::max)(1, std::atoi(ToUtf8(args[3]).c_str())) : 1;
		const float quality = args.Num() > 4 ? static_cast<float>(std::atof(ToUtf8(args[4]).c_str())) : 0.f;
		const bool force_blueprint = args.Num() > 5 && ToUtf8(args[5]) == "1";

		AShooterPlayerController* player = AsaApi::GetApiUtils().FindPlayerFromEOSID(ToFString(eos));
		if (player == nullptr)
		{
			Reply(connection, packet, nlohmann::json{
				{"error", "Joueur non connecte : un objet ne peut etre remis qu'a un personnage present"}});
			return;
		}

		if (PlayerIsDead(player))
		{
			Reply(connection, packet, nlohmann::json{{"error", "Le personnage est mort"}});
			return;
		}

		// Meme appel que les kits, deja eprouve en jeu. Un chemin invalide renvoie
		// false sans rien casser.
		const bool given = player->GiveItem(&blueprint, quantity, quality, force_blueprint, false, 0.f);

		nlohmann::json reply{
			{"given", given},
			{"eosId", eos},
			{"blueprint", ToUtf8(blueprint)},
			{"quantity", quantity},
			{"quality", quality},
			{"blueprintItem", force_blueprint},
		};

		// La cle n'est posee qu'en cas d'echec reel : une cle `error` vide sur une
		// reussite a longtemps fait passer les remises abouties pour des erreurs.
		if (!given) reply["error"] = "Remise refusee : chemin invalide ou inventaire plein";

		Reply(connection, packet, reply);

		Log::GetLog()->info("Remise d'objet a {} : {} x{} -> {}",
			ToUtf8(AsaApi::IApiUtils::GetCharacterName(player)), ToUtf8(blueprint), quantity, given);
	}
} // namespace QoL
