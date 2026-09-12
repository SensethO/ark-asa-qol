#include "Window.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "json.hpp"
#include "Logger/Logger.h"

#include "Config.h"
#include "Link.h"
#include "Inspect.h"
#include "Util.h"

namespace QoL
{
	namespace
	{
		/** Reference faible vers l'acteur singleton publie par le mod */
		TWeakObjectPtr<AActor> g_singleton;

		/**
		 * \brief En-tete d'une UFunction, lu aux offsets reels d'ARK.
		 *
		 * Le `UFunction` du SDK declare ses champs a la suite d'un `UStruct` qui
		 * n'en porte aucun — tout y passe par recherche de nom. `FunctionFlags`
		 * tombe donc a l'offset 0, c'est-a-dire sur le pointeur de table virtuelle :
		 * ce qu'on en lisait n'etait que la moitie basse d'une adresse, identique
		 * pour toutes les fonctions.
		 *
		 * Les offsets ci-dessous ont ete releves sur un evenement de signature
		 * connue — un seul parametre String, soit 1 parametre et 16 octets. Pour les
		 * retrouver apres une mise a jour d'ARK : vider 240 octets depuis la base de
		 * l'objet et chercher la suite `01 00 10 00` precedee de drapeaux plausibles.
		 */
		struct FunctionHeader
		{
			unsigned int flags = 0;
			unsigned int num_parms = 0;
			unsigned int parms_size = 0;
			unsigned int return_value_offset = 0;
			bool coherent = false;
		};

		FunctionHeader ReadFunctionHeader(UFunction* function)
		{
			constexpr size_t kFlags = 0xB0;
			constexpr size_t kNumParms = 0xB4;
			constexpr size_t kParmsSize = 0xB6;
			constexpr size_t kReturnValueOffset = 0xB8;

			const unsigned char* base = reinterpret_cast<const unsigned char*>(function);

			FunctionHeader header;
			header.flags = *reinterpret_cast<const unsigned int*>(base + kFlags);
			header.num_parms = base[kNumParms];
			header.parms_size = *reinterpret_cast<const unsigned short*>(base + kParmsSize);
			header.return_value_offset = *reinterpret_cast<const unsigned short*>(base + kReturnValueOffset);

			// Une mise a jour d'ARK peut deplacer ces champs. Plutot que de refuser des
			// appels sur des valeurs devenues fantaisistes, on juge leur coherence : si
			// elle tombe, le garde-fou se met en retrait au lieu de bloquer a tort.
			header.coherent = header.num_parms <= 32
			               && header.parms_size <= 1024
			               && (header.num_parms == 0) == (header.parms_size == 0)
			               && (header.return_value_offset == 0xFFFF
			                   || header.return_value_offset < header.parms_size);
			return header;
		}
		/**
		 * \brief Charge la classe d'un blueprint, quelle que soit la forme du chemin.
		 *
		 * Le Dev Kit livre une reference vers l'objet blueprint
		 * (`...Nom.Nom'`), alors que la classe reellement instanciable porte le
		 * suffixe `_C`. Selon les cas, `BPLoadClass` accepte l'une ou l'autre : les
		 * deux sont donc tentees, faute de quoi un chemin correct pourrait echouer
		 * sans autre explication qu'un « classe introuvable ».
		 */
		UClass* LoadBlueprintClass(const std::string& path)
		{
			if (UClass* found = UVictoryCore::BPLoadClass(ToFString(path))) return found;

			// Insertion de `_C` avant l'apostrophe finale, la forme etant
			// `Blueprint'/Chemin/Nom.Nom'`
			const size_t closing = path.rfind('\'');
			if (closing == std::string::npos || closing == 0) return nullptr;

			const std::string suffixed = path.substr(0, closing) + "_C" + path.substr(closing);
			return UVictoryCore::BPLoadClass(ToFString(suffixed));
		}

