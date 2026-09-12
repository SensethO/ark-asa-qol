#pragma once

#include <string>
#include <vector>
#include <map>

namespace QoL
{
	struct KitItem
	{
		std::string blueprint;
		int amount = 1;
		float quality = 0.f;
		bool force_blueprint = false;
	};

	struct Kit
	{
		std::string name;
		std::string description;
		int cooldown_seconds = 0;
		int max_uses = -1; // -1 = unlimited
		std::vector<KitItem> items;
	};

	/** Bandeau d'annonce affiche a l'ecran de tous les joueurs */
	struct AnnounceSettings
	{
		bool enabled = true;
		/** Duree utilisee quand la commande n'en precise pas */
		float default_seconds = 60.f;
		/** Duree maximale acceptee, pour qu'une faute de frappe ne fige pas un bandeau */
		float max_seconds = 300.f;
		/** Taille du texte ; 1.0 correspond a la taille standard des messages serveur */
		float scale = 1.3f;
		float color_r = 1.f;
		float color_g = 0.85f;
		float color_b = 0.2f;
		float color_a = 1.f;
	};

	/** Fenetre d'inventaire en jeu, servie par le mod d'interface */
	struct WindowSettings
	{
		bool enabled = true;
		/**
		 * Chemin du blueprint singleton publie par le mod. Vide desactive le pont
		 * et force le repli textuel. A renseigner apres publication du mod.
		 */
		std::string blueprint_path;
		/** Nom de la fonction Blueprint appelee sur le singleton */
		std::string function_name = "ShowInventoryWindow";
		std::string title = "Inventaires";
		/** Rayon de collecte des coffres et montures, en unites Unreal */
		float radius = 30000.f;
	};

	/**
	 * Poids des objets.
	 *
	 * ARK n'offre aucun reglage de poids : seules les piles sont configurables.
	 * Le multiplicateur est donc applique par le plugin sur l'objet par defaut
	 * de chaque classe. Les valeurs d'origine sont memorisees pour qu'un second
	 * reglage parte d'elles et non du resultat du premier.
	 */
	struct WeightSettings
	{
		bool enabled = false;
		/** 0,1 allege de 90 pour cent ; 1 restitue le poids d'origine */
		double multiplier = 1.0;
		/** Secondes avant application au demarrage : les donnees de jeu ne sont pas pretes tout de suite */
		int startup_delay = 30;
	};

	/** Tranche de niveaux et sa part dans le tirage */
	struct WildLevelBand
	{
		int from = 1;
		int to = 10;
		/**
		 * Part relative de la tranche. Les parts sont normalisees au tirage :
		 * elles n'ont pas besoin de totaliser 100, ce qui evite qu'une saisie
		 * approximative desactive silencieusement des tranches.
		 */
		float percent = 0.f;
	};

	/**
	 * \brief Niveau impose aux creatures sauvages a leur apparition.
	 *
	 * ARK ne sait pas faire cela nativement : `OverrideOfficialDifficulty` ne
	 * fixe que le plafond, les creatures apparaissant ensuite de 1 a ce maximum,
	 * sans plancher ni ponderation.
	 */
	struct WildLevelSettings
	{
		bool enabled = false;
		int min_level = 1;
		int max_level = 150;
		/** Vide : tirage uniforme entre min_level et max_level */
		std::vector<WildLevelBand> bands;
	};

	struct TeleportRules
	{
		bool enabled = true;
		int cooldown_seconds = 300;
		int warmup_seconds = 10;
		bool allow_while_riding_dino = false;
	};

	struct Config
	{
		std::string sender_name = "Serveur";
		std::string prefix = "[QoL] ";

		TeleportRules homes;
		int max_homes = 3;

		TeleportRules tpa;
		int tpa_request_timeout_seconds = 60;

		bool kits_enabled = true;
		std::vector<Kit> kits;

		AnnounceSettings announce;
		WindowSettings window;
		WildLevelSettings wild_levels;
		WeightSettings weight;

		std::map<std::string, std::string> messages;

		/**
		 * \brief Looks up a message by key, falling back to the key itself if it was removed from config
		 */
		const std::string& Msg(const std::string& key) const;
	};

	/**
	 * \brief Loads config.json from the plugin directory, writing a default file if none exists.
	 * \return false if the file exists but could not be parsed (the server keeps running with defaults)
	 */
	bool LoadConfig();

	const Config& GetConfig();

	/**
	 * \brief Absolute path to a file inside this plugin's directory
	 */
	std::string PluginFilePath(const std::string& filename);
} // namespace QoL
