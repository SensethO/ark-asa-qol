#include "Inspect.h"

#include <map>
#include <string>
#include <vector>

#include "json.hpp"
#include "Logger/Logger.h"

#include "Util.h"

namespace QoL
{
	namespace
	{
		/** Plafond de structures renvoyees : au-dela, la reponse RCON devient ingerable */
		constexpr int kMaxStructures = 400;
		/** Rayon par defaut en unites Unreal (1 unite ~ 1 cm) */
		constexpr float kDefaultRadius = 30000.f;

		void Reply(RCONClientConnection* connection, RCONPacket* packet, const nlohmann::json& payload)
		{
			FString response = ToFString(payload.dump());
			connection->SendMessageW(packet->Id, 0, &response);
		}

		void ReplyError(RCONClientConnection* connection, RCONPacket* packet, const std::string& message)
		{
			Reply(connection, packet, nlohmann::json{{"error", message}});
		}

	} // namespace

	/**
	 * \brief Position absolue d'un acteur dans le monde.
	 *
	 * `RelativeLocation` ne convient pas : elle est exprimee par rapport au
	 * parent. Un acteur libre — joueur, creature — n'en a pas, les deux valeurs
	 * coincident donc et l'erreur passait inapercue. Mais une construction
	 * accrochee a une fondation renvoyait des coordonnees locales de l'ordre de
	 * la centaine d'unites, placant toute une base au centre de la carte.
	 */
	FVector ActorPosition(AActor* actor)
	{
		if (actor == nullptr) return FVector{0, 0, 0};

		USceneComponent* root = actor->RootComponentField();
		if (root == nullptr) return FVector{0, 0, 0};

		return root->ComponentToWorldField().GetTranslation();
	}

	/**
	 * \brief Reduit un chemin de blueprint a un nom lisible.
	 * "Blueprint'/Game/.../PrimalItemStructure_StoneWall.PrimalItemStructure_StoneWall'"
	 * devient "PrimalItemStructure_StoneWall".
	 */
	std::string ShortName(const std::string& blueprint)
	{
		const size_t dot = blueprint.rfind('.');
		if (dot == std::string::npos) return blueprint;

		std::string name = blueprint.substr(dot + 1);
		while (!name.empty() && (name.back() == '\'' || name.back() == '"')) name.pop_back();
		if (name.size() > 2 && name.substr(name.size() - 2) == "_C") name.resize(name.size() - 2);

		return name;
	}

	/**
	 * \brief Categorise un objet d'inventaire.
	 *
	 * L'inventaire d'ARK contient aussi les engrammes appris et les skins et
	 * costumes possedes. Les costumes echappent a bIsItemSkin et sont classes en
	 * equipement : seul le prefixe de classe les identifie sans dependre de la
	 * langue du serveur.
	 */
	ItemLine ClassifyItem(UPrimalItem* item)
	{
		ItemLine line;
		if (item == nullptr) return line;

		FString name;
		item->GetItemName(&name, false, false, nullptr);

		line.name = ToUtf8(name);
		line.item_class = ShortName(ToUtf8(AsaApi::IApiUtils::GetItemBlueprint(item)));
		line.quantity = item->GetItemQuantity();

		const int item_type = static_cast<int>(item->MyItemTypeField().GetValue());

		line.engram = item->bIsEngram()() || line.name.rfind("Engram", 0) == 0;
		line.skin = item->bIsItemSkin()()
			|| item_type == EPrimalItemType::Skin
			|| line.item_class.rfind("PrimalItemSkin_", 0) == 0
			|| line.item_class.rfind("PrimalItemCostume_", 0) == 0;

		return line;
	}

	/**
	 * \brief Determine si une classe descend d'APrimalStructureItemContainer.
	 *
	 * `IsA(APrimalStructureItemContainer::StaticClass())` ne fonctionne pas :
	 * cette classe n'expose pas GetPrivateStaticClass, et l'appel natif a
	 * StaticClass ne se resout pas — le test ne reconnaissait aucun coffre.
	 * La hierarchie est donc remontee et comparee par nom, ce qui ne depend
	 * d'aucun symbole. Le resultat est mis en cache par classe.
	 */
	bool IsContainerClass(UClass* cls)
	{
		static std::map<UClass*, bool> cache;

		if (cls == nullptr) return false;

		const auto known = cache.find(cls);
		if (known != cache.end()) return known->second;

		bool found = false;
		for (UStruct* current = cls; current != nullptr; current = current->SuperStructField())
		{
			const std::string path = ToUtf8(AsaApi::IApiUtils::GetClassBlueprint(static_cast<UClass*>(current)));
			if (path.find("StructureItemContainer") != std::string::npos)
			{
				found = true;
				break;
			}
		}

		cache[cls] = found;
		return found;
	}

