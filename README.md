# fix-vanguard

A small tool that unlocks the Call of Duty: Vanguard campaign achievements that are broken on Steam.

The tool does not give you achievements that you did not earn. It reads the progress that the game recorded for you. Then it unlocks only the achievements for which that progress is complete.

## The problem

Nine campaign achievements never unlock on Steam. Steam statistics show each of them at less than 1% of players:

| Achievement | Requirement |
|---|---|
| Did I Get 'Em? | 15 kills with Blind Fire |
| Munition Magpie | Scavenge ammo from 15 enemy corpses in 3 seconds or less after the kill |
| Gotta Try Them All! | Kills with 15 different weapons |
| OH YEAH! | Tactical sprint through 5 destructible walls |
| Not Your Fairy Godmother | Throw back 15 grenades |
| Shot Caller | Give 15 commands as Arthur |
| Pop Goes the Nightingale | 15 kills as Polina immediately after a crawlspace |
| Heightened Senses | 15 kills as Wade with the focus ability |
| Pocket Full of Fun | 3 kills with each of 4 types of lethal equipment as Lucas |

All other campaign achievements work so are not touched by this.

## Why they do not unlock

These nine are the only achievements that count progress across the full campaign. All nine go through one function in the campaign script `scripts/sp/player/achievements.gsc`.

This is the function, decompiled from the game files. The engine function names in angle brackets are assumed.

```c
updateachievement( name, player )
{
    if ( !isdefined( name ) )
        return;

    if ( !isdefined( player ) )
        player = level.player;

    field = _get_ddl_content( name );
    count = undefined;

    if ( isdefined( field ) && field != "" )
        count = player getplayerprogression( field );
    else
        return;

    if ( isdefined( count ) )
        player setplayerprogression( field, count + 1 );

    if ( istrue( <is xbox>() ) || istrue( <is pc>() ) )
        player <report achievement progress>( name, count + 1, _get_achievement_target_value( name ) );
    else if ( count + 1 >= _get_achievement_target_value( name ) )
        level thread giveachievement( name );
}
```

The function does two things:

1. It adds 1 to a counter in your campaign progress. This part works on Steam.
2. It tells the platform about the achievement. This part is broken on Steam.

Step 2 has two paths. On PlayStation the script calls `giveachievement` when the counter reaches the target. On Xbox and PC the script calls a different engine function that reports progress (current value and target value). Xbox achievements are progress-based, so that call is sufficient on Xbox.

On Steam, the progress call does not unlock the achievement. The PC path never calls `giveachievement`. Thus the counter increases correctly, but Steam never gets the unlock. The other campaign achievements do not use this function. They call `giveachievement` directly, and they work.

Anyone who has worked with SteamWorks (Valve's API for games on Steam) knows this, it's not a big mystery and it's also not Valve's problem (as Activision has tried to claim).

## What the tool does

1. It reads `Documents\Call of Duty Vanguard\players\settings.*.Vanguard`. The game stores the nine counters in this file.
2. It shows each counter and its target.
3. It asks for your confirmation.
4. For each counter that is at or above its target, it calls the Steam function `SetAchievement`. This is the same Steam function that the game uses for the achievements that work.

Achievements with a counter below the target stay locked. Play more, then run the tool again.

## Why this is safe

- The tool does not start the game and does not attach to the game process. There is no injection, no memory read or write, and no hook.
- The tool does not change any game file or save file. It opens the settings file as read-only.
- The tool does not touch the anti-cheat driver. The game is closed while the tool runs.
- The tool does not affect multiplayer.
- The tool talks to Steam through `steam_api64.dll` from your game folder. No other library is included.
- The source is one C file. You can read it and build it yourself.

## How to use

1. Download `fix-vanguard.zip` from the [releases page](https://github.com/Kruithne/fix-vanguard/releases) and extract it.
2. Start the game one time and go to the main menu, so that your campaign progress is on this computer. Then close the game.
3. Make sure that Steam is open.
4. Run `fix-vanguard.exe`.
5. Read the table. Press `Y` to unlock the earned achievements.

Steam shows you as in the game for a few seconds. This is normal.

If the tool cannot find the game, put `fix-vanguard.exe` in the game folder. As an alternative, give the game folder as the first argument:

```
fix-vanguard.exe "D:\SteamLibrary\steamapps\common\Call of Duty Vanguard"
```

## Stranger Danger

If you don't trust downloading random executable files from strange people on the internet, that's absolutely reasonable and the correct answer.

The executable found on the releases page is automatically built from the source code in `fix-vanguard.c` via `rellease.yml`, so you can audit the whole process yourself or ask that one programmer nerd friend you have to take a look.

If you still don't trust it, you can download the .c file yourself and build it. Just run `build.bat`.

You need GCC for Windows (for example [w64devkit](https://github.com/skeeto/w64devkit)).


## Motivation
I like to 100% games on Steam as many other people do, and generally when I come across an achievement that's bugged or otherwise unobtainable, I cut my losses and move on. With Microsoft Flight Simulator, I waited 5 years for them to fix an achievement.

With this game, people have reached out to Activision many, many, MANY times and the response has been to shrug it off or blame it on Steam. The decompiled script above proves that this is actually a very simple bug they could, but refuse to fix.

While there are means of unlocking achievements, that defeats the point of them for me. So I built this tool, which requires you to legitimately earn the achievements, but bridges the gap to actually unlocking them as a substitute for Activision's mistake (and shitty attitude).

We draw the line where we need to. At the end of the day they're just images, they don't actually mean anything. If you don't agree with this then don't use it.

## Disclaimer

This project is not affiliated with Activision or Valve. Use it at your own risk.