		/**
		 * \brief Retrouve l'acteur singleton du mod d'interface.
		 *
		 * Meme mecanisme que AsaApiModUtils : le mod publie un blueprint a un
		 * chemin connu, le plugin le charge et appelle ses fonctions. Le chemin
		 * est configurable, car il depend du nom sous lequel le mod est publie.
		 */
		AActor* GetModSingleton()
		{
			if (g_singleton) return g_singleton.Get();

			const std::string& path = GetConfig().window.blueprint_path;
			if (path.empty()) return nullptr;

			UClass* singleton_class = LoadBlueprintClass(path);
			if (singleton_class == nullptr) return nullptr;

			TArray<AActor*> actors;
			UGameplayStatics::GetAllActorsOfClass(AsaApi::GetApiUtils().GetWorld(), singleton_class, &actors);
			if (actors.Num() == 0) return nullptr;

			g_singleton = GetWeakReference(actors[0]);
			return g_singleton.Get();
		}

		void AddRow(std::vector<WindowRow>& rows, const char* tab, const std::string& group,
			const std::string& name, int quantity, const FVector* position, const std::string& detail)
		{
			WindowRow row;
			row.Tab = ToFString(tab);
			row.Group = ToFString(group);
			row.Name = ToFString(name);
			row.Quantity = quantity;
			row.Detail = ToFString(detail);
			row.Lat = 0.0;
			row.Lon = 0.0;

			if (position != nullptr)
			{
				const AsaApi::MapCoords coords = AsaApi::GetApiUtils().FVectorToCoords(*position);
				row.Lat = coords.y;
				row.Lon = coords.x;
			}

			rows.push_back(row);
		}

		/**
		 * rief Nom affiche d'un contenant ou d'une creature.
		 *
		 * Trois niveaux, du plus parlant au plus brut : le nom que le joueur a
		 * donne, puis le nom descriptif du jeu, puis le nom de classe. Ce
		 * dernier etait jusqu'ici le seul employe, et c'est lui qui affichait
		 * « Phiomia_Character_BP » la ou le joueur attend « Le Para ».
		 *
		 * `BoxName` n'est pas declare par le SDK : il faut le lire par son nom,
		 * comme le fait le SDK lui-meme pour tous ses champs.
		 */
		std::string DisplayName(const FString& custom, const FString& descriptive, AActor* actor)
		{
			std::string nom = ToUtf8(custom);
			if (!nom.empty()) return nom;

			nom = ToUtf8(descriptive);
			if (!nom.empty()) return nom;

			return ShortName(ToUtf8(AsaApi::IApiUtils::GetBlueprint(actor)));
		}

		/** Nom personnalise d'un coffre, absent du SDK donc lu par son nom */
		const FString& BoxName(APrimalStructureItemContainer* container)
		{
			return *GetNativePointerField<FString*>(container, "APrimalStructureItemContainer.BoxName");
		}

		/** Ajoute l'inventaire porte par un joueur, cosmetiques exclus */
		void AddAvatarRows(std::vector<WindowRow>& rows, AShooterPlayerController* pc)
		{
			AShooterCharacter* character = PlayerCharacter(pc);
			if (character == nullptr || character->bIsDead()()) return;

			UPrimalInventoryComponent* inventory = character->MyInventoryComponentField();
			if (inventory == nullptr) return;

			const std::string owner = ToUtf8(AsaApi::IApiUtils::GetCharacterName(pc));
			const FVector position = AsaApi::IApiUtils::GetPosition(pc);

			for (UPrimalItem* item : inventory->InventoryItemsField())
			{
				if (item == nullptr) continue;

				const ItemLine line = ClassifyItem(item);
				// Engrammes et skins sont exclus : ce ne sont pas des affaires portees
				if (line.engram || line.skin) continue;

				AddRow(rows, "Avatars", owner, line.name, line.quantity, &position, line.item_class);
			}
		}
	} // namespace

	bool IsTribeOwner(AShooterPlayerController* player)
	{
		if (player == nullptr) return false;

		// Un joueur sans tribu est maitre de ses seules affaires : traite comme chef
		if (AsaApi::IApiUtils::GetTribeID(player) == 0) return true;

		auto* state = static_cast<AShooterPlayerState*>(player->PlayerStateField().Get());
		if (state == nullptr) return false;

		const unsigned int owner_id = state->MyTribeDataField().OwnerPlayerDataIDField();
		return owner_id != 0 && owner_id == static_cast<unsigned int>(AsaApi::IApiUtils::GetPlayerID(player));
	}