	namespace
	{
		/** Ajoute position brute et coordonnees de carte a un objet JSON */
		void AddPosition(nlohmann::json& node, const FVector& position)
		{
			node["x"] = position.X;
			node["y"] = position.Y;
			node["z"] = position.Z;

			const AsaApi::MapCoords coords = AsaApi::GetApiUtils().FVectorToCoords(position);
			node["lat"] = coords.y;
			node["lon"] = coords.x;
		}

		AShooterPlayerController* FindByEos(const std::string& eos_id)
		{
			return AsaApi::GetApiUtils().FindPlayerFromEOSID(ToFString(eos_id));
		}

		/** Serialise un objet deja categorise */
		nlohmann::json DescribeItem(UPrimalItem* item, bool& is_engram, bool& is_skin)
		{
			const ItemLine line = ClassifyItem(item);
			is_engram = line.engram;
			is_skin = line.skin;

			nlohmann::json entry;
			entry["name"] = line.name;
			entry["quantity"] = line.quantity;
			entry["blueprint"] = line.item_class;
			entry["engram"] = line.engram;
			entry["skin"] = line.skin;
			entry["type"] = static_cast<int>(item->MyItemTypeField().GetValue());
			entry["isBlueprint"] = item->bIsBlueprint()();
			entry["quality"] = static_cast<int>(item->ItemQualityIndexField());

			return entry;
		}

		/** Plafonds evitant une reponse RCON demesuree sur une base developpee */
		/** Mesure : 200 entrees passent en 1,3 s, 600 depassent le delai du RCON */
		constexpr int kMaxDinos = 200;
		constexpr int kMaxContainers = 60;
		constexpr int kMaxItemsPerContainer = 80;

		/**
		 * \brief Contenu d'un inventaire quelconque : coffre, structure ou monture.
		 * Les entrees cosmetiques sont ecartees : elles n'existent que sur les joueurs.
		 */
		nlohmann::json DescribeInventory(UPrimalInventoryComponent* inventory, int& total)
		{
			nlohmann::json items = nlohmann::json::array();
			total = 0;

			if (inventory == nullptr) return items;

			for (UPrimalItem* item : inventory->InventoryItemsField())
			{
				if (item == nullptr) continue;

				bool is_engram = false;
				bool is_skin = false;
				nlohmann::json entry = DescribeItem(item, is_engram, is_skin);
				if (is_engram) continue;

				total++;
				if (items.size() < kMaxItemsPerContainer) items.push_back(entry);
			}

			return items;
		}
	} // namespace

	void RconPlayers(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		nlohmann::json players = nlohmann::json::array();

		const auto& controllers = AsaApi::GetApiUtils().GetWorld()->PlayerControllerListField();
		int index = 0;

		for (TWeakObjectPtr<APlayerController> controller : controllers)
		{
			auto* pc = static_cast<AShooterPlayerController*>(controller.Get());
			if (pc == nullptr) continue;

			nlohmann::json entry;
			entry["index"] = index++;
			entry["name"] = ToUtf8(AsaApi::IApiUtils::GetCharacterName(pc));
			entry["platformName"] = ToUtf8(AsaApi::IApiUtils::GetSteamName(pc));
			entry["eosId"] = ToUtf8(AsaApi::IApiUtils::GetEOSIDFromController(pc));
			entry["playerId"] = static_cast<uint64_t>(AsaApi::IApiUtils::GetPlayerID(pc));
			entry["tribeId"] = AsaApi::IApiUtils::GetTribeID(pc);
			entry["dead"] = PlayerIsDead(pc);
			entry["riding"] = PlayerIsRiding(pc);

			// La position n'a de sens que pour un personnage vivant et incarne
			if (!PlayerIsDead(pc))
			{
				AddPosition(entry, AsaApi::IApiUtils::GetPosition(pc));
			}

			players.push_back(entry);
		}

		Reply(connection, packet, nlohmann::json{{"players", players}, {"count", players.size()}});
	}

