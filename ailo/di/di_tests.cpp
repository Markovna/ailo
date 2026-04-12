#include "di.h"
#include <iostream>
#include <cassert>
#include <type_traits>

// ============================================================================
// Test types
// ============================================================================

using namespace ailo;

// A simple interface
struct ILogger {
    virtual ~ILogger() = default;
    virtual void log(const char* msg) = 0;
};

// An implementation
struct ConsoleLogger : ILogger {
    void log(const char* msg) override {
        std::cout << "[ConsoleLogger] " << msg << "\n";
    }
};

// Another interface
struct IDatabase {
    virtual ~IDatabase() = default;
    virtual int query() = 0;
};

// Implementation that depends on ILogger
struct SqlDatabase : IDatabase {
    std::shared_ptr<ILogger> logger_;

    SqlDatabase(std::shared_ptr<ILogger> logger) : logger_(std::move(logger)) {
        logger_->log("SqlDatabase constructed");
    }

    int query() override {
        logger_->log("executing query");
        return 42;
    }
};

// A service that depends on both ILogger and IDatabase
struct AppService {
    std::shared_ptr<ILogger> logger_;
    std::shared_ptr<IDatabase> db_;

    AppService(std::shared_ptr<ILogger> logger, std::shared_ptr<IDatabase> db)
        : logger_(std::move(logger)), db_(std::move(db)) {
        logger_->log("AppService constructed");
    }

    int run() {
        logger_->log("AppService running");
        return db_->query();
    }
};

// A simple value type with no dependencies
struct Config {
    Config() = default;

    int port = 8080;
};

// A service that takes ownership via unique_ptr
struct ServiceWithUniqueLogger {
    std::unique_ptr<ILogger> logger_;

    explicit ServiceWithUniqueLogger(std::unique_ptr<ILogger> logger)
        : logger_(std::move(logger)) {}

    void work() { logger_->log("working (unique_ptr)"); }
};

// A service that holds a non-owning raw pointer (requires singleton/instance scope)
struct ServiceWithRawLogger {
    ILogger* logger_;

    explicit ServiceWithRawLogger(ILogger* logger) : logger_(logger) {}

    void work() { logger_->log("working (T*)"); }
};

// A service that holds a reference (requires singleton/instance scope)
struct ServiceWithRefLogger {
    ILogger& logger_;

    explicit ServiceWithRefLogger(ILogger& logger) : logger_(logger) {}

    void work() { logger_.log("working (T&)"); }
};

// ============================================================================
// Tests
// ============================================================================

void test_basic_binding() {
    std::cout << "=== test_basic_binding ===\n";

    auto injector = di::make_injector(
        di::bind<ILogger>.to<ConsoleLogger>()
    );

    auto logger = injector.create<std::shared_ptr<ILogger>>();
    logger->log("hello from basic binding test");

    std::cout << "\n";
}

void test_transitive_dependencies() {
    std::cout << "=== test_transitive_dependencies ===\n";

    auto injector = di::make_injector(
        di::bind<ILogger>.to<ConsoleLogger>(),
        di::bind<IDatabase>.to<SqlDatabase>()
    );

    // Creating IDatabase should automatically resolve ILogger
    auto db = injector.create<std::shared_ptr<IDatabase>>();
    int result = db->query();
    assert(result == 42);
    std::cout << "query result: " << result << "\n\n";
}

void test_deep_graph() {
    std::cout << "=== test_deep_graph ===\n";

    auto injector = di::make_injector(
        di::bind<ILogger>.to<ConsoleLogger>(),
        di::bind<IDatabase>.to<SqlDatabase>()
    );

    // AppService depends on ILogger + IDatabase.
    // IDatabase (SqlDatabase) depends on ILogger.
    // The injector resolves the whole graph.
    auto service = injector.create<std::shared_ptr<AppService>>();
    int result = service->run();
    assert(result == 42);
    std::cout << "service result: " << result << "\n\n";
}

