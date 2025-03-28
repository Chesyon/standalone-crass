#include <pmdsky.h>
#include <cot.h>
#include "extern.h"
#include "crass.h"

/***************************************
 *  Cancel Recover Acting Skip System  *
 ***************************************/

#if CANCEL_RECOVER_ACTING_SKIP_SYSTEM

struct crass_settings CRASS_SETTINGS;

/*
  Returns if the current main routine originates from Unionall.
  At least one of the following conditions must be met:
  - If the main routine kind is ROUTINE_UNIONALL
  - If the main routine's hanger (suspected field, not 100% sure) is 0
  - If the main routine kind's file address is UNIONALL_RAM_ADDRESS...and...
    - If the call stack is non-null (i.e. the main routine used OPCODE_CALL_COMMON or OPCODE_CALL to a Unionall address), the saved file address is also UNIONALL_RAM_ADDRESS
    - But if the call stack is null, we're for sure in Unionall.
*/
bool IsMainRoutineBornFromUnionall(void) {
  struct script_routine* main_routine = GROUND_STATE_PTRS.main_routine;
  return main_routine->routine_kind.val == ROUTINE_UNIONALL || main_routine->states[0].field_0x4 == 0 || (main_routine->states[0].ssb_info[0].file == UNIONALL_RAM_ADDRESS && (main_routine->states[0].ssb_info[1].next_opcode_addr != NULL ? main_routine->states[0].ssb_info[1].file == UNIONALL_RAM_ADDRESS : true));
}

/*
  Returns if the current main routine should not be skipped due to some of the routine's attributes.
  In particular, to be skipped, a main routine must have valid ssb_info addresses, must not be in Unionall, and must be currently performing an Acting scene.
*/
bool IsMainRoutineInvalidToSkip(void) {
  struct script_routine* main_routine = GROUND_STATE_PTRS.main_routine;
  uint32_t* routine_addresses = (uint32_t*)(&(main_routine->states[0].ssb_info));
  uint32_t checksum = 0;
  // Add up all the fields of script_routine::ssb_info
  for(int i = 0; i < 4; i++)
    checksum += routine_addresses[i];
  return (checksum == NULL || IsMainRoutineBornFromUnionall() || main_routine->states[0].field_0x4 != 3);
}

/*
  A small wrapper function that performs the actions of the script opcode OPCODE_MESSAGE_SET_WAIT_MODE.
*/
void MessageSetWaitModeWrapper(int speed1, int speed2) {
  MESSAGE_SET_WAIT_MODE_PARAMS[0] = speed1;
  MESSAGE_SET_WAIT_MODE_PARAMS[1] = speed2;
  MessageSetWaitMode(speed1, speed2);
}

/*
  Given an address to a script opcode, return the type of parsing operation used in cutscene skips.
  There are several possible types of parsing; see the enum "opcode_parse_kind" in "crass.h" for more info.

  Not all opcodes are treated equally when parsing a script to skip. Some opcodes, such as actor movement, are unimportant and can be skipped.
  Others, such as variable manipulation and flow control, must be properly run with the RunNextOpcode function.
*/
enum opcode_parse_kind GetOpcodeParseType(uint16_t* opcode_id_addr) {
  uint16_t opcode_id = *opcode_id_addr;
  if(opcode_id == OPCODE_MAIN_ENTER_DUNGEON)
    return OPCODE_PARSE_DUNGEON;
  else if(opcode_id == OPCODE_MAIN_ENTER_GROUND)
    return OPCODE_PARSE_GROUND;
  else if(opcode_id == OPCODE_PROCESS_SPECIAL)
    return OPCODE_PARSE_SP;
  else if(opcode_id == OPCODE_MESSAGE_SWITCH_MENU || opcode_id == OPCODE_MESSAGE_SWITCH_MENU2)
    return OPCODE_PARSE_SWITCH_MENU;
  else if(opcode_id == OPCODE_MESSAGE_MENU)
    return OPCODE_PARSE_MESSAGE_MENU;
  else if(opcode_id == OPCODE_CALL_COMMON)
    return OPCODE_PARSE_CALL_COMMON;
  else if(IsWithinRange(opcode_id, OPCODE_FLAG_CALC_BIT, OPCODE_FLAG_SET_SCENARIO) ||
          IsWithinRange(opcode_id, OPCODE_BRANCH, OPCODE_CALL) ||
          IsWithinRange(opcode_id, OPCODE_SWITCH, OPCODE_SWITCH_VARIABLE) ||
          IsWithinRange(opcode_id, OPCODE_DEBUG_ASSERT, OPCODE_DEBUG_PRINT_SCENARIO) ||
          IsWithinRange(opcode_id, OPCODE_ITEM_GET_VARIABLE, OPCODE_ITEM_SET_VARIABLE) ||
          opcode_id == OPCODE_JUMP ||
          opcode_id == OPCODE_RETURN)
    return OPCODE_PARSE_AUTO;
  else
    return OPCODE_PARSE_MANUAL;
}