	void RconInventory(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		if (args.Num() < 2)
		{
			ReplyError(connection, packet, "Usage : qol.inventory <eosId>");
			return;
		}

		const std::string eos_id = ToUtf8(args[1]);
		AShooterPlayerController* pc = FindByEos(eos_id);
		if (pc == nullptr)
		{
			ReplyError(connection, packet, "Joueur introuvable ou deconnecte");
			return;
		}

		AShooterCharacter* character = PlayerCharacter(pc);
		if (character == nullptr || character->bIsDead()())
		{
			ReplyError(connection, packet, "Personnage mort ou absent : inventaire indisponible");
			return;
		}

		UPrimalInventoryComponent* inventory = character->MyInventoryComponentField();
		if (inventory == nullptr)
		{
			ReplyError(connection, packet, "Inventaire inaccessible");
			return;
		}

		nlohmann::json items = nlohmann::json::array();
		int engrams = 0;
		int skins = 0;
		int carried = 0;

		// L'inventaire d'ARK ne contient pas que ce que le joueur porte : les
		// engrammes appris et les skins possedes y sont ranges en permanence, et
		// representent l'essentiel du volume. Ils sont comptes a part.
		for (UPrimalItem* item : inventory->InventoryItemsField())
		{
			if (item == nullptr) continue;

			bool is_engram = false;
			bool is_skin = false;
			nlohmann::json entry = DescribeItem(item, is_engram, is_skin);

			if (is_engram) engrams++;
			else if (is_skin) skins++;
			else carried++;

			items.push_back(entry);
		}

		Reply(connection, packet,
			nlohmann::json{
				{"eosId", eos_id},
				{"name", ToUtf8(AsaApi::IApiUtils::GetCharacterName(pc))},
				{"count", items.size()},
				{"engrams", engrams},
				{"skins", skins},
				{"carried", carried},
				{"items", items},
			});
	}

