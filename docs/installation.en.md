*[Français](installation.md) · **English***

# Installing AsaQoL

Three pieces, often confused, installed separately:

| | What | Where | Who installs it |
|---|---|---|---|
| **1** | **AsaApi** | `ShooterGame\Binaries\Win64\` | You, or your manager |
| **2** | **AsaQoL** (this repo) | `…\Win64\ArkApi\Plugins\AsaQoL\` | You |
| **3** | **AsaQoLUI** (mod) | Downloaded by ARK | ARK, from its mod id |

The plugin **works without the mod**: chat commands run, and `/inv` falls back
to a text listing. The mod only adds the graphical window.

---

## Step 1 — AsaApi

**SteamCMD does not install it.** This is the most common omission, and the
most silent one: without AsaApi the server starts normally and no plugin is
ever loaded.

Get the official build from
[ServersHub/ServerAPI](https://github.com/ServersHub/ServerAPI/releases) and
unzip it into `ShooterGame\Binaries\Win64\`. It adds six files:

```
AsaApiLoader.exe        AsaApiLoader.pdb        config.json
libcrypto-3-x64.dll     libssl-3-x64.dll        msdia140.dll
ArkApi\                 (folder)
```

Those six and nothing else: the `boost_*`, `tbb`, `msvcp140*` and `vcruntime*`
files sitting next to them ship with the game. This list comes from diffing
the folder against Steam's own install manifests.

You also need the **Microsoft Visual C++ 2015-2022 x64 Redistributable**.

**From then on the server must be started by `AsaApiLoader.exe`**, never by
`ArkAscendedServer.exe`. See [managers.en.md](managers.en.md) to find out
whether yours does.

## Step 2 — The plugin

### With the script, server stopped

```powershell
.\Installer-AsaQoL.ps1 -Serveur E:\ServersASA\test1
```

The script checks that AsaApi is present and **refuses to go further if it is
missing** — rather than dropping files that would never be read. It backs up
an existing configuration before touching it, and never touches `data.json`,
which holds player homes and cooldowns.

### With the script, server running

```powershell
.\Installer-AsaQoL.ps1 -Serveur E:\ServersASA\test1 -Chaud
```

It drops `AsaQoL.dll.arkapi`: AsaApi saves the world, unloads the plugin and
reloads it, with no downtime. This is also how you update.

### By hand

Unzip `AsaQoL-1.1.zip` into:

```
<server>\ShooterGame\Binaries\Win64\ArkApi\Plugins\
```

You should end up with an `AsaQoL\` folder holding `AsaQoL.dll`,
`config.json` and `PluginInfo.json`.

To **update** without overwriting your configuration, use
`AsaQoL-1.1-maj.zip` instead — it contains only the binary, already named
`AsaQoL.dll.arkapi`.

## Step 3 — The mod, if you want the window

Add **`1650813`** to the server's mod list. ARK downloads it on its own.

An ASA mod **cannot be installed locally**: the server delegates to the
CurseForge core, ignores a hand-placed `Mods\` folder, queries the catalogue,
and **shuts down** on an unknown id.

---

## Verifying

Latest file in `ShooterGame\Binaries\Win64\logs\ArkApi_*.log`:

```
[AsaQoL][info] Configuration chargee : 1 kit(s), homes=true, tpa=true, annonces=true
[AsaQoL][info] Pont client : interception posee
[AsaQoL][info] AsaQoL charge
[API][info] Loaded plugin AsaQoL V1.1
```

In game, `/help` lists the commands. Over RCON, `qol.players` answers.

If the `logs\` folder is empty, the plugin is not the problem — AsaApi was
never injected at all. Go back to step 1.

---

## Configuration

`ArkApi\Plugins\AsaQoL\config.json`, re-read when the plugin reloads.

| Section | Effect |
|---|---|
| `General` | Message prefix, sender name |
| `Homes`, `Tpa` | Cooldowns, warmups, number of homes |
| `Kits` | Contents and cooldown of each kit |
| `Announce` | Colour, duration and scale of the on-screen banner |
| `Window` | `/inv` window: search radius, mod blueprint path |
| `WildLevels` | Wild creature levels |

**`WildLevels` ships disabled**, deliberately: it forces a level floor on wild
creatures, which changes the nature of a playthrough. That is not a neutral
default — enable it as a decision, not by accident.

One caveat before you do: the interception skips **unconscious** creatures.
Without that guard, a creature being tamed had its stats recomputed on server
restart — torpor included — woke up and walked away, progress lost.

---

## Uninstalling

Delete the `ArkApi\Plugins\AsaQoL\` folder and restart. Nothing else is
affected: the plugin writes neither to save files nor to the `.ini` files.

Remove mod `1650813` from the list too, if you had added it.