/*
  Given an address to a script opcode, calculate the starting address of the next opcode.
  This function is most notably used in the naive parsing algorithim and for opcodes who are of the parse kind OPCODE_PARSE_MANUAL, among other exceptions.
*/
uint16_t* CalcNextOpcodeAddress(uint16_t* next_opcode_addr) {
  struct script_opcode current_op = SCRIPT_OP_CODES.ops[*next_opcode_addr]; // TODO: Account for custom opcodes?
  signed char num_params = (signed char)current_op.n_params;
  return next_opcode_addr += num_params < 0 ? ScriptParamToInt(next_opcode_addr[1]) + 2 : num_params + 1;
}

/*
  Given a script routine and an address to either OPCODE_SWITCH_MENU or OPCODE_SWITCH_MENU2, try to parse the remaining opcodes of the routine.
  Returning true indicates that the remaining opcodes were successfully parsed, and false otherwise.
  
  This function is the core component of skipping a cutscene. When a cutscene is skipped in the base game via OPCODE_CANCEL_RECOVER_COMMON, the game will jump to
  the coroutine ROUTINE_DEMO_CANCEL and stop running opcodes of the cutscene that was just skipped. This, however, poses a problem for cutscene skips:
  We need functionality in place to emulate important opcodes' behavior, like variable updates, special processes, exiting cutscenes, and so on.
  Thus, this function must investigate every remaining opcode in the script and choose which to "skip" and which to "execute".

  Additionally, flow control is maintained when scanning a script with this function, in the event that a script must perform an action like
  a for-loop within its main routine. Branches, switches, jumps, and calls work properly, but another problem arises: User-based branching.
  The opcodes OPCODE_MESSAGE_SWITCH_MENU and OPCODE_MESSAGE_SWITCH_MENU2 branch based on user input, and as such, we cannot assume which
  menu option will lead to the routine's end and which will infinitely loop.

  To combat this problem, every single case of a user-based menu will be investigated sequentially. This function will call itself recursively,
  able to detect which menu option is an infinite loop by comparing the next opcode address to the address of the most recent "switch menu".

  With all of this in mind, there are only two conditions that would cause this function to return false:

    - If parsing somehow goes out-of-bounds and reads an opcode from data it isn't meant to
    - If all cases of a "switch menu" opcode lead to infinite loops

  However, both conditions indicate a larger problem with the script itself and should not be common occurences.
  
  TL;DR this function quickly emulates the remaining opcodes of a cutscene that was just skipped!
*/
bool TryCutsceneSkipScanInner(struct script_routine* routine, uint16_t* switch_menu_addr) {
  uint16_t* next_opcode_addr = routine->states[0].ssb_info[0].next_opcode_addr;
  uint16_t next_opcode_id = *(next_opcode_addr);
  undefined4 unknown;
  while(!(next_opcode_id == OPCODE_END || next_opcode_id == OPCODE_HOLD)) {
    next_opcode_addr = routine->states[0].ssb_info[0].next_opcode_addr;
    if(!IsWithinRange((uint32_t)next_opcode_addr, (uint32_t)routine->states[0].ssb_info[0].opcodes, (uint32_t)routine->states[0].ssb_info[0].strings))
      return false; // Critical error! The opcode parsing has somehow gone out-of-bounds and is no longer reading valid data!
    switch(GetOpcodeParseType(next_opcode_addr)) {
      case OPCODE_PARSE_MANUAL:;
      parse_manual:;
        routine->states[0].ssb_info[0].next_opcode_addr = CalcNextOpcodeAddress(next_opcode_addr); // Skip over the opcode and just calculate the next opcode address
        break;
      case OPCODE_PARSE_AUTO:;
      parse_auto:;
        // Perform the normal functions of an opcode instead of skipping over it!
        // The function RunNextOpcode also advances to the next opcode address, meaning we don't need to call CalcNextOpcodeAddress.
        RunNextOpcode(routine);
        break;
      case OPCODE_PARSE_DUNGEON:;
        // Cutscene redirects will take priority over entering a dungeon
        if(!CRASS_SETTINGS.redirect) {
          next_opcode_addr[2] = 30;
          CRASS_SETTINGS.enter_dungeon = true;
          goto parse_auto;
        }
        goto parse_manual;
      case OPCODE_PARSE_GROUND:;
        // Cutscene redirects will take priority over entering the overworld
        if(!CRASS_SETTINGS.redirect) {
          CRASS_SETTINGS.enter_ground = true;
          goto parse_auto;
        }
        goto parse_manual;
      case OPCODE_PARSE_SP:;
        // Calling RunNextOpcode on a special process doesn't quite run a special process's code, so some manual setup is required.
        int sp_params[3];
        for(int i = 0; i < 3; i++)
          sp_params[i] = ScriptParamToInt(next_opcode_addr[i+1]);
        routine->states[0].ssb_info[0].next_opcode_addr = CalcNextOpcodeAddress(next_opcode_addr);
        routine->states[0].ssb_info[0].next_opcode_addr = ScriptCaseProcess(routine, ScriptSpecialProcessCall(&unknown, sp_params[0], sp_params[1], sp_params[2]));
        break;
      case OPCODE_PARSE_SWITCH_MENU:;
        // Search each OPCODE_CASE_MENU sequentially for the valid path that leads to the end of the script...
        uint16_t* current_switch_menu_addr = next_opcode_addr;
        if(current_switch_menu_addr == switch_menu_addr)
          return false; // Infinite loop detected, return false and try the next case...
        do {
          next_opcode_addr = CalcNextOpcodeAddress(next_opcode_addr); 
          if(!(*next_opcode_addr == OPCODE_CASE_MENU || *next_opcode_addr == OPCODE_CASE_MENU2)) {
            // We've finished investgating all case menus...begin the final attempt...
            routine->states[0].ssb_info[0].next_opcode_addr = next_opcode_addr;
            return TryCutsceneSkipScanInner(routine, current_switch_menu_addr); // If the final scan attempt fails, just stop scanning the cutscene early
          }
          // Calculate the correct offset given by an OPCODE_CASE_MENU
          routine->states[0].ssb_info[0].next_opcode_addr = routine->states[0].ssb_info[0].file + (next_opcode_addr[2] << 1);
        } while(!TryCutsceneSkipScanInner(routine, current_switch_menu_addr));
        return true;
      case OPCODE_PARSE_MESSAGE_MENU:;
        // Filter which OPCODE_MESSAGE_MENU menus are actually run...
        uint16_t message_menu_id = ScriptParamToInt(next_opcode_addr[1]);
        if(message_menu_id == 54) // Execute MENU_DUNGEON_INITIALIZE_TEAM
          goto parse_auto;
        else if(message_menu_id == 1 || message_menu_id == 4 || message_menu_id == 11) { // Redirect to ROUTINE_MAP_TEST if MENU_HERO_NAME, MENU_TEAM_NAME, or MENU_SAVE_MENU is encountered!
          CRASS_SETTINGS.menu_skipped = message_menu_id;
          CRASS_SETTINGS.redirect = true;
        }
        else if(message_menu_id == 63 || message_menu_id == 64) { // Item-giving menus
          struct bulk_item item;
          ItemAtTableIdx(0, &item);
          // Might need to use ScriptCaseProcess on the call to ScriptSpecialProcessCall instead of calculating the address manually?
          routine->states[0].ssb_info[0].next_opcode_addr = CalcNextOpcodeAddress(next_opcode_addr);
          ScriptSpecialProcessCall(&unknown, message_menu_id == 63 ? SPECIAL_PROC_ADD_ITEM_TO_BAG : SPECIAL_PROC_ADD_ITEM_TO_STORAGE, item.id.val, item.quantity);
          break;
        }
        goto parse_manual; // May need to simulate each case taken like how OPCODE_PARSE_SWITCH_MENU gets parsed
      case OPCODE_PARSE_CALL_COMMON:;
        // Filter which Unionall coroutines are called...
        enum common_routine_id coroutine_id = ScriptParamToInt(next_opcode_addr[1]);
        if(coroutine_id == ROUTINE_HANYOU_SAVE_FUNC)
          goto parse_manual;
        goto parse_auto;
      }
    next_opcode_id = *(routine->states[0].ssb_info[0].next_opcode_addr);
  }
  return true;
}