	/**
 * \brief Regroupe par contenant et compose la ligne affichee.
 *
 * Le widget se contente d'afficher `Name` : toute la mise en forme est faite
 * ici. Ce choix evite de republier le mod a chaque retouche de presentation —
 * une modification du C++ se recharge a chaud en deux minutes, quand un
 * changement de Blueprint demande une cuisson, une publication et un
 * redemarrage.
 *
 * Un en-tete est insere avant chaque contenant, portant son nom et ses
 * coordonnees. Il conserve le `Tab` de son groupe, sans quoi le filtrage par
 * onglet le laisserait orphelin en tete d'une liste vide.
 */
void ComposeDisplay(std::vector<WindowRow>& rows, const std::string& term, const std::string& opened)
{
		// Tri par onglet, puis contenant, puis nom : c'est ce qui rend le
		// regroupement possible en une seule passe
		std::stable_sort(rows.begin(), rows.end(), [](const WindowRow& a, const WindowRow& b)
		{
			const int tab = ToUtf8(a.Tab).compare(ToUtf8(b.Tab));
			if (tab != 0) return tab < 0;
			const int group = ToUtf8(a.Group).compare(ToUtf8(b.Group));
			if (group != 0) return group < 0;
			return ToUtf8(a.Name).compare(ToUtf8(b.Name)) < 0;
		});

		const std::string besoin = Lowercase(term);

		std::vector<WindowRow> composed;
		composed.reserve(rows.size() + 32);

		// Les lignes triees forment des suites contigues par contenant : une
		// seule passe suffit, en reperant les bornes de chaque suite.
		size_t i = 0;
		while (i < rows.size())
		{
			const std::string tab = ToUtf8(rows[i].Tab);
			const std::string group = ToUtf8(rows[i].Group);

			size_t fin_groupe = i;
			while (fin_groupe < rows.size()
				&& ToUtf8(rows[fin_groupe].Tab) == tab
				&& ToUtf8(rows[fin_groupe].Group) == group)
			{
				++fin_groupe;
			}

			// Une recherche ne garde que les contenants qui portent l'objet, et
			// les ouvre d'office : replier ce qu'on vient de trouver obligerait
			// a cliquer chaque resultat pour le lire.
			std::vector<const WindowRow*> retenues;
			for (size_t j = i; j < fin_groupe; ++j)
			{
				if (besoin.empty() || Lowercase(ToUtf8(rows[j].Name)).find(besoin) != std::string::npos)
				{
					retenues.push_back(&rows[j]);
				}
			}

			if (retenues.empty())
			{
				i = fin_groupe;
				continue;
			}

			const bool ouvert = !besoin.empty() || group == opened;

			char entete[320];
			snprintf(entete, sizeof(entete), "%s %s  (%.1f ; %.1f)  -  %d objet%s",
				ouvert ? "[-]" : "[+]", group.c_str(), rows[i].Lat, rows[i].Lon,
				static_cast<int>(retenues.size()), retenues.size() > 1 ? "s" : "");

			WindowRow header = rows[i];
			header.Name = ToFString(entete);
			header.Quantity = 0;
			composed.push_back(header);

			if (ouvert)
			{
				for (const WindowRow* row : retenues)
				{
					char ligne[320];
					snprintf(ligne, sizeof(ligne), "    %s x%d",
						ToUtf8(row->Name).c_str(), row->Quantity);

					WindowRow entry = *row;
					entry.Name = ToFString(ligne);
					composed.push_back(entry);
				}
			}

			i = fin_groupe;
		}

		rows.swap(composed);
	}

