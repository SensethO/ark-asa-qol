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

	/**
	 * \brief Genetique d'un oeuf feconde, ou rien si l'objet n'en est pas un.
	 *
	 * Les points de niveau d'un oeuf sont sa valeur : ils sont deja tires, et
	 * ce sont eux dont heritera la creature a l'eclosion. Les lire evite de
	 * faire eclore pour savoir.
	 */
	bool DescribeEgg(UPrimalItem* item, const std::string& classe, nlohmann::json& entry)
	{
		// Le test porte sur le nom de classe : une comparaison de chaine ne
		// peut pas lever, la ou la lecture d'un champ absent abat le serveur.
		if (classe.find("Fertilized") == std::string::npos) return false;

		static const char* kNoms[] = {"hp", "st", "to", "ox", "fo", "wa",
		                              "te", "we", "me", "sp", "tf", "cr"};

		nlohmann::json points = nlohmann::json::object();
		nlohmann::json mutations = nlohmann::json::object();

		auto niveaux = item->EggNumberOfLevelUpPointsAppliedField();
		auto mutes = item->EggNumberMutationsAppliedField();

		for (int i = 0; i < 12; ++i)
		{
			// `FieldArray` n'indexe pas : son operateur () rend le pointeur
			const int n = static_cast<int>(niveaux()[i]);
			const int m = static_cast<int>(mutes()[i]);
			if (n > 0) points[kNoms[i]] = n;
			if (m > 0) mutations[kNoms[i]] = m;
		}

		entry["kind"] = "egg";
		entry["points"] = points;
		entry["mutations"] = mutations;
		return true;
	}

	/**
	 * \brief Disposition des donnees d'un cryopode, sans rien en deduire.
	 *
	 * La creature est rangee dans `CustomItemDatas`. Son bloc d'octets est une
	 * sauvegarde serialisee, hors de portee. Mais la structure porte aussi des
	 * chaines, des flottants, des classes et des noms, tous typés — et c'est
	 * la que doivent se trouver l'espece et le nom.
	 *
	 * On rapporte donc ce qu'on trouve, index par index, sans supposer lequel
	 * porte quoi. Deviner les index est exactement ce qui a fait tomber le
	 * serveur sur `AddedImprintingQuality`.
	 */
	bool DescribeCryopod(UPrimalItem* item, const std::string& classe, nlohmann::json& entry)
	{
		if (classe.find("SoulTrap") == std::string::npos
			&& classe.find("Cryopod") == std::string::npos) return false;

		entry["kind"] = "cryopod";

		nlohmann::json blocs = nlohmann::json::array();
		for (FCustomItemData& data : item->CustomItemDatasField())
		{
			nlohmann::json bloc;
			bloc["name"] = ToUtf8(data.CustomDataNameField().ToString());

			nlohmann::json chaines = nlohmann::json::array();
			for (FString& v : data.CustomDataStringsField()) chaines.push_back(ToUtf8(v));
			bloc["strings"] = chaines;

			nlohmann::json flottants = nlohmann::json::array();
			for (float v : data.CustomDataFloatsField()) flottants.push_back(v);
			bloc["floats"] = flottants;

			nlohmann::json classes = nlohmann::json::array();
			for (UClass* c : data.CustomDataClassesField())
			{
				classes.push_back(c != nullptr ? ToUtf8(c->NameField().ToString()) : "");
			}
			bloc["classes"] = classes;

			nlohmann::json noms = nlohmann::json::array();
			for (FName& n : data.CustomDataNamesField()) noms.push_back(ToUtf8(n.ToString()));
			bloc["names"] = noms;

			blocs.push_back(bloc);
		}
		entry["data"] = blocs;
		return true;
	}

	void RconStored(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		if (args.Num() < 2)
		{
			ReplyError(connection, packet, "Usage : qol.stored <eosId> [rayon]");
			return;
		}

		AShooterPlayerController* pc = FindByEos(ToUtf8(args[1]));
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

		nlohmann::json trouves = nlohmann::json::array();
		int eggs = 0;
		int pods = 0;
		bool truncated = false;

		const auto fouiller = [&](AActor* actor, UPrimalInventoryComponent* inventory, const char* ou)
		{
			if (inventory == nullptr) return;

			for (UPrimalItem* item : inventory->InventoryItemsField())
			{
				if (item == nullptr) continue;
				if (trouves.size() >= kMaxContainers) { truncated = true; return; }

				try
				{
					const std::string classe =
						ToUtf8(AsaApi::IApiUtils::GetItemBlueprint(item));

					nlohmann::json entry;
					const bool oeuf = DescribeEgg(item, classe, entry);
					const bool pod = !oeuf && DescribeCryopod(item, classe, entry);
					if (!oeuf && !pod) continue;

					if (oeuf) eggs++; else pods++;

					entry["item"] = ShortName(classe);
					entry["label"] = ToUtf8(item->DescriptiveNameBaseField());
					entry["custom"] = ToUtf8(item->CustomItemNameField());
					entry["where"] = ou;
					if (actor != nullptr)
					{
						entry["container"] =
							ShortName(ToUtf8(AsaApi::IApiUtils::GetBlueprint(actor)));
						AddPosition(entry, ActorPosition(actor));
					}

					trouves.push_back(entry);
				}
				catch (const std::exception& error)
				{
					// Un objet illisible ne doit pas emporter le recensement
					Log::GetLog()->error("qol.stored : objet ignore ({})", error.what());
				}
			}
		};

		// L'inventaire du joueur lui-meme : c'est la qu'on porte ses cryopodes
		if (AShooterCharacter* perso = PlayerCharacter(pc))
		{
			fouiller(nullptr, perso->MyInventoryComponentField(), "joueur");
		}

		for (AActor* actor :
			AsaApi::GetApiUtils().GetAllActorsInRange(center, radius, EServerOctreeGroup::STRUCTURES))
		{
			if (actor == nullptr || actor->TargetingTeamField() != tribe) continue;
			if (!IsContainerClass(actor->ClassPrivateField())) continue;

			fouiller(actor, static_cast<APrimalStructureItemContainer*>(actor)->MyInventoryComponentField(),
				"structure");
		}

		for (AActor* actor :
			AsaApi::GetApiUtils().GetAllActorsInRange(center, radius, EServerOctreeGroup::DINOPAWNS_TAMED))
		{
			if (actor == nullptr || actor->TargetingTeamField() != tribe) continue;

			fouiller(actor, static_cast<APrimalDinoCharacter*>(actor)->MyInventoryComponentField(),
				"creature");
		}

		nlohmann::json payload{
			{"radius", radius},
			{"eggs", eggs},
			{"cryopods", pods},
			{"truncated", truncated},
			{"found", trouves},
		};
		AddPosition(payload["center"], center);
		Reply(connection, packet, payload);
	}

	void RconSpecies(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		if (args.Num() < 2)
		{
			ReplyError(connection, packet, "Usage : qol.species <ClasseDeCreature>");
			return;
		}

		// `qol.species live <eosId> [rayon]` : les coefficients lus sur des betes
		// VIVANTES. L'objet par defaut d'une classe n'a pas de composant de
		// statut — mesure, pas suppose — mais un acteur en a forcement un.
		if (ToUtf8(args[1]) == "live")
		{
			if (args.Num() < 3)
			{
				ReplyError(connection, packet, "Usage : qol.species live <eosId> [rayon]");
				return;
			}

			AShooterPlayerController* pc = FindByEos(ToUtf8(args[2]));
			if (pc == nullptr)
			{
				ReplyError(connection, packet, "Joueur introuvable ou deconnecte");
				return;
			}

			float rayon = kDefaultRadius;
			if (args.Num() > 3)
			{
				const float lu = static_cast<float>(std::atof(ToUtf8(args[3]).c_str()));
				if (lu > 0.f) rayon = lu;
			}

			static const char* kNoms[] = {"hp", "st", "to", "ox", "fo", "wa",
			                              "te", "we", "me", "sp", "tf", "cr"};

			nlohmann::json especes = nlohmann::json::object();
			const FVector centre = AsaApi::IApiUtils::GetPosition(pc);

			for (AActor* actor : AsaApi::GetApiUtils().GetAllActorsInRange(
				     centre, rayon, EServerOctreeGroup::DINOPAWNS_TAMED))
			{
				if (actor == nullptr) continue;
				auto* dino = static_cast<APrimalDinoCharacter*>(actor);

				const std::string classe =
					ToUtf8(actor->ClassPrivateField()->NameField().ToString());
				// Une espece par entree : deux exemplaires n'apprennent rien de plus
				if (especes.contains(classe)) continue;

				UPrimalCharacterStatusComponent* statut = dino->MyCharacterStatusComponentField();
				if (statut == nullptr) continue;

				nlohmann::json e;
				e["level"] = static_cast<int>(statut->BaseCharacterLevelField())
					+ static_cast<int>(statut->ExtraCharacterLevelField());
				e["baseLevel"] = static_cast<int>(statut->BaseCharacterLevelField());
				e["extraLevel"] = static_cast<int>(statut->ExtraCharacterLevelField());

				auto points = statut->NumberOfLevelUpPointsAppliedField();
				auto points_tames = statut->NumberOfLevelUpPointsAppliedTamedField();
				auto maxima = statut->MaxStatusValuesField();
				auto par_niveau = statut->AmountMaxGainedPerLevelUpValueField();

				nlohmann::json pw = nlohmann::json::object();
				nlohmann::json pt = nlohmann::json::object();
				nlohmann::json mv = nlohmann::json::object();
				nlohmann::json inc = nlohmann::json::object();
				int somme = 0;

				for (int i = 0; i < 12; ++i)
				{
					const int n = static_cast<int>(points()[i]);
					const int t = static_cast<int>(points_tames()[i]);
					somme += n;
					if (n > 0) pw[kNoms[i]] = n;
					// Une statistique montee apres apprivoisement ne peut plus
					// servir d'etalon : sa valeur inclut ces montees, et la base
					// qu'on en tirerait serait fausse. Le dire permet d'ecarter
					// la STATISTIQUE seule, au lieu de la bete entiere.
					if (t > 0) pt[kNoms[i]] = t;
					mv[kNoms[i]] = maxima()[i];
					inc[kNoms[i]] = par_niveau()[i];
				}

				e["wildPointsSum"] = somme;
				e["wildPoints"] = pw;
				e["tamedPoints"] = pt;
				e["maxValues"] = mv;
				e["perWildLevel"] = inc;

				especes[classe] = e;
			}

			Reply(connection, packet, nlohmann::json{{"radius", rayon}, {"species", especes}});
			return;
		}

		// Les donnees d'un cryopode nomment la creature « Argent_Character_BP_C_2146995208 » :
		// le suffixe est l'identifiant d'instance, pas la classe.
		std::string voulu = ToUtf8(args[1]);
		const size_t tiret = voulu.find_last_of('_');
		if (tiret != std::string::npos
			&& voulu.find_first_not_of("0123456789", tiret + 1) == std::string::npos)
		{
			voulu = voulu.substr(0, tiret);
		}

		nlohmann::json payload{{"wanted", voulu}};

		// Chaque etape est journalisee AVANT d'etre tentee : si le serveur
		// tombe, la derniere ligne ecrite designe l'appel fautif. C'est ce qui
		// a manque aux deux plantages du catalogue.
		try
		{
			Log::GetLog()->info("Especes : classe de base");
			UClass* base = APrimalDinoCharacter::StaticClass();
			if (base == nullptr)
			{
				ReplyError(connection, packet, "Classe de base introuvable");
				return;
			}

			Log::GetLog()->info("Especes : enumeration des classes derivees");
			TArray<UClass*> derivees;
			NativeCall<void, const UClass*, TArray<UClass*>*, bool>(
				nullptr,
				"Global.GetDerivedClasses(UClass*,TArray<UClass*,TSizedDefaultAllocator<32>>&,bool)",
				base, &derivees, true);

			payload["derived"] = derivees.Num();
			Log::GetLog()->info("Especes : {} classes derivees", derivees.Num());

			UClass* trouvee = nullptr;
			for (int i = 0; i < derivees.Num(); ++i)
			{
				UClass* c = derivees[i];
				if (c == nullptr) continue;
				if (ToUtf8(c->NameField().ToString()) == voulu) { trouvee = c; break; }
			}

			payload["found"] = trouvee != nullptr;
			if (trouvee == nullptr)
			{
				// Une classe non chargee est absente de la hierarchie : c'est
				// une information, pas une erreur.
				Reply(connection, packet, payload);
				return;
			}

			Log::GetLog()->info("Especes : objet par defaut");
			UObject* cdo = trouvee->ClassDefaultObjectField();
			payload["defaultObject"] = cdo != nullptr;
			if (cdo == nullptr) { Reply(connection, packet, payload); return; }

			Log::GetLog()->info("Especes : composant de statut de l'objet par defaut");
			auto* dino = static_cast<APrimalDinoCharacter*>(cdo);
			UPrimalCharacterStatusComponent* statut = dino->MyCharacterStatusComponentField();

			// C'est le point incertain : un composant est instancie par acteur.
			payload["statusComponent"] = statut != nullptr;
			if (statut == nullptr) { Reply(connection, packet, payload); return; }

			Log::GetLog()->info("Especes : lecture des coefficients");
			static const char* kNoms[] = {"hp", "st", "to", "ox", "fo", "wa",
			                              "te", "we", "me", "sp", "tf", "cr"};

			auto bases = statut->MaxStatusValuesField();
			auto par_niveau = statut->AmountMaxGainedPerLevelUpValueField();
			auto par_niveau_tame = statut->AmountMaxGainedPerLevelUpValueTamedField();

			nlohmann::json b = nlohmann::json::object();
			nlohmann::json i_sauvage = nlohmann::json::object();
			nlohmann::json i_tame = nlohmann::json::object();

			for (int i = 0; i < 12; ++i)
			{
				b[kNoms[i]] = bases()[i];
				i_sauvage[kNoms[i]] = par_niveau()[i];
				i_tame[kNoms[i]] = par_niveau_tame()[i];
			}

			payload["base"] = b;
			payload["perWildLevel"] = i_sauvage;
			payload["perTamedLevel"] = i_tame;
		}
		catch (const std::exception& error)
		{
			payload["error"] = error.what();
			Log::GetLog()->error("Especes : {}", error.what());
		}

		Reply(connection, packet, payload);
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
	/**
	 * rief `qol.dinogroup <eosId> list` ou `qol.dinogroup <eosId> <groupe|all> <ordre>`.
	 *
	 * Reproduit le menu T : choisir un groupe de creatures puis leur donner un
	 * ordre. Plutot que de re-parcourir les creatures nous-memes, on rejoue ce
	 * que fait le jeu quand le joueur siffle : on selectionne le groupe sur son
	 * PlayerState (`ServerSetSelectedDinoOrderGroup`), puis on declenche l'ordre
	 * sur son personnage (`ServerCall*_Implementation`). La portee de l'ordre
	 * (`TamedDinoCallOutRange`) et le filtrage par groupe restent ceux du jeu.
	 *
	 * Groupes numerotes de 1 a 10 comme dans l'interface ; `all` retire le filtre.
	 * Ordres : follow, stay, aggressive, passive, neutral, passiveflee, attack.
	 *
	 * A VERIFIER EN JEU : l'appel direct des `_Implementation` depuis le serveur,
	 * et la valeur interne de "aucun groupe" (supposee -1).
	 */
	// ABI : le code du jeu recoit un TSubclassOf par adresse (la sonde l'a montre : l'argument est une adresse de pile,
	// pas une UClass). Les en-tetes d'AsaApi le declarent par valeur, d'ou des lectures toujours fausses. On appelle donc
	// les fonctions natives en passant l'adresse d'un TSubclassOf.
	bool ClasseDansGroupe(AShooterPlayerState* state, int index, UClass* classe)
	{
		TSubclassOf<APrimalDinoCharacter> sous{classe};
		return NativeCall<bool, int, TSubclassOf<APrimalDinoCharacter>*>(
			state, "AShooterPlayerState.IsDinoClassInOrderGroup(int,TSubclassOf<APrimalDinoCharacter>)", index, &sous);
	}

	void AjouterOuRetirerClasse(AShooterPlayerState* state, int index, UClass* classe, bool ajout)
	{
		TSubclassOf<APrimalDinoCharacter> sous{classe};
		NativeCall<void, int, TSubclassOf<APrimalDinoCharacter>*, bool>(
			state, "AShooterPlayerState.ServerDinoOrderGroup_AddOrRemoveDinoClass_Implementation(int,TSubclassOf<APrimalDinoCharacter>,bool)",
			index, &sous, ajout);
	}

	struct FStringBrut
	{
		const wchar_t* data;
		int num;
		int max;
	};

	/**
	 * Lit le nom d'un groupe. FDinoOrderGroup est declare sans membre dans les en-tetes (taille 1) : on ne l'indexe JAMAIS,
	 * on avance de la taille REELLE de la structure, lue dans sa description d'execution. Tout acces invalide est intercepte :
	 * une lecture ratee rend « pas de nom », jamais un plantage.
	 */
	bool LireNomGroupe(AShooterPlayerState* state, int index, int taille, wchar_t* sortie, int capacite)
	{
		__try
		{
			char* base = reinterpret_cast<char*>(state->DinoOrderGroupsField()());
			FDinoOrderGroup* groupe = reinterpret_cast<FDinoOrderGroup*>(base + static_cast<size_t>(index) * static_cast<size_t>(taille));
			const FStringBrut* brut = reinterpret_cast<const FStringBrut*>(&groupe->DinoOrderGroupNameField());
			sortie[0] = 0;
			if (brut->data == nullptr || brut->num <= 1 || brut->num > capacite) return brut->num <= 1;
			for (int i = 0; i < brut->num; ++i) sortie[i] = brut->data[i];
			sortie[capacite - 1] = 0;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	std::string VersUtf8(const wchar_t* texte)
	{
		const int n = WideCharToMultiByte(CP_UTF8, 0, texte, -1, nullptr, 0, nullptr, nullptr);
		if (n <= 1) return {};
		std::string sortie(static_cast<size_t>(n), '\0');
		WideCharToMultiByte(CP_UTF8, 0, texte, -1, sortie.data(), n, nullptr, nullptr);
		sortie.resize(static_cast<size_t>(n) - 1);
		return sortie;
	}

	void RconDinoGroup(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		if (args.Num() < 3)
		{
			ReplyError(connection, packet,
				"Usage : qol.dinogroup <eosId> list | classes | members <groupe> | setclass <groupe> <espece> | removeclass <groupe> <espece> | clear <groupe> | <groupe 1-10|all> <follow|stay|aggressive|passive|neutral|passiveflee|attack>");
			return;
		}

		const std::string eos_id = ToUtf8(args[1]);
		AShooterPlayerController* pc = FindByEos(eos_id);
		if (pc == nullptr)
		{
			ReplyError(connection, packet, "Joueur introuvable ou deconnecte");
			return;
		}

		auto* state = static_cast<AShooterPlayerState*>(pc->PlayerStateField().Get());
		if (state == nullptr)
		{
			ReplyError(connection, packet, "Etat du joueur indisponible");
			return;
		}

		const std::string what = ToUtf8(args[2]);

		if (what == "list")
		{
			nlohmann::json groups = nlohmann::json::array();
			// Les noms ne sont pas lus : indexer DinoOrderGroupsField() plante (FDinoOrderGroup mesure 1 octet dans les en-tetes)
			for (int i = 0; i < 10; ++i) groups.push_back({{"group", i + 1}, {"name", "Group " + std::to_string(i + 1)}});
			Reply(connection, packet,
				nlohmann::json{{"eosId", eos_id},
				               {"selected", state->CurrentlySelectedDinoOrderGroupField()},
				               {"groups", groups}});
			return;
		}

		// ---- Composition des groupes (1.5) -------------------------------------------
		// Un groupe du menu T est defini par ESPECES (classes de creatures) : toute creature de
		// ces especes y entre. Les commandes ci-dessous lisent et modifient ces especes.
		//   qol.dinogroup <eos> classes                         especes de chaque groupe
		//   qol.dinogroup <eos> setclass <groupe> <espece...>   ajoute une espece a un groupe
		//   qol.dinogroup <eos> removeclass <groupe> <espece...> retire une espece
		//   qol.dinogroup <eos> clear <groupe>                  vide les especes d'un groupe
		//   qol.dinogroup <eos> members <groupe>                creatures de la tribu qui en font partie
		// A VERIFIER EN JEU : les appels `_Implementation` ci-dessous (meme reserve que pour l'ordre).
		if (what == "classes")
		{
			// Especes presentes dans la tribu : une classe par espece, prise sur une creature REELLE. On n'ouvre jamais
			// le tableau interne des classes d'un groupe (sa lecture directe a fait tomber un serveur, 1.5 initiale) :
			// c'est le jeu lui-meme qui repond « cette espece est-elle dans ce groupe ? ».
			const int tribe = AsaApi::IApiUtils::GetTribeID(pc);
			const FVector origin{0, 0, 0};
			TArray<AActor*> actors =
				AsaApi::GetApiUtils().GetAllActorsInRange(origin, 1000000.f, EServerOctreeGroup::DINOPAWNS_TAMED);
			std::map<std::string, std::pair<UClass*, int>> presentes;
			for (AActor* actor : actors)
			{
				if (actor == nullptr || actor->TargetingTeamField() != tribe) continue;
				auto* dino = static_cast<APrimalDinoCharacter*>(actor);
				const std::string nom = ToUtf8(dino->DescriptiveNameField());
				if (nom.empty() || dino->ClassField() == nullptr) continue;
				auto& entree = presentes[nom];
				if (entree.first == nullptr) entree.first = dino->ClassField();
				entree.second++;
			}

			nlohmann::json groups = nlohmann::json::array();
			// ATTENTION : ne jamais indexer `DinoOrderGroupsField()` (fields()[i]) : FDinoOrderGroup est declare sans membre dans
			// les en-tetes, sa taille vaut 1 octet et tout indice au-dela de 0 pointe en memoire invalide (plantage constate).
			// Les noms de groupes ne sont donc pas lus ; les especes viennent de IsDinoClassInOrderGroup, appele par le jeu.
			const int taille_groupe = GetStructSize<FDinoOrderGroup>();
			for (int i = 0; i < 10; ++i)
			{
				nlohmann::json especes = nlohmann::json::array();
				for (const auto& [nom, entree] : presentes)
					if (ClasseDansGroupe(state, i, entree.first)) especes.push_back(nom);
				std::string nom = "Group " + std::to_string(i + 1);
				if (taille_groupe >= 24 && taille_groupe <= 1024)
				{
					wchar_t tampon[96];
					if (LireNomGroupe(state, i, taille_groupe, tampon, 96))
					{
						const std::string lu = VersUtf8(tampon);
						if (!lu.empty()) nom = lu;
					}
				}
				groups.push_back({{"group", i + 1}, {"name", nom}, {"species", especes}});
			}
			nlohmann::json available = nlohmann::json::array();
			for (const auto& [nom, entree] : presentes) available.push_back({{"s", nom}, {"n", entree.second}});

			Reply(connection, packet,
				nlohmann::json{{"eosId", eos_id}, {"selected", state->CurrentlySelectedDinoOrderGroupField()}, {"groups", groups}, {"available", available}});
			return;
		}

		if (what == "setclass" || what == "removeclass" || what == "clear" || what == "members" || what == "adddino" || what == "rmdino" || what == "rename")
		{
			if (args.Num() < 4)
			{
				ReplyError(connection, packet, "Groupe manquant (1 a 10)");
				return;
			}
			const int index = std::atoi(ToUtf8(args[3]).c_str()) - 1;
			if (index < 0 || index > 9)
			{
				ReplyError(connection, packet, "Groupe invalide : 1 a 10");
				return;
			}

			if (what == "clear")
			{
				state->ServerDinoOrderGroup_Clear_Implementation(index, true, true);
				Reply(connection, packet, nlohmann::json{{"eosId", eos_id}, {"group", index + 1}, {"cleared", true}});
				Log::GetLog()->info("Groupe {} vide pour {}", index + 1, eos_id);
				return;
			}

			const int tribe = AsaApi::IApiUtils::GetTribeID(pc);
			const FVector origin{0, 0, 0};
			TArray<AActor*> actors =
				AsaApi::GetApiUtils().GetAllActorsInRange(origin, 1000000.f, EServerOctreeGroup::DINOPAWNS_TAMED);

			if (what == "members")
			{
				nlohmann::json dinos = nlohmann::json::array();
				for (AActor* actor : actors)
				{
					if (actor == nullptr || actor->TargetingTeamField() != tribe) continue;
					auto* dino = static_cast<APrimalDinoCharacter*>(actor);
					if (!state->IsDinoInOrderGroup(index, dino)) continue;
					if (dinos.size() >= static_cast<size_t>(kMaxDinos)) break;

					int base_level = 0;
					int extra_level = 0;
					nlohmann::json points = nlohmann::json::object();
					if (UPrimalCharacterStatusComponent* status = dino->MyCharacterStatusComponentField())
					{
						base_level = status->BaseCharacterLevelField();
						extra_level = static_cast<int>(status->ExtraCharacterLevelField());
						static const char* kNoms[] = {"hp", "st", "to", "ox", "fo", "wa", "te", "we", "me", "sp", "tf", "cr"};
						auto appliques = status->NumberOfLevelUpPointsAppliedField();
						auto apprivoises = status->NumberOfLevelUpPointsAppliedTamedField();
						for (int i = 0; i < 12; ++i)
						{
							const int brut = static_cast<int>(appliques()[i]);
							const int tame = static_cast<int>(apprivoises()[i]);
							if (brut > 0 || tame > 0) points[kNoms[i]] = {{"w", brut}, {"t", tame}};
						}
					}

					const AsaApi::MapCoords coords = AsaApi::GetApiUtils().FVectorToCoords(ActorPosition(actor));
					dinos.push_back({{"s", ToUtf8(dino->DescriptiveNameField())},
					                 {"n", ToUtf8(dino->TamedNameField())},
					                 {"l", base_level + extra_level},
					                 {"lb", base_level},
					                 {"f", dino->bIsFemale()()},
					                 {"lat", coords.y},
					                 {"lon", coords.x},
					                 {"pts", points}});
				}
				Reply(connection, packet,
					nlohmann::json{{"eosId", eos_id}, {"group", index + 1}, {"count", dinos.size()}, {"dinos", dinos}});
				return;
			}

			// rename <groupe> <nom> : renomme le groupe (le client l'affiche dans son menu T)
			if (what == "rename")
			{
				std::string nom;
				for (int i = 4; i < args.Num(); ++i) nom += (i > 4 ? " " : "") + ToUtf8(args[i]);
				if (nom.empty() || nom.size() > 60)
				{
					ReplyError(connection, packet, "Nom attendu : 1 a 60 caracteres");
					return;
				}
				FString fnom = ToFString(nom);
				state->ServerSetDinoGroupName_Implementation(index, fnom);
				Reply(connection, packet, nlohmann::json{{"eosId", eos_id}, {"group", index + 1}, {"name", nom}, {"action", "rename"}});
				Log::GetLog()->info("Groupe {} renomme en {} pour {}", index + 1, nom, eos_id);
				return;
			}

			// adddino / rmdino <groupe> <espece>|<nom>|<niveau d'origine>|<sexe 0/1> : une creature precise, retrouvee dans la tribu
			// (le serveur n'expose aucun identifiant : on combine espece, nom, niveau d'origine et sexe).
			if (what == "adddino" || what == "rmdino")
			{
				std::string cle;
				for (int i = 4; i < args.Num(); ++i) cle += (i > 4 ? " " : "") + ToUtf8(args[i]);
				std::vector<std::string> champs;
				for (size_t debut = 0;;)
				{
					const size_t fin = cle.find('|', debut);
					champs.push_back(cle.substr(debut, fin == std::string::npos ? std::string::npos : fin - debut));
					if (fin == std::string::npos) break;
					debut = fin + 1;
				}
				if (champs.size() != 4)
				{
					ReplyError(connection, packet, "Cle attendue : espece|nom|niveau d'origine|sexe");
					return;
				}
				const bool ajout = what == "adddino";
				const int niveau = std::atoi(champs[2].c_str());
				const bool femelle = champs[3] == "1";
				APrimalDinoCharacter* cible = nullptr;
				for (AActor* actor : actors)
				{
					if (actor == nullptr || actor->TargetingTeamField() != tribe) continue;
					auto* dino = static_cast<APrimalDinoCharacter*>(actor);
					if (Lowercase(ToUtf8(dino->DescriptiveNameField())) != Lowercase(champs[0])) continue;
					if (ToUtf8(dino->TamedNameField()) != champs[1]) continue;
					if (static_cast<bool>(dino->bIsFemale()()) != femelle) continue;
					UPrimalCharacterStatusComponent* status = dino->MyCharacterStatusComponentField();
					if (status == nullptr || status->BaseCharacterLevelField() != niveau) continue;
					// ajout : une creature pas encore dans le groupe ; retrait : une qui y est (cas de creatures jumelles)
					if (ajout != state->IsDinoInOrderGroup(index, dino)) { cible = dino; break; }
				}
				if (cible == nullptr)
				{
					ReplyError(connection, packet, ajout ? "Creature introuvable ou deja dans le groupe" : "Creature introuvable ou absente du groupe");
					return;
				}
				state->ServerDinoOrderGroup_AddOrRemoveDinoCharacter_Implementation(index, cible, ajout);
				Reply(connection, packet, nlohmann::json{{"eosId", eos_id}, {"group", index + 1}, {"action", what}, {"key", cle}});
				Log::GetLog()->info("Groupe {} : {} {} pour {}", index + 1, what, cle, eos_id);
				return;
			}

			// setclass / removeclass : l'espece est retrouvee sur une creature reelle de la tribu
			// (on n'a donc pas a charger une classe par son chemin, ce qui serait plus risque)
			std::string espece;
			for (int i = 4; i < args.Num(); ++i) espece += (i > 4 ? " " : "") + ToUtf8(args[i]);
			if (espece.empty())
			{
				ReplyError(connection, packet, "Espece manquante");
				return;
			}

			UClass* classe = nullptr;
			for (AActor* actor : actors)
			{
				if (actor == nullptr || actor->TargetingTeamField() != tribe) continue;
				auto* dino = static_cast<APrimalDinoCharacter*>(actor);
				if (Lowercase(ToUtf8(dino->DescriptiveNameField())) == Lowercase(espece))
				{
					classe = dino->ClassField();
					break;
				}
			}
			if (classe == nullptr)
			{
				ReplyError(connection, packet, "Aucune creature de cette espece dans la tribu : " + espece);
				return;
			}

			AjouterOuRetirerClasse(state, index, classe, what == "setclass");
			Reply(connection, packet,
				nlohmann::json{{"eosId", eos_id}, {"group", index + 1}, {"species", espece}, {"action", what}});
			Log::GetLog()->info("Groupe {} : {} {} pour {}", index + 1, what, espece, eos_id);
			return;
		}

		if (args.Num() < 4)
		{
			ReplyError(connection, packet, "Ordre manquant (follow, stay, aggressive, passive, neutral, passiveflee, attack, land)");
			return;
		}

		int group = -1;
		if (what != "all")
		{
			group = std::atoi(what.c_str()) - 1;
			if (group < 0 || group > 9)
			{
				ReplyError(connection, packet, "Groupe invalide : 1 a 10, ou all");
				return;
			}
		}

		AShooterCharacter* character = pc->GetPlayerCharacter();
		if (character == nullptr)
		{
			ReplyError(connection, packet, "Personnage mort ou absent : aucun ordre possible");
			return;
		}

		const std::string order = ToUtf8(args[3]);
		const int previous = state->CurrentlySelectedDinoOrderGroupField();

		state->ServerSetSelectedDinoOrderGroup_Implementation(group);

		if (order == "follow") character->ServerCallFollow_Implementation();
		else if (order == "stay") character->ServerCallStay_Implementation();
		else if (order == "aggressive") character->ServerCallAggressive_Implementation();
		else if (order == "passive") character->ServerCallPassive_Implementation();
		else if (order == "neutral") character->ServerCallNeutral_Implementation();
		else if (order == "passiveflee") character->ServerCallPassiveFlee_Implementation();
		else if (order == "attack") character->ServerCallAttackTargetNew_Implementation();
		else if (order == "land")
		{
			// Atterrir : le jeu n'a pas d'ordre de groupe pour cela (touche CallLandOne = une creature visee). On l'envoie donc a
			// chaque creature de la tribu qui fait partie du groupe choisi (ou a toutes avec « all »).
			const int tribe = AsaApi::IApiUtils::GetTribeID(pc);
			const FVector origin{0, 0, 0};
			TArray<AActor*> actors =
				AsaApi::GetApiUtils().GetAllActorsInRange(origin, 1000000.f, EServerOctreeGroup::DINOPAWNS_TAMED);
			int envoyes = 0;
			for (AActor* actor : actors)
			{
				if (actor == nullptr || actor->TargetingTeamField() != tribe) continue;
				auto* dino = static_cast<APrimalDinoCharacter*>(actor);
				if (group >= 0 && !state->IsDinoInOrderGroup(group, dino)) continue;
				character->ServerCallLandFlyerOne_Implementation(dino);
				++envoyes;
			}
			Log::GetLog()->info("Atterrissage demande a {} creature(s)", envoyes);
		}
		else
		{
			state->ServerSetSelectedDinoOrderGroup_Implementation(previous);
			ReplyError(connection, packet, "Ordre inconnu : " + order);
			return;
		}

		Reply(connection, packet,
			nlohmann::json{{"eosId", eos_id},
			               {"group", what},
			               {"order", order},
			               {"previousSelection", previous},
			               {"selected", state->CurrentlySelectedDinoOrderGroupField()}});

		Log::GetLog()->info("Ordre de groupe pour {} : {} -> {}", eos_id, what, order);
	}
} // namespace QoL