/*
  Attempt a cutscene skip, returning whether the cutscene's remaining opcodes could all be parsed!
  While the function TryCutsceneSkipScanInner ultimately handles the bulk of the opcode parsing and has the potential for recursion,
  this function is only called once per cutscene skip and performs some initial setup before committing to emulate important opcodes.

  If the opcode after the skippable cutscene (played by OPCODE_SUPERVISION_EXECUTE_ACTING_SUB in Unionall) is OPCODE_HOLD or OPCODE_END, then
  the cutscene may only be permitted to skip if it contains at least one instance of OPCODE_MAIN_ENTER_DUNGEON or OPCODE_MAIN_ENTER_GROUND.
  This function searches for either opcode by beginning at the opcode start address and investigating every opcode until it reaches
  the string start address, without care for flow control. Important opcodes like variable manipulation are not properly executed.
  If neither opcode is found, then the cutscene cannot be skipped and will fall back to performing a cutscene speedup.

  If the return value of TryCutsceneSkipScanInner is false, then a redirect will be performed to reload the game at ROUTINE_MAP_TEST with a skip kind of CRASS_ERROR.
*/
__attribute((used)) bool TryCutsceneSkipScan(void) {
  if(CRASS_SETTINGS.skip_active) {
    CRASS_SETTINGS.enter_dungeon = false;
    CRASS_SETTINGS.enter_ground = false;
    CRASS_SETTINGS.menu_skipped = 0;
    struct script_routine main_routine;
    uint16_t* next_opcode_addr;
    uint16_t next_opcode_id;
    if(IsMainRoutineInvalidToSkip()) {
      CRASS_SETTINGS.skip_active = false;
      return false;
    }
    MemcpySimple(&main_routine, GROUND_STATE_PTRS.main_routine, sizeof(struct script_routine));
    // Conditional naive pass: If the current cutscene is followed by an ending control opcode, scan the whole script to ensure it has OPCODE_MAIN_ENTER_DUNGEON or OPCODE_MAIN_ENTER_GROUND!
    if(CRASS_SETTINGS.end_after_cutscene) {
      next_opcode_addr = (uint16_t*)(main_routine.states[0].ssb_info[0].opcodes);
      next_opcode_id = *(next_opcode_addr);
      while(true) {
        if(next_opcode_addr >= (uint16_t*)main_routine.states[0].ssb_info[0].strings) {
          // OPCODE_MAIN_ENTER_DUNGEON and OPCODE_MAIN_ENTER_DUNGEON not found, so fall back to a speedup!
          CRASS_SETTINGS.skip_active = false;
          CRASS_SETTINGS.can_skip = false;
          CRASS_SETTINGS.speedup_active = true;
          PlaySeVolumeWrapper(0x4);
          MessageSetWaitModeWrapper(0, 0);
          return false;
        }
        if(next_opcode_id == OPCODE_MAIN_ENTER_DUNGEON || next_opcode_id == OPCODE_MAIN_ENTER_DUNGEON)
          break; // Found an opcode that stops playing cutscenes; keep going with the cutscene skip attempt!
        next_opcode_addr = CalcNextOpcodeAddress(next_opcode_addr);
        next_opcode_id = *(next_opcode_addr);
      }
    }
    // General smart recursive pass: Run through any important variable-setting opcodes!
    bool cutscene_skipped_successfully = TryCutsceneSkipScanInner(&main_routine, NULL);
    CRASS_SETTINGS.can_skip = false;
    CRASS_SETTINGS.can_speedup = false;
    if(!cutscene_skipped_successfully) {
      // Error encountered with cutscene skipping, so attempt a redirect and note the error!
      CRASS_SETTINGS.redirect = true;
      CRASS_SETTINGS.menu_skipped = 0;
      CRASS_SETTINGS.crass_kind = CRASS_ERROR;
      return false;
    }
  }
  return true;
}

