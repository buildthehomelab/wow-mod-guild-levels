/*
 * mod-guild-levels loader.
 *
 * AzerothCore looks up a loader symbol derived from the module's folder name: for folder
 * "mod-guild-levels" that symbol is exactly "Addmod_guild_levelsScripts". If you clone the repo
 * under a different folder name, rename this function to match.
 *
 * Released under the MIT License.
 */

void AddGuildLevelsXPScripts();
void AddGuildLevelsPerksScripts();
void AddGuildLevelsSpellScripts();
void AddGuildLevelsAddonScripts();

void Addmod_guild_levelsScripts()
{
    AddGuildLevelsAddonScripts();
    AddGuildLevelsXPScripts();
    AddGuildLevelsPerksScripts();
    AddGuildLevelsSpellScripts();
}