	/**
	 * \brief Recense les creatures de la carte.
	 *
	 * Usage : qol.dinos [tamed|wild|all] [rayon] [decalage] [limite]
	 *
	 * Le rayon est centre sur l'origine du monde, ce qui couvre la carte entiere
	 * avec une valeur large. Une carte peuplee compte des milliers de creatures
	 * sauvages : mesure faite, le RCON ne transporte pas une telle reponse dans
	 * son delai (200 entrees passent en 1,3 s, 600 echouent a 16 s). La reponse
	 * est donc paginee, a l'appelant d'enchainer les pages via le decalage.
	 */
	void RconDinos(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		// Arguments nommes : la commande porte six criteres, l'ordre positionnel
		// deviendrait illisible et fragile
		std::map<std::string, std::string> options;
		for (int i = 1; i < args.Num(); ++i)
		{
			const std::string token = ToUtf8(args[i]);
			const size_t equals = token.find('=');
			if (equals == std::string::npos) continue;

			options[Lowercase(token.substr(0, equals))] = token.substr(equals + 1);
		}

		const auto option = [&options](const char* key, const std::string& fallback)
		{
			const auto found = options.find(key);
			return found != options.end() ? found->second : fallback;
		};

		std::string filter = Lowercase(option("filter", "tamed"));
		if (filter != "wild" && filter != "all" && filter != "tamed") filter = "tamed";

		const float radius = (std::max)(1.f, static_cast<float>(std::atof(option("radius", "1000000").c_str())));
		const int offset = (std::max)(0, std::atoi(option("offset", "0").c_str()));
		const int limit = (std::min)(kMaxDinos, (std::max)(1, std::atoi(option("limit", "200").c_str())));
		const int min_level = std::atoi(option("minlevel", "0").c_str());

		// Filtre d'espece applique cote serveur : sans lui, retrouver une creature
		// sur une carte peuplee obligerait a rapatrier des dizaines de milliers
		// d'entrees pour n'en garder que quelques-unes
		const std::string species_filter = Lowercase(option("species", ""));

		// Filtre de proprietaire : dompteur, empreinte ou proprietaire courant.
		// Applique cote serveur pour la meme raison que l'espece.
		const std::string owner_filter = Lowercase(option("owner", ""));

		// Le groupe restreint aux creatures apprivoisees evite de parcourir des
		// milliers de creatures sauvages quand elles ne sont pas demandees
		const EServerOctreeGroup::Type group =
			filter == "tamed" ? EServerOctreeGroup::DINOPAWNS_TAMED : EServerOctreeGroup::DINOPAWNS;

		const FVector origin{0, 0, 0};
		TArray<AActor*> actors = AsaApi::GetApiUtils().GetAllActorsInRange(origin, radius, group);

		nlohmann::json dinos = nlohmann::json::array();
		int matched = 0;
		bool truncated = false;

		for (AActor* actor : actors)
		{
			if (actor == nullptr) continue;

			auto* dino = static_cast<APrimalDinoCharacter*>(actor);

			// Une creature apprivoisee porte le nom de son dompteur et appartient
			// a une tribu ; les creatures sauvages n'ont ni l'un ni l'autre
			const std::string tamer = ToUtf8(dino->TamerStringField());
			const int team = dino->TargetingTeamField();
			const bool tamed = !tamer.empty() || team >= 50000;

			if (filter == "tamed" && !tamed) continue;
			if (filter == "wild" && tamed) continue;

			// AbsoluteBaseLevel sert a fixer le niveau au spawn et vaut 0 sur les
			// creatures existantes. Le niveau reel vit dans le composant de statut,
			// en deux parties : le niveau d'origine et les niveaux gagnes apres
			// apprivoisement.
			int base_level = 0;
			int extra_level = 0;
			if (UPrimalCharacterStatusComponent* status = dino->MyCharacterStatusComponentField())
			{
				base_level = status->BaseCharacterLevelField();
				extra_level = static_cast<int>(status->ExtraCharacterLevelField());
			}

			const int level = base_level + extra_level;
			if (level < min_level) continue;

			const std::string species = ToUtf8(dino->DescriptiveNameField());
			const std::string tamed_name = ToUtf8(dino->TamedNameField());
			if (!species_filter.empty()
				&& Lowercase(species).find(species_filter) == std::string::npos
				&& Lowercase(tamed_name).find(species_filter) == std::string::npos)
			{
				continue;
			}

			// Trois noms distincts, souvent confondus : celui qui a apprivoise la
			// creature, celui qui l'a marquee de son empreinte, et celui qui la
			// possede aujourd'hui. Un transfert de tribu change le dernier sans
			// toucher aux deux premiers.
			const std::string imprinter = ToUtf8(dino->ImprinterNameField());
			const std::string owner = ToUtf8(dino->OwningPlayerNameField());

			if (!owner_filter.empty()
				&& Lowercase(tamer).find(owner_filter) == std::string::npos
				&& Lowercase(imprinter).find(owner_filter) == std::string::npos
				&& Lowercase(owner).find(owner_filter) == std::string::npos)
			{
				continue;
			}

			const int index = matched++;
			if (index < offset) continue;
			if (static_cast<int>(dinos.size()) >= limit)
			{
				truncated = true;
				continue;
			}

			// Charge utile volontairement maigre : seules les coordonnees carte
			// sont transmises, la position monde n'apportant rien a l'affichage
			// et doublant le volume
			const AsaApi::MapCoords coords = AsaApi::GetApiUtils().FVectorToCoords(ActorPosition(actor));

			nlohmann::json entry;
			entry["s"] = species;
			entry["n"] = tamed_name;
			entry["t"] = tamed;
			entry["l"] = level;
			entry["lb"] = base_level; // niveau d'origine, avant montees post-apprivoisement
			entry["f"] = dino->bIsFemale()();
			entry["g"] = team;
			entry["lat"] = coords.y;
			entry["lon"] = coords.x;
			entry["tm"] = tamer;
			entry["im"] = imprinter;
			entry["ow"] = owner;

			// L'empreinte est volontairement absente. `AddedImprintingQuality`
			// existe, mais c'est une FONCTION — le cache d'offsets porte son
			// thunk `execAddedImprintingQuality` — et non un champ. En demander
			// l'offset comme champ leve une exception qui traverse le plugin et
			// abat le serveur : le filet pose sur les commandes n'a pas suffi.
			// La lire demanderait un NativeCall, a verifier avant d'essayer.

			// Points de niveau par statistique. Deux series distinctes : ceux
			// tires a la naissance, qui font la valeur genetique de la bete, et
			// ceux depenses apres apprivoisement, qui ne se transmettent pas.
			if (UPrimalCharacterStatusComponent* status = dino->MyCharacterStatusComponentField())
			{
				static const char* kNoms[] = {"hp", "st", "to", "ox", "fo", "wa",
				                              "te", "we", "me", "sp", "tf", "cr"};

				nlohmann::json sauvages = nlohmann::json::object();
				nlohmann::json apprivoises = nlohmann::json::object();
				nlohmann::json valeurs = nlohmann::json::object();

				auto points = status->NumberOfLevelUpPointsAppliedField();
				auto points_tames = status->NumberOfLevelUpPointsAppliedTamedField();
				auto maxima = status->MaxStatusValuesField();

				for (int i = 0; i < 12; ++i)
				{
					// `FieldArray` n'indexe pas : il rend le pointeur par son operateur ()
					const int brut = static_cast<int>(points()[i]);
					const int tame = static_cast<int>(points_tames()[i]);
					if (brut > 0) sauvages[kNoms[i]] = brut;
					if (tame > 0) apprivoises[kNoms[i]] = tame;

					const float valeur = maxima()[i];
					if (valeur != 0.f) valeurs[kNoms[i]] = valeur;
				}

				entry["pw"] = sauvages;      // points d'origine
				entry["pt"] = apprivoises;   // points depenses apres apprivoisement
				entry["mv"] = valeurs;       // valeurs maximales atteintes
			}

			dinos.push_back(entry);
		}

		Reply(connection, packet,
			nlohmann::json{
				{"filter", filter},
				{"radius", radius},
				{"matched", matched},
				{"offset", offset},
				{"returned", dinos.size()},
				{"truncated", truncated},
				{"dinos", dinos},
			});

		Log::GetLog()->info("Creatures recensees ({}) : {} trouvees", filter, matched);
	}