/*
  This function may hijack the coroutine that is referenced when (re)loading Unionall to either ROUTINE_EVENT_DIVIDE or ROUTINE_MAP_TEST.
  To hijack a coroutine, a cutscene skip must be active, not in the middle of entering a dungeon, and have a status (field unknown) of 2.

  ROUTINE_MAP_TEST is the coroutine used for cutscene "redirections", i.e. when a cutscene skip diverts to ROUTINE_MAP_TEST instead of playing the next cutscene.
*/
__attribute((used)) bool GetRecoverCoroutineInfo(struct coroutine_info* coroutine_info, enum common_routine_id coroutine_id, int execution_status) {
  CRASS_SETTINGS.coroutine_hijack = CRASS_SETTINGS.skip_active && !CRASS_SETTINGS.enter_dungeon && execution_status == 2;
  if(CRASS_SETTINGS.coroutine_hijack)
    coroutine_id = CRASS_SETTINGS.enter_ground ? ROUTINE_EVENT_DIVIDE : ROUTINE_MAP_TEST;
  return GetCoroutineInfo(coroutine_info, coroutine_id);
}

/*
  This function immediately follows GetRecoverCoroutineInfo during execution, responsible for manipulation of the main routine's fields during a cutscene skip.

    - If a cutscene redirection will occur, then the routine's call stack will be set up such that using OPCODE_RETURN will jump back to the opcode following the skipped OPCODE_SUPERVISION_EXECUTE_ACITNG_SUB.
    - If a cutscene redirection will not occur, then the routine's next opcode address will be that of the the opcode following the skipped OPCODE_SUPERVISION_EXECUTE_ACITNG_SUB.
*/
__attribute((used)) void CustomInitScriptRoutineFromCoroutineInfo(struct script_routine* routine, undefined4 param_2, struct coroutine_info* coroutine_info, int status) {
  InitScriptRoutineFromCoroutineInfo(routine, param_2, coroutine_info, status);
  if(CRASS_SETTINGS.coroutine_hijack && CRASS_SETTINGS.return_info.next_opcode_addr != NULL) {
    if(CRASS_SETTINGS.redirect)
      MemcpySimple(&(routine->states[0].ssb_info[1]), &(CRASS_SETTINGS.return_info), sizeof(struct ssb_runtime_info)); // Setting up fields to be used by OPCODE_RETURN
    else
      routine->states[0].ssb_info[0].next_opcode_addr = CRASS_SETTINGS.return_info.next_opcode_addr; // Next opcode address is the opcode following the skipped OPCODE_SUPERVISION_EXECUTE_ACITNG_SUB
    CRASS_SETTINGS.redirect = false;
    CRASS_SETTINGS.skip_active = false;
    CRASS_SETTINGS.coroutine_hijack = false;
  }
}