	std::vector<WindowRow> BuildWindowRows(AShooterPlayerController* viewer, float radius,
		const std::string& term, const std::string& opened)
	{
		std::vector<WindowRow> rows;
		if (viewer == nullptr) return rows;

		// Le demandeur voit toujours son propre inventaire
		AddAvatarRows(rows, viewer);

		// Le controle d'acces est applique ICI, cote serveur. Un mod client est
		// modifiable par un joueur : il ne doit jamais recevoir de donnees qu'il
		// n'a pas le droit de voir, filtrer dans le widget serait une faille.
		if (!IsTribeOwner(viewer)) return rows;

		const int tribe = AsaApi::IApiUtils::GetTribeID(viewer);
		const FVector center = AsaApi::IApiUtils::GetPosition(viewer);
		const std::string viewer_eos = ToUtf8(AsaApi::IApiUtils::GetEOSIDFromController(viewer));

		// Avatars des autres membres connectes
		for (TWeakObjectPtr<APlayerController> controller :
			AsaApi::GetApiUtils().GetWorld()->PlayerControllerListField())
		{
			auto* other = static_cast<AShooterPlayerController*>(controller.Get());
			if (other == nullptr || other == viewer) continue;
			if (AsaApi::IApiUtils::GetTribeID(other) != tribe) continue;

			AddAvatarRows(rows, other);
		}

		// Coffres et structures a inventaire
		for (AActor* actor :
			AsaApi::GetApiUtils().GetAllActorsInRange(center, radius, EServerOctreeGroup::STRUCTURES))
		{
			if (actor == nullptr || actor->TargetingTeamField() != tribe) continue;
			if (!IsContainerClass(actor->ClassPrivateField())) continue;

			auto* container = static_cast<APrimalStructureItemContainer*>(actor);
			UPrimalInventoryComponent* inventory = container->MyInventoryComponentField();
			if (inventory == nullptr) continue;

			const std::string label =
				DisplayName(BoxName(container), container->DescriptiveNameField(), actor);
			const FVector position = ActorPosition(actor);

			for (UPrimalItem* item : inventory->InventoryItemsField())
			{
				if (item == nullptr) continue;

				const ItemLine line = ClassifyItem(item);
				if (line.engram) continue;

				AddRow(rows, "Coffres", label, line.name, line.quantity, &position, line.item_class);
			}
		}

		// Inventaires des montures apprivoisees
		for (AActor* actor :
			AsaApi::GetApiUtils().GetAllActorsInRange(center, radius, EServerOctreeGroup::DINOPAWNS_TAMED))
		{
			if (actor == nullptr || actor->TargetingTeamField() != tribe) continue;

			auto* dino = static_cast<APrimalDinoCharacter*>(actor);
			UPrimalInventoryComponent* inventory = dino->MyInventoryComponentField();
			if (inventory == nullptr) continue;

			const std::string label =
				DisplayName(dino->TamedNameField(), dino->DescriptiveNameField(), actor);
			const FVector position = ActorPosition(actor);

			for (UPrimalItem* item : inventory->InventoryItemsField())
			{
				if (item == nullptr) continue;

				const ItemLine line = ClassifyItem(item);
				if (line.engram) continue;

				AddRow(rows, "Montures", label, line.name, line.quantity, &position, line.item_class);
			}
		}

		ComposeDisplay(rows, term, opened);
		return rows;
	}

	bool SendToModWindow(AShooterPlayerController* viewer, const std::vector<WindowRow>& rows)
	{
		// Le relais du joueur lui appartient deja : rien a reattribuer, et aucun
		// risque de deposseder un autre joueur du sien.
		if (AActor* link = LinkFor(viewer)) return SendWindowOn(link, rows);

		AActor* singleton = GetModSingleton();
		if (singleton == nullptr) return false;

		// L'acteur place dans le monde n'a pas de proprietaire : une RPC « client
		// proprietaire » n'irait nulle part. Le poser sur le controleur du
		// spectateur dirige l'appel vers lui, et vers lui seul. Ce detour
		// disparait des que le joueur dispose de son relais, qui lui appartient
		// deja — voir Link.cpp.
		singleton->SetOwner(viewer);
		return SendWindowOn(singleton, rows);
	}

	bool SendWindowOn(AActor* target, const std::vector<WindowRow>& rows)
	{
		AActor* singleton = target;
		if (singleton == nullptr) return false;

		ShowInventoryWindow_Params params;
		params.Title = ToFString(GetConfig().window.title);
		for (const WindowRow& row : rows) params.Rows.Add(row);

		// FName ne dispose que du constructeur etroit dans AsaApi : la version
		// large n'est pas exportee par la bibliotheque
		const FName function_name(GetConfig().window.function_name.c_str());

		// La signature Blueprint doit correspondre exactement au struct, champ par
		// champ : un decalage provoque une corruption memoire, pas une erreur claire
		UFunction* function = singleton->ClassField()->FindFunctionByName(function_name, EIncludeSuperFlag::IncludeSuper);
		if (function == nullptr) return false;

		// Meme garde que le diagnostic : le moteur recopie `ParmsSize` octets depuis
		// ce tampon. S'il en attend plus qu'il n'y en a, il lit au-dela et corrompt
		// la memoire du serveur sans le moindre message.
		const FunctionHeader header = ReadFunctionHeader(function);
		if (header.coherent && header.parms_size > sizeof(ShowInventoryWindow_Params))
		{
			Log::GetLog()->error("Signature de {} desaccordee : le blueprint attend {} octets, le plugin en fournit {}",
				GetConfig().window.function_name, header.parms_size, sizeof(ShowInventoryWindow_Params));
			return false;
		}

		singleton->ProcessEvent(function, &params);
		return true;
	}

