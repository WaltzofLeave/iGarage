#include "language_contract_tests.h"
#include "../language/backend.h"
#include <algorithm>
#include <memory>
#include <utility>

namespace orbit::tests {
namespace {
using namespace language;
struct EndpointState {
    std::vector<Json> sent;
    std::vector<TransportEvent> incoming;
    Transport::Wake wake;
    bool started = false, stopped = false;
    void publish(Json message) {
        incoming.push_back({TransportEvent::Kind::Message, std::move(message), {}});
        if (wake) wake();
    }
    const Json* method(std::string_view name) const {
        const auto found = std::find_if(sent.rbegin(), sent.rend(), [&](const auto& message) { return message.value("method", "") == name; });
        return found == sent.rend() ? nullptr : &*found;
    }
};
// Simulates an independently hosted endpoint. No socket, Windows API, process,
// Scintilla, Python executable, or real remote host is used by these checks.
class ScriptedTransport final : public Transport {
public:
    explicit ScriptedTransport(std::shared_ptr<EndpointState> state) : state_(std::move(state)) {}
    void start(Wake wake) override {
        state_->wake = std::move(wake); state_->started = true;
        state_->incoming.push_back({TransportEvent::Kind::Connected, {}, {}});
        if (state_->wake) state_->wake();
    }
    void stop() noexcept override { state_->stopped = true; state_->wake = {}; state_->incoming.clear(); }
    bool send(Json message) override {
        if (state_->stopped) return false;
        if (message.value("method", "") == "initialize") {
            state_->publish({{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", {{"capabilities", {
                {"positionEncoding", "utf-16"}, {"textDocumentSync", 2}, {"completionProvider", {{"resolveProvider", true}}}}}}}});
        }
        state_->sent.push_back(std::move(message));
        return true;
    }
    std::vector<TransportEvent> drain() override { return std::exchange(state_->incoming, {}); }
    std::size_t peakQueuedBytes() const override { return 0; }
private:
    std::shared_ptr<EndpointState> state_;
};
} // namespace