/*
  This function decides what status code to return upon exiting ground mode.
*/
__attribute((used)) int DebugPrintGameCancel(char* fmt) {
  if(CRASS_SETTINGS.skip_active) {
    DebugPrint0("GAME SKIP\n");
    if(CRASS_SETTINGS.enter_dungeon) {
      CRASS_SETTINGS.skip_active = false;
      CRASS_SETTINGS.enter_dungeon = false;
      return 0x5; // Enter a dungeon (this may not run in time to matter, but just in case, it's kept here)
    }
    else
      return 0x9; // Reload Unionall and start at a new coroutine
  }
  else {
    DebugPrint0(fmt);
    return 0xB; // Title screen
  }
}

/*
  This function obtains the name of an Acting scene and an optional "crass_kind" parameter.

  To allow some degree of customizability with cutscene skips, each cutscene can have a parameter via its name similar to text tags.
  See the "crass_kind" enum in "crass.h" for more info.

  For example, if the scene name "s12a0701:1" is encountered, scene s12a0701 will be loaded and have a crass_kind of CRASS_OFF (1), making it unskippable.
  Omitting a crass_kind parameter is the same as using a parameter of CRASS_DEFAULT (0), making the cutscene have its default skip settings.
*/
__attribute((used)) void CustomGetSceneName(char* truncated_scene_name, char* full_scene_name) {
  GetSceneName(truncated_scene_name, full_scene_name); // Clamps the scene name down to 8 characters; may not contain null byte
  int crass_kind = -1;
  char current_char = *full_scene_name;
  int len = 0;
  // Scan through the full scene name manually to try and find a crass_kind parameter!
  while(current_char != '\0') {
    if(current_char == ':') {
      crass_kind = AtoiTag(full_scene_name+1); // Get the integer value of anything beyond a ":" in the scene name
      break;
    }
    full_scene_name++;
    len++;
    current_char = *full_scene_name;
  }
  if(crass_kind < CRASS_DEFAULT)
    crass_kind = CRASS_DEFAULT; // No parameter, default to normal skip settings
  else if(len < 8)
    truncated_scene_name[len] = '\0'; // Ensure we don't try to treat any part of the skip parameter as the scene name
  uint16_t* next_opcode_addr = GROUND_STATE_PTRS.main_routine->states[0].ssb_info[0].next_opcode_addr;
  uint16_t next_opcode_id = *next_opcode_addr;
  MemZero(&CRASS_SETTINGS, sizeof(struct crass_settings));
  // Only allow a cutscene to be skipped if it's loaded by Unionall, i.e. don't skip a cutscene that loads a cutscene
  if(!IsMainRoutineBornFromUnionall()) {
    CRASS_SETTINGS.crass_kind = CRASS_OFF;
    return;
  }
  CRASS_SETTINGS.crass_kind = crass_kind;
  // Decide what sort of action should be taken, given a crass_kind parameter to a scene...
  switch(crass_kind) {
    case CRASS_OFF:;
      break;
    case CRASS_DEFAULT:;
    skip_default:;
      // If a cutscene-to-overworld transition is the next opcode, fall back to a speedup instead
      if(next_opcode_id == OPCODE_CALL_COMMON) {
        int16_t coroutine_id = ScriptParamToInt(next_opcode_addr[1]);
        if(IsWithinRange(coroutine_id, ROUTINE_EVENT_END_MAPIN, ROUTINE_EVENT_END_FREE_AE))
          goto skip_speedup;
      }
      else if(next_opcode_id == OPCODE_END || next_opcode_id == OPCODE_HOLD)
        CRASS_SETTINGS.end_after_cutscene = true; // Cutscene may need to be sped up, so note it for a later check in TryCutsceneSkipScan
      CRASS_SETTINGS.can_skip = true;
      // Save the state of the script runtime info to perform a proper return after a skip
      MemcpySimple(&(CRASS_SETTINGS.return_info), &(GROUND_STATE_PTRS.main_routine->states[0].ssb_info[0]), sizeof(struct ssb_runtime_info));
      break;
    case CRASS_SPEEDUP:;
    skip_speedup:;
      CRASS_SETTINGS.can_speedup = true;
      break;
    default:;
      if(crass_kind >= CRASS_REDIRECT)
        CRASS_SETTINGS.redirect = true;
      goto skip_default;
  }
}