	void RconContainers(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		if (args.Num() < 2)
		{
			ReplyError(connection, packet, "Usage : qol.containers <eosId> [rayon]");
			return;
		}

		const std::string eos_id = ToUtf8(args[1]);
		AShooterPlayerController* pc = FindByEos(eos_id);
		if (pc == nullptr)
		{
			ReplyError(connection, packet, "Joueur introuvable ou deconnecte");
			return;
		}

		float radius = kDefaultRadius;
		if (args.Num() > 2)
		{
			const float parsed = static_cast<float>(std::atof(ToUtf8(args[2]).c_str()));
			if (parsed > 0.f) radius = parsed;
		}

		const FVector center = AsaApi::IApiUtils::GetPosition(pc);
		const int tribe = AsaApi::IApiUtils::GetTribeID(pc);

		nlohmann::json containers = nlohmann::json::array();
		int matched = 0;
		int empty = 0;
		int total_items = 0;
		bool truncated = false;

		const auto collect = [&](AActor* actor, UPrimalInventoryComponent* inventory, const char* kind)
		{
			if (inventory == nullptr) return;

			int count = 0;
			nlohmann::json items = DescribeInventory(inventory, count);

			// Les contenants vides sont comptes mais pas detailles : les lister
			// noierait la vue sous les fondations et torches d'une base
			if (count == 0)
			{
				empty++;
				return;
			}

			matched++;
			total_items += count;

			if (containers.size() >= kMaxContainers)
			{
				truncated = true;
				return;
			}

			nlohmann::json entry;
			entry["kind"] = kind;
			entry["name"] = ShortName(ToUtf8(AsaApi::IApiUtils::GetBlueprint(actor)));
			entry["itemCount"] = count;
			entry["returned"] = items.size();
			entry["items"] = items;
			AddPosition(entry, ActorPosition(actor));

			containers.push_back(entry);
		};

		// Coffres, forges, generateurs : toute structure dotee d'un inventaire
		for (AActor* actor :
			AsaApi::GetApiUtils().GetAllActorsInRange(center, radius, EServerOctreeGroup::STRUCTURES))
		{
			if (actor == nullptr || actor->TargetingTeamField() != tribe) continue;
			if (!IsContainerClass(actor->ClassPrivateField())) continue;

			collect(actor, static_cast<APrimalStructureItemContainer*>(actor)->MyInventoryComponentField(),
				"structure");
		}

		// Montures apprivoisees : leur inventaire est un rangement a part entiere
		for (AActor* actor :
			AsaApi::GetApiUtils().GetAllActorsInRange(center, radius, EServerOctreeGroup::DINOPAWNS_TAMED))
		{
			if (actor == nullptr || actor->TargetingTeamField() != tribe) continue;

			auto* dino = static_cast<APrimalDinoCharacter*>(actor);
			collect(actor, dino->MyInventoryComponentField(), "creature");
		}

		nlohmann::json payload{
			{"eosId", eos_id},
			{"name", ToUtf8(AsaApi::IApiUtils::GetCharacterName(pc))},
			{"tribeId", tribe},
			{"radius", radius},
			{"matched", matched},
			{"empty", empty},
			{"returned", containers.size()},
			{"totalItems", total_items},
			{"truncated", truncated},
			{"containers", containers},
		};

		AddPosition(payload["center"], center);
		Reply(connection, packet, payload);

		Log::GetLog()->info("Contenants listes pour {} : {} non vides, {} objets", eos_id, matched, total_items);
	}

