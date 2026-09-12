#include "Ceiling.h"

#include <map>
#include <string>

#include "Logger/Logger.h"

#include "Inspect.h"
#include "Util.h"

namespace QoL
{
	namespace
	{
		/**
		 * Les deux morts relevees dans le journal de tribu se sont produites a
		 * Z = 58 143 et Z = 59 397. Le plafond garde donc une marge de plus de
		 * cinq mille unites, et la redescente ramene assez bas pour qu'un vol
		 * ascendant ne redeclenche pas le garde-fou a chaque seconde.
		 */
		constexpr double kCeiling = 53000.0;
		constexpr double kSafe = 50000.0;

		/** Un rappel par joueur toutes les trente secondes, pas a chaque tour */
		std::map<std::string, int64_t> g_last_warning;
	} // namespace

	void EnforceCeiling()
	{
		UWorld* world = AsaApi::GetApiUtils().GetWorld();
		if (world == nullptr) return;

		for (TWeakObjectPtr<APlayerController> controller : world->PlayerControllerListField())
		{
			auto* pc = static_cast<AShooterPlayerController*>(controller.Get());
			if (pc == nullptr) continue;

			AShooterCharacter* character = PlayerCharacter(pc);
			if (character == nullptr || character->bIsDead()()) continue;

			// Sur une monture, c'est elle qu'il faut redescendre : deplacer le
			// seul cavalier le laisserait tomber de plusieurs kilometres, ce qui
			// serait un remede pire que le mal.
			AActor* target = character;
			if (APrimalDinoCharacter* mount = character->RidingDinoField().Get())
			{
				target = mount;
			}

			FVector position = ActorPosition(target);
			if (position.Z < kCeiling) continue;

			position.Z = kSafe;

			// `SetActorLocation` plutot que `TeleportTo` : elle ne touche pas a
			// l'orientation, donc ni la vue du joueur ni le cap de sa monture.
			target->SetActorLocation(&position, false, nullptr, static_cast<ETeleportType>(1));

			const std::string eos = ToUtf8(AsaApi::IApiUtils::GetEOSIDFromController(pc));
			const int64_t now = Now();
			if (now - g_last_warning[eos] >= 30)
			{
				g_last_warning[eos] = now;
				SendMsg(pc, "Altitude limite : vous auriez ete detruit par la barriere du monde.");
				Log::GetLog()->info("Plafond applique pour {} (Z={:.0f})", eos, position.Z);
			}
		}
	}
} // namespace QoL