/*
  Returns whether a cutscene should be skipped, called nearly every frame while a script is active due to having similar conditions as OPCODE_CANCEL_RECOVER_COMMON.
  A cutscene can only be skipped if all the following conditions are met:
  
    - If the main routine is "valid" to be skipped (see IsMainRoutineInvalidToSkip)
    - If the Select button is pressed
    - If the current cutscene can be skipped

  This function also handles the activation behind cutscene speedups, since they are also triggered by pressing the Select button.
  However, even if a cutscene speedup is activated, this function will still return false because a speedup is not a skip.
*/
__attribute((used)) bool ShouldSkipCutscene(void) {
  if(IsMainRoutineInvalidToSkip())
    return false;
  uint16_t button_bitfield = 0;
  GetPressedButtons(0, (undefined*)&button_bitfield);
  if(button_bitfield & 0b100) { // If the Select button is pressed
    // |= needed because we don't want to toggle off the skip
    if(CRASS_SETTINGS.can_skip)
      CRASS_SETTINGS.skip_active |= true;
    else if(CRASS_SETTINGS.can_speedup) {
      CRASS_SETTINGS.speedup_active |= true;
      PlaySeVolumeWrapper(0x4);
      MessageSetWaitModeWrapper(0, 0);
    }
    else if(!(CRASS_SETTINGS.skip_active || CRASS_SETTINGS.speedup_active))
      PlaySeVolumeWrapper(0x2);
  }
  return CRASS_SETTINGS.skip_active && CRASS_SETTINGS.can_skip;
}