	void RconStructures(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		if (args.Num() < 2)
		{
			ReplyError(connection, packet, "Usage : qol.structures <eosId> [rayon]");
			return;
		}

		const std::string eos_id = ToUtf8(args[1]);
		AShooterPlayerController* pc = FindByEos(eos_id);
		if (pc == nullptr)
		{
			ReplyError(connection, packet, "Joueur introuvable ou deconnecte");
			return;
		}

		float radius = kDefaultRadius;
		if (args.Num() > 2)
		{
			const float parsed = static_cast<float>(std::atof(ToUtf8(args[2]).c_str()));
			if (parsed > 0.f) radius = parsed;
		}

		const FVector center = AsaApi::IApiUtils::GetPosition(pc);
		const int tribe = AsaApi::IApiUtils::GetTribeID(pc);

		// Requete spatiale centree sur le joueur : parcourir toute la carte
		// couterait une pause serveur visible sur une base un peu developpee
		TArray<AActor*> actors =
			AsaApi::GetApiUtils().GetAllActorsInRange(center, radius, EServerOctreeGroup::STRUCTURES);

		nlohmann::json structures = nlohmann::json::array();
		int matched = 0;
		bool truncated = false;

		for (AActor* actor : actors)
		{
			if (actor == nullptr) continue;
			// Seules les constructions de la tribu du joueur sont remontees
			if (actor->TargetingTeamField() != tribe) continue;

			matched++;
			if (structures.size() >= kMaxStructures)
			{
				truncated = true;
				continue;
			}

			nlohmann::json entry;
			entry["name"] = ShortName(ToUtf8(AsaApi::IApiUtils::GetBlueprint(actor)));
			AddPosition(entry, ActorPosition(actor));
			structures.push_back(entry);
		}

		nlohmann::json payload{
			{"eosId", eos_id},
			{"name", ToUtf8(AsaApi::IApiUtils::GetCharacterName(pc))},
			{"tribeId", tribe},
			{"radius", radius},
			{"matched", matched},
			{"returned", structures.size()},
			{"truncated", truncated},
			{"structures", structures},
		};

		AddPosition(payload["center"], center);
		Reply(connection, packet, payload);

		Log::GetLog()->info("Structures listees pour {} : {} dans un rayon de {:.0f}", eos_id, matched, radius);
	}

