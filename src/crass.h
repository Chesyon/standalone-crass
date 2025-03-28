#pragma once

#include "extern.h"
#include <cot.h>
#include <pmdsky.h>

/***************************************
 *  Cancel Recover Acting Skip System  *
 ***************************************/

#define CANCEL_RECOVER_ACTING_SKIP_SYSTEM 1

#if CANCEL_RECOVER_ACTING_SKIP_SYSTEM

enum opcode_parse_kind {
  OPCODE_PARSE_MANUAL = 0,
  OPCODE_PARSE_AUTO = 1,
  OPCODE_PARSE_DUNGEON = 2,
  OPCODE_PARSE_GROUND = 3,
  OPCODE_PARSE_SP = 4,
  OPCODE_PARSE_SWITCH_MENU = 5,
  OPCODE_PARSE_MESSAGE_MENU = 6,
  OPCODE_PARSE_CALL_COMMON = 7
};

enum crass_kind {
  CRASS_DEFAULT =
      0, // The cutscene skip will perform its default settings: Attempting to
         // skip naturally, and if it fails, it performs a speedup.
  CRASS_OFF = 1, // The cutscene cannot be skipped or sped up no matter what.
  CRASS_SPEEDUP = 2, // The cutscene skip will not attempt a natural skip and
                     // instead force a speedup.
  CRASS_ERROR = 99,  // An error has occured with an attempted cutscene script;
                     // this is not meant to be supplied as a parameter.
  CRASS_REDIRECT =
      100 // The cutscene will perform the same operations as CRASS_DEFAULT,
          // however, a skip will redirect control flow to ROUTINE_MAP_TEST.
          // Additionally, anything greater than or equal to 100 will be
          // treated as a redirection, and this can be checked within
          // ROUTINE_MAP_TEST.
};

struct crass_settings {
  struct ssb_runtime_info
      return_info; // Used to return control flow back to either the next opcode
                   // after OPCODE_SUPERVISION_EXECUTE_ACTING_SUB or via
                   // OPCODE_RETURN in ROUTINE_MAP_TEST.
  enum crass_kind
      crass_kind; // To indicate the kind of cutscene skip taken place. Only
                  // really intended to be used in ROUTINE_MAP_TEST, when
                  // performing manual redirection.
  uint16_t menu_skipped; // For certain important menus that were skipped. This
                         // takes priority over the crass_kind field when
                         // performing manual redirection.
  bool can_skip;         // If the current cutscene being played can be skipped.
  bool can_speedup;      // If the current cutscene being played can be sped up,
                         // usually due to `can_skip` being false.
  bool skip_active;    // A cutscene skip has been activated by pressing Select.
  bool speedup_active; // A cutscene speedup has been activated by pressing
                       // Select (and sometimes originally intending to be a
                       // skip, but falling back to a speedup).
  bool end_after_cutscene; // The opcode following the current cutscene being
                           // skipped in Unionall is OPCODE_HOLD or OPCODE_END,
                           // so additional checks must be performed to avoid
                           // skipping into a softlock.
  bool enter_dungeon; // Skipping the current cutscene will lead to a dungeon.
  bool enter_ground;  // Skipping the current cutscene will lead to an overworld
                      // transition.
  bool redirect;      // Skipping the current cutscene will pass control flow to
                 // ROUTINE_MAP_TEST. Control flow can be passed back to the
                 // opcode after the skipped cutscene by using OPCODE_RETURN. A
                 // redirect will also take priority over entering a dungeon or
                 // overworld.
  bool coroutine_hijack; // Indicates that a new coroutine will be loaded due to
                         // a cutscene skip.
};

extern struct crass_settings CRASS_SETTINGS;

#endif