void test_singleton() {
    std::cout << "=== test_singleton ===\n";

    auto injector = di::make_injector(
        di::bind<ILogger>.to_singleton<ConsoleLogger>()
    );

    auto logger1 = injector.create<std::shared_ptr<ILogger>>();
    auto logger2 = injector.create<std::shared_ptr<ILogger>>();

    // Both should be the same instance
    assert(logger1.get() == logger2.get());
    std::cout << "logger1 == logger2: " << (logger1.get() == logger2.get()) << "\n";

    logger1->log("singleton test");
    std::cout << "\n";
}

void test_instance_binding() {
    std::cout << "=== test_instance_binding ===\n";

    auto my_logger = std::make_shared<ConsoleLogger>();
    my_logger->log("I was created manually");

    auto injector = di::make_injector(
        di::bind<ILogger>.to_value(my_logger)
    );

    auto resolved = injector.create<std::shared_ptr<ILogger>>();
    assert(resolved.get() == my_logger.get());
    std::cout << "same instance: " << (resolved.get() == my_logger.get()) << "\n";

    resolved->log("resolved from injector");
    std::cout << "\n";
}

void test_auto_construction() {
    std::cout << "=== test_auto_construction ===\n";

    // No bindings at all — Config has a 0-arg constructor,
    // so the injector should auto-construct it.
    auto injector = di::make_injector();

    auto config = injector.create<std::shared_ptr<Config>>();
    std::cout << "config.port = " << config->port << "\n";
    assert(config->port == 8080);

    std::cout << "\n";
}

void test_unique_ptr_injection() {
    std::cout << "=== test_unique_ptr_injection ===\n";

    auto injector = di::make_injector(
        di::bind<ILogger>.to<ConsoleLogger>()
    );

    // Top-level create as unique_ptr
    auto logger = injector.create<std::unique_ptr<ILogger>>();
    assert(logger != nullptr);
    logger->log("created as unique_ptr");

    // Each create yields a distinct instance (unique scope)
    auto a = injector.create<std::unique_ptr<ILogger>>();
    auto b = injector.create<std::unique_ptr<ILogger>>();
    assert(a.get() != b.get());

    // Constructor injection of unique_ptr<ILogger>
    auto service = injector.create<std::shared_ptr<ServiceWithUniqueLogger>>();
    assert(service != nullptr);
    service->work();

    std::cout << "\n";
}

void test_raw_pointer_injection() {
    std::cout << "=== test_raw_pointer_injection ===\n";

    auto injector = di::make_injector(
        di::bind<ILogger>.to_singleton<ConsoleLogger>()
    );

    // Top-level create as T*
    ILogger* p = injector.create<ILogger*>();
    assert(p != nullptr);
    p->log("accessed as T*");

    // Must be the same object as the singleton shared_ptr
    auto sp = injector.create<std::shared_ptr<ILogger>>();
    assert(p == sp.get());

    // Constructor injection of ILogger*
    auto service = injector.create<std::shared_ptr<ServiceWithRawLogger>>();
    assert(service != nullptr);
    service->work();

    std::cout << "\n";
}

void test_reference_injection() {
    std::cout << "=== test_reference_injection ===\n";

    auto injector = di::make_injector(
        di::bind<ILogger>.to_singleton<ConsoleLogger>()
    );

    // Constructor injection of ILogger&
    auto service = injector.create<std::shared_ptr<ServiceWithRefLogger>>();
    assert(service != nullptr);
    service->work();

    // The reference inside the service is the singleton
    auto sp = injector.create<std::shared_ptr<ILogger>>();
    assert(&service->logger_ == sp.get());

    std::cout << "\n";
}

int main() {
    test_basic_binding();
    test_transitive_dependencies();
    test_deep_graph();
    test_singleton();
    test_instance_binding();
    test_auto_construction();
    test_unique_ptr_injection();
    test_raw_pointer_injection();
    test_reference_injection();

    std::cout << "All tests passed!\n";
    return 0;
}