#pragma once

#include <string>
#include <vector>

#include "API/ARK/Ark.h"

namespace QoL
{
	/**
	 * \brief Une ligne de la fenetre d'inventaire.
	 *
	 * Volontairement plate : Blueprint manipule tres bien un tableau de structures
	 * simples, alors qu'une hierarchie imbriquee serait penible a reproduire et
	 * fragile a aligner. Les onglets et les regroupements sont reconstruits cote
	 * widget a partir des champs Tab et Group.
	 *
	 * L'ordre et les types des champs doivent correspondre EXACTEMENT a la
	 * structure Blueprint du mod. Voir docs/specification-mod.md.
	 */
	struct WindowRow
	{
		FString Tab;      // Onglet : "Avatars", "Coffres", "Montures", "Constructions"
		FString Group;    // Regroupement : nom du joueur ou du contenant
		FString Name;     // Nom de l'objet ou de l'element
		int Quantity;     // Quantite ; 0 pour une ligne qui n'en porte pas
		double Lat;       // Coordonnee carte, 0 si non applicable
		double Lon;
		FString Detail;   // Nom de classe ou complement d'information
	};

	/**
	 * \brief Parametres de l'appel Blueprint ShowInventoryWindow.
	 *
	 * L'ordre des champs reproduit celui des parametres de la fonction.
	 *
	 * Il n'y a plus de liste de destinataires : l'appel est une RPC « client
	 * proprietaire », dirigee vers le seul joueur pose comme proprietaire de
	 * l'acteur juste avant l'envoi. Transmettre des destinataires n'aurait servi
	 * qu'a filtrer cote widget — c'est-a-dire dans un mod que le joueur peut
	 * modifier, donc nulle part.
	 */
	struct ShowInventoryWindow_Params
	{
		FString Title;
		TArray<WindowRow> Rows;
	};

	/** True si le joueur est le proprietaire de sa tribu */
	bool IsTribeOwner(AShooterPlayerController* player);

	/**
	 * \brief Assemble les lignes visibles par ce joueur, selon ses droits.
	 *
	 * Un membre simple ne recoit que son propre avatar. Le proprietaire de la
	 * tribu recoit en plus les avatars des autres membres connectes, les
	 * contenants et les constructions de la tribu.
	 */
	std::vector<WindowRow> BuildWindowRows(AShooterPlayerController* viewer, float radius,
		const std::string& term = "", const std::string& opened = "");

	/**
	 * \brief Envoie les lignes au mod pour affichage.
	 * \return false si le mod n'est pas installe : l'appelant se rabat sur le chat.
	 */
	bool SendToModWindow(AShooterPlayerController* viewer, const std::vector<WindowRow>& rows,
		const char* function_name = nullptr);

	/** Meme envoi, mais dirige vers un acteur precis : le relais du joueur */
	bool SendWindowOn(AActor* target, const std::vector<WindowRow>& rows,
		const char* function_name = nullptr);

	/**
	 * rief L'evenement que `/inv` declenche : bascule, plutot qu'affichage.
	 *
	 * `ShowInventoryWindow` affiche ou rafraichit, et ne ferme jamais — c'est ce
	 * qui a corrige le defaut ou chaque clic faisait disparaitre la fenetre.
	 * Restait a rendre une sortie au joueur. Elle est confiee au client, seul a
	 * savoir si sa fenetre est ouverte : le serveur n'a aucun moyen de
	 * l'interroger, et tenir cet etat ici aurait exige une notification en
	 * retour, donc une occasion de plus de se desynchroniser.
	 */
	constexpr const char* kToggleFunction = "Basculer";

	/** Commande de chat ouvrant la fenetre, avec repli textuel */
	void CmdInventoryWindow(AShooterPlayerController* player, FString* message, int, int);

	/**
	 * rief Parametres du pont de validation : un String, puis un entier.
	 *
	 * Envoyer plus de champs que la fonction Blueprint n'en declare est sans
	 * danger — le moteur ne lit que `ParmsSize` octets. C'est l'inverse qui
	 * corrompt la memoire. Ce struct est donc toujours le plus large des deux,
	 * et il reste compatible avec un evenement resté a un seul parametre.
	 */
	struct QoLTest_Params
	{
		FString Message;
		int Value;
	};

	/**
	 * \brief Parametres d'un appel a une fonction prenant une seule WindowRow.
	 *
	 * Etape suivante du pont : la structure complete, alignement compris. Les deux
	 * `double` imposent un alignement sur 8 octets, et donc un bourrage apres
	 * `Quantity` que Blueprint doit reproduire a l'identique. `qol.windowtest`
	 * compare la taille annoncee par le Blueprint a celle-ci avant d'appeler.
	 */
	struct QoLRow_Params
	{
		WindowRow Row;
	};
	/**
	 * \brief `qol.windowtest [fonction] [message]` — diagnostic du pont vers le mod.
	 *
	 * Un decalage entre la signature Blueprint et le struct C++ ne produit aucune
	 * erreur : il fait lire de la memoire arbitraire. La mise au point se fait donc
	 * par etapes, en commencant par une fonction a un seul parametre String. Cette
	 * commande rapporte l'issue de chaque etape separement — chemin configure,
	 * classe chargee, acteur present, fonction trouvee, appel effectue — pour que
	 * l'etape qui echoue soit nommee au lieu d'etre devinee.
	 */
	void RconWindowTest(RCONClientConnection* connection, RCONPacket* packet, UWorld* world);
} // namespace QoL
