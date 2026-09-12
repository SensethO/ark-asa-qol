#include "Util.h"

#include "Config.h"

namespace QoL
{
	void SendMsg(AShooterPlayerController* player, const std::string& text)
	{
		if (player == nullptr)
			return;

		const std::wstring line = ToWide(GetConfig().prefix + text);
		const FString sender = ToFString(GetConfig().sender_name);

		// "{}" keeps the message itself out of the format string, so braces in config.json are literal
		AsaApi::GetApiUtils().SendChatMessage(player, sender, L"{}", line.c_str());
	}

	std::string Lowercase(std::string value)
	{
		for (char& c : value)
		{
			if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
		}
		return value;
	}

	std::string FormatDuration(int64_t seconds)
	{
		if (seconds < 0)
			seconds = 0;

		const int64_t minutes = seconds / 60;
		const int64_t rest = seconds % 60;

		if (minutes > 0)
			return std::to_string(minutes) + "m " + std::to_string(rest) + "s";

		return std::to_string(rest) + "s";
	}

	std::string Format(const std::string& templ, const std::vector<std::string>& args)
	{
		std::string result;
		result.reserve(templ.size());

		for (size_t i = 0; i < templ.size(); ++i)
		{
			if (templ[i] != '{')
			{
				result += templ[i];
				continue;
			}

			const size_t close = templ.find('}', i);
			if (close == std::string::npos)
			{
				result += templ.substr(i);
				break;
			}

			const std::string token = templ.substr(i + 1, close - i - 1);
			size_t index = 0;
			bool numeric = !token.empty();
			for (const char c : token)
			{
				if (c < '0' || c > '9')
				{
					numeric = false;
					break;
				}
				index = index * 10 + static_cast<size_t>(c - '0');
			}

			if (numeric && index < args.size())
				result += args[index];
			else
				result += templ.substr(i, close - i + 1); // Unknown placeholder: leave it visible rather than eat it

			i = close;
		}

		return result;
	}
} // namespace QoL
