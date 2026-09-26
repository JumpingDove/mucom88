#ifndef MUCOM88_EDITOR_COMMAND_H
#define MUCOM88_EDITOR_COMMAND_H

#include "editor/playback_session.h"

namespace mucom88 {

enum class EditorCommand {
    Save,
    SaveAs,
    Find,
    Replace,
    GoToLine,
    Compile,
    CompileAndPlay,
    PauseResume,
    Stop,
    FastForward,
    Reconnect
};

struct EditorCommandState {
    bool has_document = false;
    bool has_text_view = false;
    bool compiler_ready = false;
    bool playback_ui_ready = false;
    bool reconnect_available = false;
    PlaybackState playback_state = PlaybackState::Idle;
};

bool IsEditorCommandEnabled(EditorCommand command,
    const EditorCommandState &state);

} // namespace mucom88

#endif