	void CmdInventoryWindow(AShooterPlayerController* player, FString* message, int, int)
	{
		const Config& config = GetConfig();
		if (!config.window.enabled)
		{
			SendMsg(player, config.Msg("NoPermission"));
			return;
		}

		const std::vector<WindowRow> rows = BuildWindowRows(player, config.window.radius);
		const bool owner = IsTribeOwner(player);

		if (SendToModWindow(player, rows))
		{
			Log::GetLog()->info("Fenetre ouverte pour {} ({} lignes, chef={})",
				ToUtf8(AsaApi::IApiUtils::GetCharacterName(player)), rows.size(), owner);
			return;
		}

		// Repli tant que le mod d'interface n'est pas installe : meme donnees,
		// meme controle d'acces, presentation textuelle paginee
		const TArray<FString> args = SplitArgs(message);
		int page = 1;
		if (args.Num() > 1)
		{
			const std::string raw = ToUtf8(args[1]);
			if (!raw.empty() && raw.find_first_not_of("0123456789") == std::string::npos)
			{
				// Parentheses obligatoires : Windows.h definit des macros min/max
				// qui casseraient l'appel a std::max
				page = (std::max)(1, std::atoi(raw.c_str()));
			}
		}

		constexpr int kPerPage = 12;
		const int total = static_cast<int>(rows.size());
		const int pages = (std::max)(1, (total + kPerPage - 1) / kPerPage);
		page = (std::min)(page, pages);

		SendMsg(player, owner ? "Inventaires de la tribu (vous etes chef)" : "Votre inventaire");

		if (total == 0)
		{
			SendMsg(player, "Rien a afficher.");
			return;
		}

		const int last = (std::min)(page * kPerPage, total);
		for (int i = (page - 1) * kPerPage; i < last; ++i)
		{
			const WindowRow& row = rows[static_cast<size_t>(i)];

			std::string line = "[" + ToUtf8(row.Tab) + "] " + ToUtf8(row.Group) + " - " + ToUtf8(row.Name)
				+ " x" + std::to_string(row.Quantity);

			if (row.Lat != 0.0 || row.Lon != 0.0)
			{
				char coords[48];
				snprintf(coords, sizeof coords, "  (%.1f / %.1f)", row.Lat, row.Lon);
				line += coords;
			}

			SendMsg(player, line);
		}

		if (pages > 1)
		{
			SendMsg(player, "Page " + std::to_string(page) + "/" + std::to_string(pages)
				+ " - tapez /inv " + std::to_string((std::min)(page + 1, pages)) + " pour la suite.");
		}
	}