/*
  A small function that clears all of the current cutscene skip settings when OPCODE_END is reached in a script, effectively disabling skips.
  Only a main or Unionall routine may trigger this function, and OPCODE_HOLD does not cause this function to run.
  This is due to ROUTINE_DEMO_CANCEL containing an instance of OPCODE_HOLD.
*/
__attribute((used)) int TryDisableCutsceneSkipRoutineEnd(int status, struct script_routine* routine) {
  if(routine->routine_kind.val == ROUTINE_MAIN || routine->routine_kind.val == ROUTINE_UNIONALL) {
    MemZero(&CRASS_SETTINGS, sizeof(struct crass_settings));
    MessageSetWaitModeWrapper(-1, -1);
  }
  return status;
}

/*
  If a cutscene speedup is in progress, bump up the movement speed to 13.
*/
__attribute((used)) int16_t GetMovementSpeedParam(uint16_t parameter) {
  if(CRASS_SETTINGS.speedup_active)
    parameter = 13;
  return ScriptParamToFixedPoint16(parameter);
}

/*
  If a cutscene speedup is in progress, bump up the turn speed to 1.
*/
__attribute((used)) void TrySpeedUpTurnSpeedParam(uint16_t* opcode_id_addr) {
  if(CRASS_SETTINGS.speedup_active && *opcode_id_addr != OPCODE_TURN_DIRECTION)
    opcode_id_addr[1] = 1;
}

