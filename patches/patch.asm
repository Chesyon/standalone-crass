// Replace the "GetMovePower" function with a custom one.
// Since a branch is inserted at the start of the function, the function is practically
// replaced with our own. The "b" instruction doesn't modify the link register, so
// execution will continue after the call to `GetMovePower` once our function returns.

.nds
.include "symbols.asm"

.open "arm9.bin", arm9_start
    ; Cutscene skip shenanigans
    .ifdef TryCutsceneSkipScan
    .org CreateDefaultScriptEngineBox
        bl CreateScriptEngineDialogueBox
    .org ShowStringInDialogueBoxCallsite1
        bl ShowScriptEngineStringInDialogueBox
    .org IsValidPortraitCallsite
        bl ShouldShowScriptEnginePortrait
    .endif
.close

.open "overlay11.bin", overlay11_start
    ; More cutscene skip shenanigans
    .ifdef TryCutsceneSkipScan
    .org GroundSupervisionExecuteRequestCancelCallsite
        b FinalCutsceneSkipCheck
    .org GetCoroutineInfoCallsite
        bl GetRecoverCoroutineInfo
    .org InitScriptRoutineFromCoroutineInfoCallsite
        bl CustomInitScriptRoutineFromCoroutineInfo
    .org DebugPrintCallsite
    .area 0x8
        bl DebugPrintGameCancel
        nop
    .endarea
    .org GetSceneNameCallsite
        bl CustomGetSceneName
    .org SelectPressBranchEqual
        beq CheckSelectPressTrampoline
    .org OpcodeMainEnterDungeonBranchEqual
        beq HijackRunNextOpcodeMainEnterDungeon
    .org OpcodeMainEnterGroundBranch
        b HijackRunNextOpcodeMainEnterGround
    .org OpcodeEndBranchReturn ; end
        b HijackRunNextOpcodeControlStatement
    .org OpcodeMovementSpeed ; move
        bl GetMovementSpeedParam
    .org OpcodeSlidingSpeed ; slide
        bl GetMovementSpeedParam
    .org TurnOpcodeSwitchStatementSetup ; turn
        b TrySpeedUpTurnSpeedParamTrampoline
    .org OpcodeHeightSpeed ; height
        bl GetMovementSpeedParam
    .org OpcodeWaitSpeed ; wait
        bl GetWaitTime
    .org OpcodeBgmWaitSpeed ; bgm1
        bl GetWaitTime
    .org OpcodeBgm2WaitSpeed ; bgm2
        bl GetWaitTime
    .org OpcodeSetWaitModeStuff ; setwaitmode
    .area 0x2C
        ldrh r0,[r6,#0x2]
        bl ScriptParamToInt
        mov r11,r0
        ldrh r0,[r6,#0x0]
        bl ScriptParamToInt
        mov r1,r11
        bl TryMessageSetWaitMode
        nop
        nop
        nop
        nop
    .endarea
    .org ShowStringInDialogueBoxCallsite2
        bl ShowScriptEngineStringInDialogueBox
    .org ShowStringInDialogueBoxCallsite3
        bl ShowScriptEngineStringInDialogueBox
    .endif
.close