	void RconWindowTest(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
	{
		TArray<FString> args;
		packet->Body.ParseIntoArray(args, L" ", true);

		const std::string function_name = args.Num() > 1 ? ToUtf8(args[1]) : "QoLTest";

		std::string target_eos;
		bool force_replication = false;
		int value = 0;
		bool as_row = false;
		bool as_rows = false;
		std::string message = "Pont AsaQoL operationnel";
		if (args.Num() > 2)
		{
			message.clear();
			for (int i = 2; i < args.Num(); ++i)
			{
				const std::string arg = ToUtf8(args[i]);

				// `for=<eosId>` designe le joueur a qui destiner l'appel : il n'entre
				// pas dans le message
				if (arg.rfind("for=", 0) == 0)
				{
					target_eos = arg.substr(4);
					continue;
				}

				if (Lowercase(arg) == "replicate")
				{
					force_replication = true;
					continue;
				}

				// `rows` va jusqu'au bout : titre plus tableau de structures, soit la
				// signature reelle de la fenetre d'inventaire
				if (Lowercase(arg) == "rows")
				{
					as_rows = true;
					continue;
				}

				// `row` bascule sur la structure complete : c'est l'etape qui met
				// l'alignement a l'epreuve, les deux double imposant un bourrage
				if (Lowercase(arg) == "row")
				{
					as_row = true;
					continue;
				}

				// `value=<n>` alimente le second champ, l'entier : c'est l'etape
				// qui valide qu'une structure a plusieurs champs traverse le pont
				if (arg.rfind("value=", 0) == 0)
				{
					value = std::atoi(arg.substr(6).c_str());
					continue;
				}

				if (!message.empty()) message += " ";
				message += arg;
			}
		}

		const std::string& path = GetConfig().window.blueprint_path;

		nlohmann::json report{
			{"blueprintPath", path},
			{"function", function_name},
			{"message", message},
			{"pathConfigured", !path.empty()},
			{"classLoaded", false},
			{"actorsFound", 0},
			{"functionFound", false},
			{"called", false},
		};

		const auto reply = [&]
		{
			FString response = ToFString(report.dump());
			connection->SendMessageW(packet->Id, 0, &response);
		};

		if (path.empty())
		{
			report["hint"] = "Renseignez Window.BlueprintPath dans config.json, puis relancez avec 'reload'";
			return reply();
		}

		UClass* singleton_class = LoadBlueprintClass(path);
		if (singleton_class == nullptr)
		{
			report["hint"] = "Classe introuvable. Si le chemin est juste, c'est que le mod n'est pas charge "
			                 "par le serveur : verifiez qu'il est cuisine et present dans -mods=";
			return reply();
		}
		report["classLoaded"] = true;

		TArray<AActor*> actors;
		UGameplayStatics::GetAllActorsOfClass(AsaApi::GetApiUtils().GetWorld(), singleton_class, &actors);
		report["actorsFound"] = actors.Num();

		if (actors.Num() == 0)
		{
			// La classe existe mais aucune instance ne vit dans le monde : le
			// blueprint n'est pas place sur la carte ou n'est pas charge par le mod
			report["hint"] = "Classe chargee mais aucun acteur dans le monde : le singleton doit etre place ou instancie par le mod";
			return reply();
		}

		// Une RPC « client proprietaire » ne s'execute que sur la connexion qui
		// possede l'acteur. Un singleton cree par le serveur n'a pas de
		// proprietaire : l'appel ne partait donc nulle part. Le poser sur le
		// controleur du joueur vise dirige l'appel vers lui, et vers lui seul —
		// ce qui vaut bien mieux qu'un multicast, qui livrerait a tous.
		AShooterPlayerController* owner = nullptr;
		if (!target_eos.empty())
		{
			owner = AsaApi::GetApiUtils().FindPlayerFromEOSID(ToFString(target_eos));
			report["ownerFound"] = owner != nullptr;

			if (owner != nullptr)
			{
				actors[0]->SetOwner(owner);
				report["ownerSet"] = true;
			}
		}

		// Sans replication, l'acteur n'existe pas chez le client : aucune RPC ne
		// peut l'atteindre, meme avec le bon proprietaire. Ces deux drapeaux se
		// posent normalement dans le Blueprint, mais les lire — et au besoin les
		// forcer — evite d'attendre un Dev Kit en panne pour trancher.
		report["replicates"] = actors[0]->bReplicates()();
		report["alwaysRelevant"] = actors[0]->bAlwaysRelevant()();

		// Une RPC ne s'ecrit que dans une connexion reseau. Faute de connexion,
		// Unreal ne signale rien : il execute la fonction sur place, cote serveur,
		// ou « Create Widget » n'a aucun ecran ou s'afficher. Ces trois valeurs
		// distinguent « l'appel est parti » de « l'appel a fait un tour sur lui-meme ».
		report["netConnection"] = actors[0]->GetNetConnection() != nullptr;
		report["onlyRelevantToOwner"] = actors[0]->bOnlyRelevantToOwner()();
		if (owner != nullptr) report["ownerNetConnection"] = owner->GetNetConnection() != nullptr;

		if (force_replication)
		{
			actors[0]->SetReplicates(true);
			actors[0]->bAlwaysRelevant() = true;
			report["replicationForced"] = true;
			report["replicatesAfter"] = actors[0]->bReplicates()();
			report["alwaysRelevantAfter"] = actors[0]->bAlwaysRelevant()();
		}

		const FName function(function_name.c_str());
		UFunction* target = actors[0]->ClassField()->FindFunctionByName(function, EIncludeSuperFlag::IncludeSuper);
		if (target == nullptr)
		{
			report["hint"] = "Fonction absente du blueprint : verifiez son nom exact";
			return reply();
		}
		report["functionFound"] = true;

		// La signature reelle de la fonction Blueprint, lue a la bonne adresse. Un
		// evenement personnalise nait « non replique » et ce reglage ne se voit que
		// dans son panneau Details : le lire ici evite d'ouvrir le Dev Kit.
		const FunctionHeader header = ReadFunctionHeader(target);
		report["flags"] = header.flags;
		report["headerCoherent"] = header.coherent;
		report["numParms"] = header.num_parms;
		report["parmsSize"] = header.parms_size;
		report["isNet"] = (header.flags & 0x00000040u) != 0;        // FUNC_Net
		report["isReliable"] = (header.flags & 0x00000080u) != 0;   // FUNC_NetReliable
		report["isMulticast"] = (header.flags & 0x00004000u) != 0;  // FUNC_NetMulticast
		report["isNetClient"] = (header.flags & 0x01000000u) != 0;  // FUNC_NetClient
		report["isNetServer"] = (header.flags & 0x00200000u) != 0;  // FUNC_NetServer

		// Le moteur recopie `ParmsSize` octets depuis le tampon fourni. En fournir
		// moins qu'annonce le fait lire au-dela : c'est ainsi qu'une signature
		// Blueprint desaccordee corrompt la memoire du serveur sans le moindre
		// message. En fournir plus est sans danger.
		const size_t provided = as_rows ? sizeof(ShowInventoryWindow_Params)
		                      : as_row ? sizeof(QoLRow_Params)
		                                : sizeof(QoLTest_Params);
		report["sizeofParams"] = static_cast<unsigned int>(provided);

		if (header.coherent && header.parms_size > provided)
		{
			report["hint"] = "La fonction Blueprint attend " + std::to_string(header.parms_size)
			               + " octets de parametres, le plugin n'en fournit que "
			               + std::to_string(provided)
			               + " : appel refuse, il lirait de la memoire arbitraire";
			return reply();
		}

		if (as_rows)
		{
			// Trois lignes suffisent a prouver le transport d'un tableau : la
			// premiere et la derniere encadrent, celle du milieu revele une erreur
			// de pas entre elements, qu'un tableau d'une seule ligne cacherait
			ShowInventoryWindow_Params window;
			window.Title = ToFString(message);
			for (int i = 0; i < 3; ++i)
			{
				WindowRow row;
				row.Tab = ToFString("Avatars");
				row.Group = ToFString("Petra");
				row.Name = ToFString("Ligne " + std::to_string(i + 1));
				row.Quantity = value + i;
				row.Lat = 12.5 + i;
				row.Lon = -34.25 - i;
				row.Detail = ToFString("Detail " + std::to_string(i + 1));
				window.Rows.Add(row);
			}
			actors[0]->ProcessEvent(target, &window);
		}
		else if (as_row)
		{
			// Valeurs temoins choisies pour se reconnaitre a l'ecran : un entier et
			// deux reels a decimales, seuls capables de reveler un decalage d'octets
			QoLRow_Params row;
			row.Row.Tab = ToFString("Avatars");
			row.Row.Group = ToFString("Petra");
			row.Row.Name = ToFString(message);
			row.Row.Quantity = value;
			row.Row.Lat = 12.5;
			row.Row.Lon = -34.25;
			row.Row.Detail = ToFString("Detail");
			actors[0]->ProcessEvent(target, &row);
		}
		else
		{
			QoLTest_Params params;
			params.Message = ToFString(message);
			params.Value = value;
			actors[0]->ProcessEvent(target, &params);
		}

		report["called"] = true;
		report["value"] = value;
		report["asRow"] = as_row;
		report["asRows"] = as_rows;
		reply();

		Log::GetLog()->info("Test du pont mod : {} appelee avec '{}'", function_name, message);
	}
} // namespace QoL