/*
  If a cutscene speedup is in progress, bump down the wait time to 1.
*/
__attribute((used)) int16_t GetWaitTime(uint16_t wait_param) { return CRASS_SETTINGS.speedup_active ? 1 : ScriptParamToInt(wait_param); }

/*
  If a cutscene speedup is in progress, make any dialogue boxes created by a script opcode have an invisible window.
*/
__attribute((used)) int8_t CreateScriptEngineDialogueBox(struct window_params* window_params) {
  if(CRASS_SETTINGS.speedup_active) {
    struct window_params speedy_params = { .x_offset = 2, .y_offset = 0x11, .width = 0x1C, .height = 0x5, .box_type = {0xFA} };
    return CreateDialogueBox(&speedy_params);
  }
  return CreateDialogueBox(window_params);
}

/*
  If a cutscene speedup is not in progress, perform the actions of OPCODE_MESSAGE_SET_WAIT_MODE.
*/
__attribute((used)) void TryMessageSetWaitMode(int param_1, int param_2) {
  if(!CRASS_SETTINGS.speedup_active)
    MessageSetWaitModeWrapper(param_1, param_2);
}

/*
  If a cutscene speedup is in progress, change the attributes of the string about to be displayed.
  This effectively gives the illusion of no text ever displaying in a dialogue box during a speedup.
*/
__attribute((used)) void ShowScriptEngineStringInDialogueBox(int window_id, struct preprocessor_flags flags, char* string, struct preprocessor_args* args) {
  if(CRASS_SETTINGS.speedup_active) {
    flags.timer_2 = true; // Instant text
    args = NULL;
    if(string != NULL)
      string[0] = '\0'; // Show nothing in the dialogue box
  }
  ShowStringInDialogueBox(window_id, flags, string, args);
}

/*
  If a cutscene speedup is in progress, don't show a portrait created by a script opcode.
*/
__attribute((used)) bool ShouldShowScriptEnginePortrait(struct portrait_params* portrait_params) { return CRASS_SETTINGS.speedup_active ? false : IsValidPortrait(portrait_params); }


// Below are various naked functions that help jump to the above C code.

__attribute((naked)) void FinalCutsceneSkipCheck(void) {
  asm("bl TryCutsceneSkipScan");
  asm("cmp r0,#0x0");
  asm("beq GroundMainLoopStuff");
  asm("bl GroundSupervisionExecuteRequestCancel");
  asm("b GroundSupervisionExecuteRequestCancelCallsite+0x4");
}

__attribute((naked)) void CheckSelectPressTrampoline(void) {
  asm("bl ShouldSkipCutscene");
  asm("cmp r0,#0x0");
  asm("beq GroundMainLoopStuff");
  asm("b CancelRecoverStart");
}

__attribute((naked)) int TrySpeedUpTurnSpeedParamTrampoline(void) {
  asm("mov r0,r6");
  asm("bl TrySpeedUpTurnSpeedParam");
  asm("subs r0,r7,#0x14c");
  asm("b TurnOpcodeSwitchStatementSetup+0x4");
}

__attribute((naked)) int HijackRunNextOpcodeControlStatement(int status) {
  asm("mov r1,r4");
  asm("bl TryDisableCutsceneSkipRoutineEnd");
  asm("b RunNextOpcodeReturn");
}

__attribute((naked)) void HijackRunNextOpcodeMainEnterDungeon(void) {
  CRASS_SETTINGS.can_skip = false;
  CRASS_SETTINGS.can_speedup = false;
  asm("b RunNextOpcodeMainEnterDungeon");
}

__attribute((naked)) void HijackRunNextOpcodeMainEnterGround(void) {
  CRASS_SETTINGS.can_skip = false;
  CRASS_SETTINGS.can_speedup = false;
  asm("b RunNextOpcodeMainEnterGround");
}

#endif
