#include "editor/editor_command.h"
#include "tests/test_support.h"

int main()
{
    mucom88_test::TestContext test;
    mucom88::EditorCommandState state;
    CHECK(test, !mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Save, state));
    state.has_document = true;
    CHECK(test, mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Save, state));
    CHECK(test, !mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Find, state));
    state.has_text_view = true;
    CHECK(test, mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Find, state));
    CHECK(test, !mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Compile, state));
    state.compiler_ready = true;
    CHECK(test, mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Compile, state));
    CHECK(test, !mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::CompileAndPlay, state));
    CHECK(test, !mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Stop, state));
    state.playback_ui_ready = true;
    CHECK(test, mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::CompileAndPlay, state));
    CHECK(test, !mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::PauseResume, state));
    CHECK(test, !mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Stop, state));
    state.playback_state = mucom88::PlaybackState::Playing;
    CHECK(test, mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::PauseResume, state));
    CHECK(test, mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Stop, state));
    CHECK(test, mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::FastForward, state));
    CHECK(test, !mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Reconnect, state));
    state.playback_state = mucom88::PlaybackState::DeviceLost;
    state.reconnect_available = true;
    CHECK(test, mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Reconnect, state));
    CHECK(test, !mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::FastForward, state));
    state.playback_state = mucom88::PlaybackState::Stopping;
    CHECK(test, !mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::CompileAndPlay, state));
    CHECK(test, !mucom88::IsEditorCommandEnabled(
        mucom88::EditorCommand::Stop, state));
    return test.ExitCode();
}
