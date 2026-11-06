#include "client.h"

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace orbit::language {
namespace {
constexpr std::size_t queueLimit = 16 * 1024 * 1024;
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::wstring quoteWindows(std::wstring_view value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0; result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
std::size_t cost(const Json& value) {
    if (value.is_string()) return value.get_ref<const std::string&>().size() + 64;
    std::size_t count = 64;
    if (value.is_structured()) for (const auto& item : value) count += cost(item);
    return count;
}
bool writeAll(HANDLE output, std::string_view text) {
    while (!text.empty()) {
        DWORD written = 0;
        if (!WriteFile(output, text.data(), static_cast<DWORD>(std::min<std::size_t>(text.size(), 64 * 1024)), &written, nullptr) || !written) return false;
        text.remove_prefix(written);
    }
    return true;
}
} // namespace
Client::~Client() { stop(); }
void Client::start(ProcessOptions options, std::function<void()> wake) {
    stop();
    {
        std::lock_guard lock(mutex_);
        incoming_.clear(); outgoing_.clear(); queuedBytes_ = incomingBytes_ = 0; peakQueued_ = 0;
        wake_ = std::move(wake);
        stopping_ = false;
        job_ = CreateJobObjectW(nullptr, nullptr);
        if (!job_) throw std::runtime_error("Cannot create language-server job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(job_); job_ = nullptr; stopping_ = true;
            throw std::runtime_error("Cannot configure language-server job");
        }
    }
    worker_ = std::thread([this, options = std::move(options)] { run(options); });
}
void Client::stop() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        if (job_) TerminateJobObject(job_, 0);
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(mutex_);
    if (job_) CloseHandle(job_);
    job_ = nullptr; processId_ = 0;
    outgoing_.clear(); incoming_.clear(); queuedBytes_ = incomingBytes_ = 0;
}
bool Client::send(Json message) {
    const auto bytes = cost(message);
    std::lock_guard lock(mutex_);
    if (stopping_ || bytes > queueLimit || queuedBytes_ + bytes > queueLimit || outgoing_.size() >= 512) return false;
    queuedBytes_ += bytes;
    peakQueued_ = std::max(peakQueued_.load(), queuedBytes_);
    outgoing_.push_back(std::move(message));
    condition_.notify_one();
    return true;
}
std::vector<ProcessEvent> Client::drain() {
    std::lock_guard lock(mutex_);
    std::vector<ProcessEvent> result;
    result.reserve(incoming_.size());
    while (!incoming_.empty()) { result.push_back(std::move(incoming_.front())); incoming_.pop_front(); }
    incomingBytes_ = 0;
    return result;
}
void Client::emit(ProcessEvent event) {
    bool wake = false;
    {
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        const auto bytes = cost(event.message) + event.error.size();
        while (incoming_.size() >= 128 || incomingBytes_ + bytes > queueLimit) {
            // Replace superseded diagnostics/progress, never block a pipe reader on the UI.
            const auto replaceable = std::find_if(incoming_.begin(), incoming_.end(), [](const auto& entry) {
                return entry.kind == ProcessEvent::Kind::Message && entry.message.contains("method") && !entry.message.contains("id");
            });
            if (replaceable != incoming_.end()) {
                incomingBytes_ -= cost(replaceable->message) + replaceable->error.size(); incoming_.erase(replaceable);
            } else {
                incoming_.clear(); incomingBytes_ = 0;
                event = {ProcessEvent::Kind::Stopped, {}, "Python analysis response queue exceeded its budget"};
                stopping_ = true; if (job_) TerminateJobObject(job_, 1); condition_.notify_all(); break;
            }
        }
        wake = incoming_.empty();
        incomingBytes_ += cost(event.message) + event.error.size();
        incoming_.push_back(std::move(event));
    }
    if (wake && wake_) wake_();
}
void Client::run(ProcessOptions options) {
    Handle inputRead, inputWrite, outputRead, outputWrite, errorRead, errorWrite;
    Handle process, primaryThread;
    std::thread writer, errors;
    std::string error;
    try {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        if (!CreatePipe(&inputRead.value, &inputWrite.value, &security, 0) ||
            !CreatePipe(&outputRead.value, &outputWrite.value, &security, 0) ||
            !CreatePipe(&errorRead.value, &errorWrite.value, &security, 0)) throw std::runtime_error("Cannot create language-server pipes");
        SetHandleInformation(inputWrite.value, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outputRead.value, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errorRead.value, HANDLE_FLAG_INHERIT, 0);
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        auto storage = std::make_unique<unsigned char[]>(size);
        auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(storage.get());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size)) throw std::runtime_error("Cannot initialize process attributes");
        const auto cleanup = std::unique_ptr<std::remove_pointer_t<PPROC_THREAD_ATTRIBUTE_LIST>, decltype(&DeleteProcThreadAttributeList)>(attributes, DeleteProcThreadAttributeList);
        HANDLE inherited[]{inputRead.value, outputWrite.value, errorWrite.value};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) throw std::runtime_error("Cannot restrict inherited handles");
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.StartupInfo.wShowWindow = SW_HIDE;
        startup.StartupInfo.hStdInput = inputRead.value;
        startup.StartupInfo.hStdOutput = outputWrite.value;
        startup.StartupInfo.hStdError = errorWrite.value;
        startup.lpAttributeList = attributes;
        std::wstring command = quoteWindows(options.executable.wstring());
        for (const auto& argument : options.arguments) command += L" " + quoteWindows(argument);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(options.executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr,
            options.root.c_str(), &startup.StartupInfo, &info)) throw std::runtime_error("Cannot start Python analysis service");
        process.value = info.hProcess; primaryThread.value = info.hThread;
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || !AssignProcessToJobObject(job_, process.value)) {
                TerminateProcess(process.value, 1); throw std::runtime_error("Cannot own Python analysis process");
            }
            processId_ = info.dwProcessId;
        }
        ResumeThread(primaryThread.value);
        CloseHandle(inputRead.value); inputRead.value = nullptr;
        CloseHandle(outputWrite.value); outputWrite.value = nullptr;
        CloseHandle(errorWrite.value); errorWrite.value = nullptr;
        errors = std::thread([&] {
            char bytes[4096]; DWORD read = 0;
            while (ReadFile(errorRead.value, bytes, sizeof(bytes), &read, nullptr) && read) {}
        });
        writer = std::thread([&] {
            try {
                for (;;) {
                    Json message;
                    {
                        std::unique_lock lock(mutex_);
                        condition_.wait(lock, [&] { return stopping_ || !outgoing_.empty(); });
                        if (stopping_) break;
                        message = std::move(outgoing_.front()); outgoing_.pop_front(); queuedBytes_ -= cost(message);
                    }
                    if (!writeAll(inputWrite.value, frame(message))) break;
                }
            } catch (...) {}
            if (!stopping_) emit({ProcessEvent::Kind::Stopped, {}, "Python analysis request could not be written"});
            {
                std::lock_guard lock(mutex_);
                stopping_ = true;
                // Closing the child-side pipe writers also releases a blocked read.
                // Do not access the owning std::thread object from its own worker.
                if (job_) TerminateJobObject(job_, 1);
            }
            condition_.notify_all();
        });
        emit({ProcessEvent::Kind::Started, {}, {}});
        FrameDecoder decoder;
        char bytes[16384]; DWORD read = 0;
        while (!stopping_ && ReadFile(outputRead.value, bytes, sizeof(bytes), &read, nullptr) && read) {
            for (auto& message : decoder.feed(std::string_view(bytes, read))) emit({ProcessEvent::Kind::Message, std::move(message), {}});
        }
        if (!stopping_) error = "Python analysis service disconnected";
    } catch (const std::exception& exception) { error = exception.what(); }
    {
        std::lock_guard lock(mutex_);
        if (job_) TerminateJobObject(job_, 0);
    }
    // Waking the writer is separate from stopping_, so a terminal event can still be delivered.
    if (!error.empty()) emit({ProcessEvent::Kind::Stopped, {}, error});
    stopping_ = true;
    condition_.notify_all();
    if (writer.joinable()) writer.join();
    if (errors.joinable()) errors.join();
    processId_ = 0;
}
} // namespace orbit::language
