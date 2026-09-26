#include "editor/editor_command.h"

namespace mucom88 {

namespace {

bool IsActive(PlaybackState state)
{
    return state == PlaybackState::Preparing ||
        state == PlaybackState::Buffering ||
        state == PlaybackState::Playing ||
        state == PlaybackState::Paused ||
        state == PlaybackState::Draining ||
        state == PlaybackState::DeviceLost ||
        state == PlaybackState::Failed;
}

} // namespace

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
            state.playback_ui_ready &&
            state.playback_state != PlaybackState::Stopping;
    case EditorCommand::PauseResume:
        return state.playback_ui_ready &&
            (state.playback_state == PlaybackState::Buffering ||
             state.playback_state == PlaybackState::Playing ||
             state.playback_state == PlaybackState::Paused);
    case EditorCommand::Stop:
        return state.playback_ui_ready && IsActive(state.playback_state) &&
            state.playback_state != PlaybackState::Stopping;
    case EditorCommand::FastForward:
        return state.playback_ui_ready &&
            (state.playback_state == PlaybackState::Buffering ||
             state.playback_state == PlaybackState::Playing ||
             state.playback_state == PlaybackState::Paused);
    case EditorCommand::Reconnect:
        return state.playback_ui_ready && state.reconnect_available &&
            state.playback_state == PlaybackState::DeviceLost;
    }
    return false;
}

} // namespace mucom88