	void RconDeathCache(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		if (args.Num() < 2)
		{
			ReplyError(connection, packet, "Usage : qol.deathcache <eosId> [rayon]");
			return;
		}

		// Deux formes : autour d'un joueur connecte, ou autour d'un point. La
		// seconde existe parce qu'un joueur mort se deconnecte souvent avant
		// qu'on ait pu chercher, et que sa depouille, elle, reste.
		FVector center{0, 0, 0};
		int suivant = 2;

		if (ToUtf8(args[1]) == "at")
		{
			if (args.Num() < 5)
			{
				ReplyError(connection, packet, "Usage : qol.deathcache at <x> <y> <z> [rayon]");
				return;
			}
			center.X = std::atof(ToUtf8(args[2]).c_str());
			center.Y = std::atof(ToUtf8(args[3]).c_str());
			center.Z = std::atof(ToUtf8(args[4]).c_str());
			suivant = 5;
		}
		else
		{
			AShooterPlayerController* pc =
				AsaApi::GetApiUtils().FindPlayerFromEOSID(ToUtf8(args[1]).c_str());
			if (pc == nullptr)
			{
				ReplyError(connection, packet, "Joueur introuvable ou deconnecte - utilisez : qol.deathcache at <x> <y> <z> [rayon]");
				return;
			}
			center = AsaApi::IApiUtils::GetPosition(pc);
		}

		// Un sac tombe parfois loin du point de mort ; le rayon par defaut est
		// donc large, au prix d'une requete plus couteuse qu'a l'ordinaire.
		float radius = 500000.f;
		if (args.Num() > suivant)
		{
			const float parsed = static_cast<float>(std::atof(ToUtf8(args[suivant]).c_str()));
			if (parsed > 0.f) radius = parsed;
		}
		TArray<AActor*> actors =
			AsaApi::GetApiUtils().GetAllActorsInRange(center, radius, EServerOctreeGroup::ALL_SPATIAL);

		nlohmann::json trouves = nlohmann::json::array();
		int balayes = 0;

		for (AActor* actor : actors)
		{
			if (actor == nullptr) continue;
			balayes++;

			const std::string nom = ShortName(ToUtf8(AsaApi::IApiUtils::GetBlueprint(actor)));
			const std::string minuscule = Lowercase(nom);
			if (minuscule.find("death") == std::string::npos
				&& minuscule.find("corpse") == std::string::npos
				&& minuscule.find("cache") == std::string::npos
				&& minuscule.find("bag") == std::string::npos)
			{
				continue;
			}

			nlohmann::json entry;
			entry["name"] = nom;
			entry["team"] = actor->TargetingTeamField();
			AddPosition(entry, ActorPosition(actor));

			// Le compte d'objets distingue un sac encore plein d'une depouille vide
			UPrimalInventoryComponent* inv = nullptr;
			if (APrimalStructureItemContainer* box = static_cast<APrimalStructureItemContainer*>(actor))
			{
				if (IsContainerClass(actor->ClassField())) inv = box->MyInventoryComponentField();
			}
			entry["items"] = inv != nullptr ? inv->InventoryItemsField().Num() : -1;

			trouves.push_back(entry);
		}

		Reply(connection, packet, nlohmann::json{
			{"radius", radius},
			{"scanned", balayes},
			{"found", trouves.size()},
			{"caches", trouves},
		});
	}

	void RconGameMode(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		AShooterGameMode* mode = AsaApi::GetApiUtils().GetShooterGameMode();
		if (mode == nullptr)
		{
			ReplyError(connection, packet, "Mode de jeu indisponible");
			return;
		}

		nlohmann::json valeurs = nlohmann::json::object();

		// Chaque lecture est isolee : une cle absente d'une version du jeu doit
		// faire manquer une ligne, pas la reponse entiere. Les noms sont ceux du
		// binaire, verifies presents comme CHAMPS et non comme fonctions — la
		// confusion des deux a deja coute un arret du serveur.
		const char* booleens[] = {
			"AShooterGameMode.bEnableCryopodNerf",
			"AShooterGameMode.bEnableCryoSicknessPVE",
			"AShooterGameMode.bDisableCryopodFridgeRequirement",
			"AShooterGameMode.bDisableCryopodEnemyCheck",
			"AShooterGameMode.bAllowCryoFridgeOnSaddle",
		};
		const char* flottants[] = {
			"AShooterGameMode.CryopodNerfDuration",
			"AShooterGameMode.CryopodNerfDamageMult",
			"AShooterGameMode.CryopodNerfIncomingDamageMultPercent",
			"AShooterGameMode.CryopodFridgeCooldownTime",
			"AShooterGameMode.TamingSpeedMultiplier",
			"AShooterGameMode.MatingIntervalMultiplier",
			"AShooterGameMode.EggHatchSpeedMultiplier",
			"AShooterGameMode.BabyMatureSpeedMultiplier",
			"AShooterGameMode.BabyCuddleIntervalMultiplier",
			"AShooterGameMode.PassiveTameIntervalMultiplier",
			"AShooterGameMode.WildDinoTorporDrainMultiplier",
		};

		const auto cle = [](const char* complet)
		{
			const std::string nom = complet;
			const size_t point = nom.rfind('.');
			return point == std::string::npos ? nom : nom.substr(point + 1);
		};

		for (const char* champ : booleens)
		{
			try { valeurs[cle(champ)] = *GetNativePointerField<bool*>(mode, champ); }
			catch (...) { /* absent de cette version */ }
		}

		for (const char* champ : flottants)
		{
			try { valeurs[cle(champ)] = *GetNativePointerField<float*>(mode, champ); }
			catch (...) { /* absent de cette version */ }
		}

		Reply(connection, packet, nlohmann::json{{"settings", valeurs}, {"count", valeurs.size()}});
	}