language::Json languageContractChecks() {
    using namespace language;
    Json checks = Json::array();
    const auto check = [&](std::string name, bool passed) { checks.push_back({{"name", std::move(name)}, {"passed", passed}}); };
    try {
        WorkspaceUris remote("file:///srv/orbit%20project/");
        const auto remoteUri = remote.document("src/中文😀.py");
        check("posix_workspace_uri_keeps_exactly_three_file_scheme_slashes", remote.root() == "file:///srv/orbit%20project" && remoteUri.starts_with("file:///srv/"));
        check("remote_unicode_document_maps_back_to_local_relative_name", remote.relative(remoteUri) == "src/中文😀.py");
        check("remote_diagnostics_cannot_escape_to_neighboring_workspaces", !remote.relative("file:///srv/orbit%20project-other/main.py") && !remote.relative("file:///etc/passwd"));
        check("encoded_parent_traversal_and_uri_queries_are_rejected", !remote.relative("file:///srv/orbit%20project/%2e%2e/private.py") && !remote.relative(remoteUri + "?query"));
        check("posix_backslash_filenames_do_not_alias_frontend_directories", !remote.relative("file:///srv/orbit%20project/src%5cmain.py"));
        check("relative_document_names_reject_absolute_and_parent_paths", !WorkspaceUris::validRelative("../main.py") && !WorkspaceUris::validRelative("/main.py") &&
            !WorkspaceUris::validRelative("C:/main.py") && !WorkspaceUris::validRelative("a//main.py") && !WorkspaceUris::validRelative(std::string("a\0b", 3)));
        WorkspaceUris windows("file:///C:/Projects/Orbit/");
        check("windows_backend_root_is_case_insensitive_with_relative_names_retained", windows.relative("file:///c:/projects/orbit/src/Main.py") == "src/Main.py");
        WorkspaceUris unc("file://server/share/Orbit");
        check("unc_and_posix_root_workspaces_round_trip", unc.relative(unc.document("src/main.py")) == "src/main.py" && WorkspaceUris("file:///").document("main.py") == "file:///main.py");

        auto state = std::make_shared<EndpointState>();
        Service service;
        int wakes = 0;
        auto options = pyreflySession(remote.root(), "Remote workspace", "/opt/project/.venv/bin/python");
        service.start(std::make_unique<ScriptedTransport>(state), options, [&] { ++wakes; });
        const std::string path = "src/中文😀.py", code = "名称 = 1\n名称.bi";
        service.open(path, code, 7);
        service.poll(); service.poll();
        const auto* initialize = state->method("initialize");
        check("session_initializes_over_injected_transport_without_local_backend_process", service.ready() && state->started && service.processId() == 0 && wakes > 0);
        check("remote_initialize_uses_backend_uri_and_null_parent_process_id", initialize && (*initialize)["params"]["rootUri"] == remote.root() && (*initialize)["params"]["processId"].is_null());
        check("backend_interpreter_configuration_does_not_use_frontend_paths", initialize && (*initialize)["params"]["initializationOptions"]["pythonPath"] == "/opt/project/.venv/bin/python");
        const auto* opened = state->method("textDocument/didOpen");
        check("unsaved_document_snapshot_uses_remote_uri_and_frontend_revision", opened && (*opened)["params"]["textDocument"]["uri"] == remoteUri &&
            (*opened)["params"]["textDocument"]["text"] == code && (*opened)["params"]["textDocument"]["version"] == 7);
        service.request(RequestKind::Completion, path, code.size());
        const auto request = *state->method("textDocument/completion");
        check("remote_completion_positions_are_utf16_with_frontend_byte_carets", request["params"]["position"] == encode({1, 5}));
        state->publish({{"id", request["id"]}, {"result", Json::array({{{"label", "bit_length"}, {"kind", 2}}})}});
        const auto results = service.poll();
        check("remote_completion_response_keeps_frontend_document_identity", results.size() == 1 && results.front().path == path && results.front().caret == code.size() &&
            results.front().revision == 7 && completions(results.front().value).front().label == "bit_length");

        const auto diagnostic = Json{{"range", {{"start", encode({1, 0})}, {"end", encode({1, 2})}}}, {"message", "Remote type diagnostic"}, {"severity", 1}};
        const auto publish = [&](std::string uri, int version) {
            state->publish({{"method", "textDocument/publishDiagnostics"}, {"params", {{"uri", std::move(uri)}, {"version", version}, {"diagnostics", Json::array({diagnostic})}}}});
        };
        publish(remoteUri, 7); service.poll();
        check("remote_diagnostic_uri_is_mapped_to_the_frontend_document", service.document(path)->diagnostics.size() == 1);
        publish("file:///srv/private/secret.py", 7); service.poll();
        check("unmapped_remote_diagnostics_never_create_frontend_files", service.documents().size() == 1 && service.document(path)->diagnostics.size() == 1);
        service.request(RequestKind::Completion, path, code.size());
        const auto staleId = (*state->method("textDocument/completion"))["id"];
        service.change(path, code.size(), 0, "t", 8);
        const auto* changed = state->method("textDocument/didChange");
        check("remote_edits_send_incremental_ranges_instead_of_full_snapshots", changed && (*changed)["params"]["contentChanges"][0]["range"]["start"] == encode({1, 5}) &&
            (*changed)["params"]["contentChanges"][0]["text"] == "t");
        const auto* cancellation = state->method("$/cancelRequest");
        state->publish({{"id", staleId}, {"result", Json::array({{{"label", "stale"}}})}});
        const auto staleResults = service.poll();
        check("remote_responses_after_edit_are_canceled_and_discarded", cancellation && (*cancellation)["params"]["id"] == staleId && staleResults.empty() && service.staleResponses() > 0);
        publish(remoteUri, 7); service.poll();
        check("old_remote_diagnostic_versions_do_not_restore_stale_squiggles", service.document(path)->diagnostics.empty());
        Json configurationRequest{{"id", 9001}, {"method", "workspace/configuration"}};
        configurationRequest["params"]["items"] = Json::array({{{"section", "python.analysis"}}, {{"section", "missing"}}});
        state->publish(std::move(configurationRequest));
        service.poll();
        const auto configuration = state->sent.back();
        check("configuration_sections_are_resolved_from_backend_settings", configuration["id"] == 9001 && configuration["result"][0]["autoImportCompletions"] == true && configuration["result"][1].is_null());
        state->incoming.push_back({TransportEvent::Kind::Disconnected, {}, "Simulated remote disconnection"});
        service.poll();
        check("remote_disconnect_leaves_editor_mirrors_available_without_analysis", !service.ready() && service.document(path)->buffer.text() == code + "t" && !service.request(RequestKind::Hover, path, 0));
        service.stop();
        check("session_stops_owned_transport_and_releases_document_mirrors", state->stopped && service.documents().empty());
        auto local = pyreflySession("file:///C:/Orbit", "Local workspace", "C:/Python/python.exe", 123);
        check("local_backend_parent_pid_is_explicit_not_inferred_by_session", local.parentProcessId == 123u && !options.parentProcessId);
        auto rejectedState = std::make_shared<EndpointState>();
        auto invalidOptions = options; invalidOptions.rootUri = "file:///srv/path%5calias";
        service.start(std::make_unique<ScriptedTransport>(rejectedState), invalidOptions, [] {});
        check("invalid_backend_uri_is_rejected_before_a_transport_connects", !rejectedState->started && !service.ready() && !service.status().empty());
    } catch (const std::exception& error) {
        checks.push_back({{"name", "language_contract_exception"}, {"passed", false}, {"error", error.what()}});
    }
    return checks;
}
} // namespace orbit::tests

#ifdef ORBIT_LANGUAGE_CONTRACT_STANDALONE
#include <iostream>
int main() {
    const auto checks = orbit::tests::languageContractChecks();
    const bool passed = std::all_of(checks.begin(), checks.end(), [](const auto& check) { return check["passed"].template get<bool>(); });
    std::cout << orbit::language::Json{{"passed", passed}, {"checks", checks.size()}, {"results", checks}}.dump(2) << '\n';
    return passed ? 0 : 1;
}
#endif
