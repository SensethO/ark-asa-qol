#pragma once

namespace QoL
{
	/**
	 * \brief Registers every chat command and the periodic save/expiry tick
	 */
	void RegisterCommands();

	/**
	 * \brief Removes everything RegisterCommands installed. Must run before the DLL unloads.
	 */
	void UnregisterCommands();
} // namespace QoL