	void RconCreative(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		int wanted = -1; // -1 = lecture seule
		for (int i = 1; i < args.Num(); ++i)
		{
			const std::string arg = Lowercase(ToUtf8(args[i]));
			if (arg.rfind("show=", 0) == 0) wanted = std::atoi(arg.substr(5).c_str()) != 0 ? 1 : 0;
		}

		UWorld* world = AsaApi::GetApiUtils().GetWorld();
		if (world == nullptr)
		{
			ReplyError(connection, packet, "Monde indisponible");
			return;
		}

		auto* state = static_cast<AShooterGameState*>(world->GameStateField().Get());
		if (state == nullptr)
		{
			ReplyError(connection, packet, "Etat de jeu indisponible");
			return;
		}

		auto* mode = static_cast<AShooterGameMode*>(state->AuthorityGameModeField().Get());

		nlohmann::json report;
		report["gameStateBefore"] = state->bShowCreativeModeField();
		if (mode != nullptr) report["gameModeBefore"] = mode->bShowCreativeModeField();

		// Octroi direct a un joueur, sans passer par la console ni par
		// l'authentification admin : le plugin s'execute deja cote serveur avec
		// tous les droits, et `AddCheats(true)` cree le gestionnaire de triche
		// meme si le joueur n'a jamais tape EnableCheats.
		// Seul l'octroi est propose. Le retrait a ete implemente puis retire :
		// `SetCreativeModeOnPawn(pawn, false)` a fait tomber le serveur sur une
		// violation d'acces a l'adresse 0x10, dans le traitement du paquet RCON.
		// Le mode creatif se perd de toute facon a la deconnexion du joueur.
		std::string target;
		for (int i = 1; i < args.Num(); ++i)
		{
			const std::string arg = ToUtf8(args[i]);
			if (Lowercase(arg).rfind("give=", 0) == 0) target = arg.substr(5);
		}

		if (!target.empty())
		{
			AShooterPlayerController* player = AsaApi::GetApiUtils().FindPlayerFromEOSID(ToFString(target));
			if (player == nullptr)
			{
				ReplyError(connection, packet, "Joueur non connecte");
				return;
			}

			AShooterCharacter* character = PlayerCharacter(player);
			report["hasCharacter"] = character != nullptr;

			player->AddCheats(true);
			auto* cheats = static_cast<UShooterCheatManager*>(player->CheatManagerField().Get());
			report["hasCheatManager"] = cheats != nullptr;

			if (cheats != nullptr && character != nullptr)
			{
				report["granted"] = cheats->SetCreativeModeOnPawn(character, true);
			}
			else if (cheats != nullptr)
			{
				// Sans personnage vivant, on retombe sur la voie par identifiant
				cheats->GiveCreativeModeToPlayer(player->LinkedPlayerIDField());
				report["granted"] = true;
			}
		}

		if (wanted >= 0)
		{
			// Les deux sont poses : le mode de jeu pour la coherence cote serveur,
			// l'etat de jeu parce que c'est lui qui voyage jusqu'au client
			const bool value = wanted == 1;
			if (mode != nullptr) mode->bShowCreativeModeField() = value;
			state->bShowCreativeModeField() = value;
		}

		report["gameState"] = state->bShowCreativeModeField();
		if (mode != nullptr) report["gameMode"] = mode->bShowCreativeModeField();
		report["applied"] = wanted >= 0;

		Reply(connection, packet, report);

		Log::GetLog()->info("Mode creatif : etat={} mode={}",
			state->bShowCreativeModeField(), mode != nullptr ? mode->bShowCreativeModeField() : false);
	}
} // namespace QoL
