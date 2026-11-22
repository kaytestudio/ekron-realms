{ Realms Engine - the Quake 2 game module (gamex86) as a shared library.

  librealms_game.so / .dylib / realms_game.dll exports GetGameAPI with the
  Quake 2 game API v3 C ABI (realms/include/realms/q2game.h). The C++
  sandbox loads it through realms::GameServer:

    fpc -Mdelphi -dGAMEDLL -FUunits -Fu. -Fi. -olibrealms_game.so realms_game.lpr

  (CMake builds it as the realms_game target when Free Pascal is found.)
  The game units use $PACKRECORDS C, so the records have the same layout as
  the C structs; GameUnit's initialization checks the sizes. }
library realms_game;

{$MODE Delphi}

uses
  g_ai, g_chase, g_cmds, g_combat, g_func, g_items, g_local, g_local_add, g_main, g_misc,
  g_monster, g_phys, g_save, g_spawn, g_svcmds, g_target, g_trigger, g_turret, g_utils, g_weapon,
  m_actor, m_berserk, m_boss2, m_boss3, m_boss31, m_boss32, m_brain, m_chick, m_flash, m_flipper,
  m_float, m_flyer, m_gladiator, m_gunner, m_hover, m_infantry, m_insane, m_medic, m_move,
  m_mutant, m_parasite, m_player, m_soldier, m_supertank, m_tank,
  p_client, p_hud, p_trail, p_view, p_weapon;

exports
  GetGameApi name 'GetGameAPI';

begin
end.
