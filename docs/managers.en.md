*[Français](gestionnaires.md) · **English***

# Which server managers can load AsaQoL

AsaQoL is an **AsaApi** plugin. It is not loaded by your manager, but by
AsaApi, inside the game process. So one single fact decides everything:

> **Does the manager launch `AsaApiLoader.exe`, or `ArkAscendedServer.exe`?**

`AsaApiLoader.exe` starts the game *and* injects AsaApi, which then loads the
plugins. A manager that launches the game executable directly will **never**
load any plugin — and that is the trap: the server starts normally, players
connect, and nothing indicates the plugins are missing.

---

## The two-minute test

Works for any manager, including those not listed here.

1. Install AsaApi and AsaQoL (see [installation.en.md](installation.en.md)).
2. Start the server **through the manager**.
3. Open the latest file in
   `ShooterGame\Binaries\Win64\logs\ArkApi_*.log`.

| What you read | Verdict |
|---|---|
| `[AsaQoL][info] AsaQoL charge` | The manager works. |
| The `logs\` folder is empty, or holds no recent file | AsaApi was never injected: the manager launches the wrong executable. |
| `Loaded all plugins` with no `AsaQoL` line | AsaApi runs, but the plugin is in the wrong place. |

One more check while the server runs: Task Manager should show
**`AsaApiLoader.exe`** above `ArkAscendedServer.exe`.

---

## Verified

| Manager | Platform | Plugins | How |
|---|---|---|---|
| **[asa-manager](https://github.com/SensethO/asa-manager)** | Windows | **Yes** | Detects AsaApi and switches on its own. Logs *"AsaApi detected: launching via AsaApiLoader.exe"*. The only one that drives the 14 `qol.*` RCON commands from a UI. |
| **[AASM](https://arkascendedservermanager.com/asa-api-plugin-setup/)** | Windows, Linux | **Yes** | First-class support, **ASA-API** tab. Starts `AsaApiLoader.exe` instead of `ArkAscendedServer.exe`. Paid after a trial. |
| **[AMP](https://discourse.cubecoders.com/t/ark-survival-ascended-guide/6738)** (CubeCoders) | Windows, Linux | **Yes** | *Configuration → Updates → Runtime Configuration*, then update the server. **Vendor's caveat**: on Windows, AMP can no longer track the process, so metrics become wrong. |
| **[POK-manager](https://github.com/Acekorneya/Ark-Survival-Ascended-Server)** | Linux (Docker + Proton) | **Yes, with a caveat** | Installs AsaApi 2.03, SHA-256 verified, runs the loader under Wine. **Vendor's caveat**: *"custom plugins are unsupported and may fail under Wine"* — AsaQoL is one. Untested. |
| **[WindowsGSM](https://github.com/ohmcodes/WindowsGSM.ArkSAwithServerAPI)** + ServerAPI add-on | Windows | **Yes** | Add-on built for ServerAPI. The *Permissions* plugin is required and not bundled. The author advises against auto-update on start. |
| **Nitrado** | Managed hosting | **No** | Allows no external plugins. No access to the server binaries. Mods remain possible, plugins do not. |

## Not verified

These managers exist and handle ASA, but I found no reliable source on their
AsaApi support. Run the test above on them.

- **Arti's ARK: Survival Ascended Server Manager** (Windows, free)
- **ASAM** — justsomebritishguy (Windows)
- **arkservermanager.app** (Windows, open source)
- **ASA Dedicated Manager** — asadedicatedmanager.eu
- **ARK-Ascended-Server-Manager** — Ch4r0ne (Windows, PowerShell)
- **HaruHostGSM** (Windows, Linux)

## Out of reach

| Case | Why |
|---|---|
| **Managed hosts** — G-Portal, Shockbyte, Host Havoc and similar | No filesystem access, no choice of executable. A web panel cannot inject AsaApi. |
| **Native Linux server** (without Proton) | AsaApi is a Windows binary. It needs a compatibility layer — which is exactly what POK-manager provides. |
| **Clients / non-dedicated servers** | AsaApi only injects into the dedicated server. |

**Worth remembering**: a host renting you a **full Windows machine** (VPS,
dedicated server, Remote Desktop access) is not a managed host. Everything
works there, because you choose what gets launched.

---

## What another manager will not give you

Even on a manager that loads the plugin, the **14 `qol.*` RCON commands** have
no UI. They remain typeable by hand in its RCON console:

```
qol.announce 30 Restarting in 5 minutes
qol.dinos owner=Petra
qol.wildlevels reload
qol.gamemode
```

The Creatures, Players, Wild levels, Stacks and Strategic settings tabs belong
to **[asa-manager](https://github.com/SensethO/asa-manager)**, which calls
these commands and writes directly into
`ArkApi\Plugins\AsaQoL\config.json`.
