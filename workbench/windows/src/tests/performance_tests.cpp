#include "../app.h"

int App::performanceTest(const fs::path& report) {
    json checks = json::array();
    json measurements = json::object();
    std::string previousSession;
    const auto check = [&](std::string name, bool value) {
        checks.push_back({{"name", std::move(name)}, {"passed", value}});
    };
    const auto elapsed = [](Clock::time_point began) {
        return std::chrono::duration<double, std::milli>(Clock::now() - began).count();
    };
    const auto pump = [&](const std::function<bool()>& ready, int milliseconds = 10000) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
        do {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message != WM_QUIT) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            if (ready()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (Clock::now() < deadline);
        return false;
    };
    const auto outputText = [&] {
        const auto length = static_cast<std::size_t>(SendMessageW(output, SCI_GETLENGTH, 0, 0));
        std::string text(length + 1, 0);
        SendMessageW(output, SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(text.data()));
        text.resize(length);
        return text;
    };

    try {
        orbit::OutputLineLimiter limiter(16);
        const auto clipped = limiter.consume(std::string(200, 'x'));
        check("long_line_has_visible_truncation_marker", clipped.starts_with(std::string(16, 'x')) && clipped.find("truncated") != clipped.npos);
        check("truncation_resumes_after_newline", limiter.consume("ignored\nnext\n") == "next\n");
        limiter.reset();
        check("utf8_incomplete_chunk_is_retained", limiter.consume("\xe4\xbd").empty());
        check("utf8_split_chunks_reassemble", limiter.consume("\xa0\n") == "你\n");
        orbit::OutputLineLimiter utf8Limit(4);
        const auto utf8 = utf8Limit.consume("你好\n");
        check("line_limit_keeps_utf8_character_boundaries", utf8.starts_with("你 ") && utf8.find("truncated") != utf8.npos);

        orbit::RunOutputBuffer transport(32768);
        const std::string payload(300000, 'a');
        auto producer = std::async(std::launch::async, [&] {
            for (std::size_t i = 0; i < payload.size(); i += 4096) {
                if (!transport.write(std::string_view(payload).substr(i, std::min<std::size_t>(4096, payload.size() - i)))) return false;
            }
            return true;
        });
        std::string received;
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (producer.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready && Clock::now() < deadline) {
            received += transport.take(8192);
            std::this_thread::yield();
        }
        transport.cancel();
        const bool complete = producer.get();
        for (auto batch = transport.take(); !batch.empty(); batch = transport.take()) received += batch;
        check("bounded_transport_preserves_all_bytes", complete && received == payload);
        check("bounded_transport_applies_backpressure", transport.peakBytes() <= 32768);
        orbit::RunOutputBuffer cancellation(4);
        cancellation.write("1234");
        auto blocked = std::async(std::launch::async, [&] { return cancellation.write("5"); });
        cancellation.cancel();
        check("cancel_unblocks_waiting_producer", blocked.wait_for(std::chrono::seconds(1)) == std::future_status::ready && !blocked.get());

        orbit::ProjectTreeIndex index;
        std::vector<std::string> paths;
        std::set<std::string> openDirectories{"", "src"};
        for (int i = 0; i < 20000; ++i) {
            const auto directory = "src/pkg_" + std::to_string(i / 10);
            openDirectories.insert(directory);
            paths.push_back(directory + "/module_" + std::to_string(i) + ".py");
        }
        auto began = Clock::now();
        index.rebuild(paths);
        measurements["tree_20000_paths_index_build_ms"] = elapsed(began);
        std::vector<double> treeSamples;
        std::vector<TreeRow> rows;
        for (int i = 0; i < 5; ++i) {
            began = Clock::now();
            rows = index.visibleRows(openDirectories, "test-project");
            treeSamples.push_back(elapsed(began));
        }
        measurements["tree_20000_files_2000_folders_flatten_samples_ms"] = treeSamples;
        check("indexed_tree_preserves_all_expanded_rows", rows.size() == 22002 && index.pathCount() == paths.size());
        check("collapsed_tree_does_not_expand_children", index.visibleRows({""}, "test-project").size() == 2);
        index.rebuild({"src/z.py", "README.md", "src/A.py"});
        const auto sorted = index.visibleRows({"", "src"}, "test-project");
        check("tree_orders_folders_first_and_files_case_insensitively", sorted.size() == 5 && sorted[1].path == "src" && sorted[2].path == "src/A.py");

        check("native_project_ready", pump([&] { return !active.empty() && documents.contains(active) && documents.at(active).loaded; }));
        previousSession = readFile(dataDir / L"session.json");
        const auto fixture = exeDir / (L"performance-fixture-" + std::to_wstring(GetCurrentProcessId()));
        fs::create_directories(fixture);
        fs::copy(exeDir / L"sample-project", fixture, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        scanProject(fixture);
        check("performance_project_isolated", pump([&] { return root == fixture && documents.contains("src/main.py") && documents.at("src/main.py").loaded; }));

        clearConsole(false);
        began = Clock::now();
        appendOutput(std::string(3 * 1024 * 1024, 'x'));
        measurements["direct_3MiB_long_line_append_ms"] = elapsed(began);
        check("single_line_cannot_bypass_console_limit", SendMessageW(output, SCI_GETLENGTH, 0, 0) <= orbit::ConsoleView::displayLimit && outputText().find("truncated") != std::string::npos);
        clearConsole(false);
        const std::string lines(63, 'a');
        for (int i = 0; i < 70000; ++i) appendOutput(lines + "\n");
        check("multiline_console_has_hard_byte_limit", SendMessageW(output, SCI_GETLENGTH, 0, 0) <= orbit::ConsoleView::displayLimit);
        check("console_never_retains_undo_history", SendMessageW(output, SCI_GETUNDOACTIONS, 0, 0) == 0 && SendMessageW(output, SCI_GETUNDOCOLLECTION, 0, 0) == 0);
        measurements["multiline_display_bytes_after_4MiB"] = SendMessageW(output, SCI_GETLENGTH, 0, 0);

        began = Clock::now();
        run(false, "", "import sys; sys.stdout.write('x' * (1024 * 1024))");
        const bool oneMiBFinished = pump([&] { return !running; });
        measurements["python_1MiB_single_line_wall_ms"] = elapsed(began);
        measurements["python_1MiB_output_queue_peak_bytes"] = outputBuffer.peakBytes();
        check("real_python_long_output_completes", oneMiBFinished && !problems && outputText().find("truncated") != std::string::npos);
        check("real_python_output_transport_stays_bounded", outputBuffer.peakBytes() <= 1024 * 1024);

        run(false, "", "import sys\nwhile True:\n    sys.stdout.write('x' * 4096)\n");
        const bool launched = pump([&] { std::lock_guard lock(processMutex); return processJob != nullptr; });
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        stopRun();
        check("stop_handles_full_output_backpressure", launched && pump([&] { return !running; }, 5000));

        const auto checkpointDirectory = fixture / L".orbit/checkpoints";
        fs::create_directories(checkpointDirectory);
        for (int i = 0; i < 50; ++i) {
            const auto path = checkpointDirectory / (std::to_wstring(i) + L".json");
            atomicWrite(path, json{{"name", "Snapshot " + std::to_string(i)}, {"date", std::to_string(i)}, {"files", {{"large.py", std::string(1024 * 1024, 'a')}}}}.dump());
        }
        checkpoints.clear();
        began = Clock::now();
        listCheckpoints();
        measurements["checkpoint_listing_ui_submit_ms"] = elapsed(began);
        began = Clock::now();
        check("legacy_checkpoint_metadata_loads_in_background", pump([&] { return checkpoints.size() == 10; }));
        measurements["checkpoint_listing_background_completion_ms"] = elapsed(began);
        check("checkpoint_list_does_not_retain_project_payloads", !checkpoints.empty() && !checkpoints.front().second.contains("files"));
    } catch (const std::exception& error) {
        checks.push_back({{"name", "unexpected_exception"}, {"passed", false}, {"error", error.what()}});
        if (running) { stopRun(); pump([&] { return !running; }); }
    }
    io.drain();
    searchWorker.drain();
    if (!previousSession.empty()) atomicWrite(dataDir / L"session.json", previousSession);
    bool passed = true;
    for (const auto& item : checks) if (!item["passed"].get<bool>()) passed = false;
    atomicWrite(report, json{{"passed", passed}, {"checks", checks.size()}, {"results", checks},
        {"measurements", measurements}, {"scope", "Release build, isolated model and native application workflows. Method and hidden-window pipeline measurements; not input-to-screen latency or GPU profiling."}}.dump(2));
    return passed ? 0 : 1;
}
