#include "editor/editor_command.h"

namespace mucom88 {

bool IsEditorCommandEnabled(EditorCommand command,
    const EditorCommandState &state)
{
    switch (command) {
    case EditorCommand::Save:
    case EditorCommand::SaveAs:
        return state.has_document;
    case EditorCommand::Find:
    case EditorCommand::Replace:
    case EditorCommand::GoToLine:
        return state.has_document && state.has_text_view;
    case EditorCommand::Compile:
        return state.has_document && state.has_text_view && state.compiler_ready;
    case EditorCommand::CompileAndPlay:
        return state.has_document && state.has_text_view && state.compiler_ready &&
            state.playback_ui_ready;
    case EditorCommand::Stop:
    case EditorCommand::FastForward:
        return state.playback_ui_ready;
    }
    return false;
}

} // namespace mucom88
