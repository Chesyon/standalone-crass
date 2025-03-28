#include <pmdsky.h>
#include <cot.h>
#include "crass.h"

// Special process 100: Change border color
// Based on https://github.com/SkyTemple/eos-move-effects/blob/master/example/process/set_frame_color.asm
/*static int SpChangeBorderColor(short arg1) {
  SetBothScreensWindowsColor(arg1);
  return 0;
}*/

// Special process 255: Return either the current cutscene_skip_settings::crass_kind value or the ID of the OPCODE_MESSAGE_MENU that was skipped.
static int SpGetCrassKind() {
    #if CANCEL_RECOVER_ACTING_SKIP_SYSTEM
    return CRASS_SETTINGS.menu_skipped > 0 ? CRASS_SETTINGS.menu_skipped : CRASS_SETTINGS.crass_kind;
    #else
    return 0;
    #endif
}

// Called for special process IDs 100 and greater.
//
// Set return_val to the return value that should be passed back to the game's script engine. Return true,
// if the special process was handled.
bool CustomScriptSpecialProcessCall(undefined4* unknown, uint32_t special_process_id, short arg1, short arg2, int* return_val) {
  switch (special_process_id) {
    /*case 100:
      *return_val = SpChangeBorderColor(arg1);
      return true;*/
    case 255:
        *return_val = SpGetCrassKind();
        return true;
    default:
      return false;
  }
}
