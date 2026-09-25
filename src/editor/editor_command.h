#ifndef MUCOM88_EDITOR_COMMAND_H
#define MUCOM88_EDITOR_COMMAND_H

namespace mucom88 {

enum class EditorCommand {
    Save,
    SaveAs,
    Find,
    Replace,
    GoToLine,
    Compile,
    CompileAndPlay,
    Stop,
    FastForward
};

struct EditorCommandState {
    bool has_document = false;
    bool has_text_view = false;
    bool compiler_ready = false;
    bool playback_ui_ready = false;
};

bool IsEditorCommandEnabled(EditorCommand command,
    const EditorCommandState &state);

} // namespace mucom88

#endif
