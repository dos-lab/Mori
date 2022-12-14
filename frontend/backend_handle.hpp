#pragma once

#include "includes/stdlibs.hpp"

#include "includes/backend.hpp"
#include "includes/context.hpp"
#include "includes/logging.hpp"
#include "includes/memory_status.hpp"
#include "includes/memory_event.hpp"
#include "includes/exceptions.hpp"

namespace mori {

extern "C" int backend_entry(std::unique_ptr<Backend>& ptr, const Context& _context);

struct BackendHandle {
    bool inited = false;

    Logger* logger;

    BackendHandle() = default;
    BackendHandle(BackendHandle&& backend_handle) = default;

    void setLogger(Logger* _logger) {
        if (inited) throw inited_exception();
        logger = _logger;
    }

    virtual void init() = 0;

    virtual void registerOperator(const OperatorStatus& operator_status) = 0;

    virtual void submitEvent(const MemoryEvent& event) = 0;

    virtual std::vector<ScheduleEvent> getScheduleEvents() = 0;

    virtual void unregisterOperator(const std::string& op) = 0;

    virtual void terminate() {
        if (!inited) throw uninited_exception();

        inited = false;
    }

    virtual ~BackendHandle() {
        logger = nullptr;
    }
};  // struct BackendHandle  

struct LocalBackendHandle : public BackendHandle {
    std::unique_ptr<Backend> backend;

    LocalBackendHandle(Context _context) {}
    LocalBackendHandle(LocalBackendHandle&& backend_handle) {
        backend = std::move(backend_handle.backend);
    }

    virtual void init() {
        if (inited) return;
        backend->init();
        inited = true;
    }

    virtual void registerOperator(const OperatorStatus& operator_status) {
        backend->registerOperator(operator_status);
    }

    virtual void submitEvent(const MemoryEvent& event) {
        (*logger)<<LogLevel::info<<"Submiting of event "<<event;
        logger->flush();
        backend->submitEvent(event);
    }

    virtual std::vector<ScheduleEvent> getScheduleEvents() {
        return backend->getScheduleEvents();
    }

    virtual void unregisterOperator(const std::string& op) {
        backend->unregisterOperator(op);
    }

    virtual void terminate() {
        if (!inited) throw uninited_exception();

        backend->terminate();

        BackendHandle::terminate();
    }

    virtual ~LocalBackendHandle() {
        backend.reset();
    }
};  // struct LocalBackendHandle

#ifdef ENABLE_INTEGRATED_BACKEND
/**
 * IntegratedHandle
 */
struct IntegratedBackendHandle : public LocalBackendHandle {
    IntegratedBackendHandle(const Context& _context): LocalBackendHandle(_context) {
        int ret = backend_entry(backend, _context);
		if (ret != 0) throw backend_exception();
    }
};  // struct IntegratedBackendHandle
#endif

/**
 * DLBackendHandle
 * Handle for dynamic library backend.
 */
struct DylibBackendHandle : public LocalBackendHandle {
    void* hInst;

    DylibBackendHandle(const Context& _context): LocalBackendHandle(_context) {
        typedef int(*BackendEntryType)(std::unique_ptr<Backend>&, const Context&);

        const std::string& path = _context.at("path");
        std::string obj_path = std::string(path.begin() + 8, path.end()).c_str();

		hInst = dlopen(obj_path.c_str(), RTLD_LAZY);
		if (!hInst) throw dynamic_library_exception("Failed to open backend dynamic library.");
		BackendEntryType backend_entry = (BackendEntryType)dlsym(hInst, "backend_entry");

		int ret;
		if (backend_entry) ret = backend_entry(backend, _context);
		else throw dynamic_library_exception("Failed to access backend entry.");

		if (ret != 0) throw dynamic_library_exception("Failed to enter backend.");
    }

    virtual ~DylibBackendHandle() {
        backend.reset();
        dlclose(hInst);
    }
};  // struct DylibBackendHandle

/**
 * RemoteBackendHandle
 * Handle for remote library backend.
 * Currently, HTTP Mori Server.
 */

// struct SharedMemoryBackendHandle : public BackendHandle {
//     SharedMemoryBackendHandle(const std::string& path) {}
//     virtual ~SharedMemoryBackendHandle() {}
// };  // struct SharedMemoryBackendHandle

// struct UnixSocketBackendHandle : public BackendHandle {
//     UnixSocketBackendHandle(const std::string& path) {}
//     ~UnixSocketBackendHandle() {}
// };  // struct UnixSocketBackendHandle

// struct HTTPBackendHandle : public BackendHandle {
//     HTTPBackendHandle(const std::string& path) {}
//     ~HTTPBackendHandle() {}
// };  // struct HTTPBackendHandle

static std::unique_ptr<BackendHandle> make_backend_handle(const Context& context) {
    // if (!context.isParamExists("path")) throw context_missing();

    const std::string& path = context.at("path");
#ifdef ENABLE_INTEGRATED_BACKEND
    if (path.find("int://") == 0) return std::unique_ptr<BackendHandle>(new IntegratedBackendHandle(context));
#endif
    if (path.find("dylib://") == 0) return std::unique_ptr<BackendHandle>(new DylibBackendHandle(context));
    //else if (path.find("http://") == path.begin()) return std::unique_ptr<BackendHandle>(new RemoteBackendHandle(_context));
    //else if (path.find("https://") == path.begin()) return std::unique_ptr<BackendHandle>(new RemoteBackendHandle(_context));
    else throw context_invalid("path");
}

}   // namespace mori