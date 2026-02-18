# Quick Reference Card

> ℹ️ **Note:** This reference is based on the [Boost.Beast HTTP server example](https://www.boost.org/doc/libs/release/libs/beast/example/http/server/async/http_server_async.cpp). The original code is Copyright (c) Vinnie Falco and the Boost team.

## File Structure

```
my-project/
├── CMakeLists.txt          # Build config
├── README.md               # Project docs
├── incl/
│   ├── listener.h         # Server (class declaration)
│   ├── session.h          # Connection handler (class declaration)
│   └── utils.h            # Helpers
└── src/
    ├── main.cpp           # Entry point
    ├── listener.cpp       # Server implementation
    ├── session.cpp        # Connection handler implementation
    └── utils.cpp          # Helper implementations
```

## Key Concepts

### io_context - The Event Loop
```cpp
boost::asio::io_context ioc;   // Create
ioc.run();                      // Start (blocks until done)
```

### Accepting Connections
```cpp
// Setup
boost::asio::ip::tcp::acceptor acceptor(ioc, endpoint);

// Accept (async)
acceptor.async_accept(callback);
```

### Reading HTTP
```cpp
beast::flat_buffer buffer;
http::request<http::string_body> req;

http::async_read(socket, buffer, req, callback);
```

### Writing HTTP
```cpp
http::response<http::string_body> res;
res.result(http::status::ok);
res.body() = "Hello";
res.prepare_payload();

http::async_write(socket, res, callback);
```

## Thread Safety

### Without Strand (Dangerous)
```cpp
// Multiple threads can call these simultaneously
void read() { ... }    // Thread 1
void write() { ... }   // Thread 2  <- RACE CONDITION!
```

### With Strand (Safe)
```cpp
// All operations serialized through strand
auto strand = make_strand(ioc);
post(strand, [] { read(); });   // Executes first
post(strand, [] { write(); });  // Waits for first
```

## Object Lifetime

### Wrong Way
```cpp
void handle() {
    session s(socket);  // Stack object
    s.start_async();    // Async operation started
}  // s destroyed! CRASH when callback fires!
```

### Right Way
```cpp
class session : public enable_shared_from_this<session> {
    void start() {
        auto self = shared_from_this();  // Keep alive
        async_op([self] { ... });        // Safe!
    }
};

void handle() {
    auto s = make_shared<session>(socket);  // Heap
    s->start();
}  // s stays alive (reference held by async op)
```

## Common Patterns

### Async Chain
```cpp
void step1() {
    async_op([this](ec) {
        if (!ec) step2();
    });
}

void step2() {
    async_op([this](ec) {
        if (!ec) step3();
    });
}
```

### Error Handling
```cpp
void callback(error_code ec) {
    if (ec == http::error::end_of_stream) {
        // Client closed normally
        return;
    }
    if (ec) {
        // Real error
        log(ec.message());
        return;
    }
    // Success
}
```

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Running

```bash
./http-server-async <address> <port> <doc_root> <threads>
./http-server-async 0.0.0.0 8080 ./public 4
```

## Testing

```bash
# Test with curl
curl http://localhost:8080/index.html

# Test with browser
http://localhost:8080/

# Test keep-alive (multiple requests on one connection)
curl -v http://localhost:8080/ http://localhost:8080/style.css
```

## Debugging Tips

1. **Enable verbose logging**:
   ```cpp
   // Add to main.cpp
   #define BOOST_ASIO_ENABLE_HANDLER_TRACKING
   ```

2. **Check error codes**:
   ```cpp
   if (ec) {
       std::cerr << "Error: " << ec.message() 
                 << " (" << ec.value() << ")\n";
   }
   ```

3. **Use gdb**:
   ```bash
   gdb ./http-server-async
   (gdb) run 0.0.0.0 8080 . 1
   (gdb) bt  # Backtrace when crash
   ```

## Key Headers

```cpp
#include <boost/asio.hpp>           // Core I/O
#include <boost/asio/ip/tcp.hpp>    // TCP sockets
#include <boost/asio/strand.hpp>    // Thread safety
#include <boost/beast/core.hpp>     // Core utilities
#include <boost/beast/http.hpp>     // HTTP protocol
```

## Type Aliases Used

```cpp
// In this project, we use fully qualified names:
boost::asio::io_context
boost::asio::ip::tcp::socket
boost::asio::ip::tcp::acceptor
boost::beast::tcp_stream
boost::beast::error_code
boost::beast::flat_buffer
boost::beast::http::request<...>
boost::beast::http::response<...>
```

## Lifetimes Cheat Sheet

| Object | Who Creates | Who Destroys | Notes |
|--------|------------|--------------|-------|
| io_context | main() | Automatic | Lives for entire program |
| listener | make_shared | Automatic | Keeps itself alive |
| session | listener | Automatic | Per connection |
| socket | OS | beast::tcp_stream | Managed by stream |
| buffer | session | Automatic | Reset each request |
| request | session | Automatic | Reused (keep-alive) |

## Async Operation Signatures

```cpp
// Accept
void(error_code, tcp::socket)

// Read
void(error_code, size_t bytes)

// Write  
void(error_code, size_t bytes)

// Timer
void(error_code)
```

## HTTP Status Codes

```cpp
http::status::ok                    // 200
http::status::bad_request           // 400
http::status::not_found            // 404
http::status::internal_server_error // 500
```

## Common Errors

| Error | Meaning | Solution |
|-------|---------|----------|
| `address_in_use` | Port taken | Use different port or wait |
| `connection_reset` | Client closed | Normal, handle gracefully |
| `eof` | End of file/stream | Client closed normally |
| `timed_out` | Operation timeout | Increase timeout or handle |
| `bad_descriptor` | Socket closed | Check logic flow |

## Performance Tips

1. **Use strands** - Don't use mutexes with Asio
2. **Buffer reuse** - Clear and reuse buffers
3. **Move semantics** - Use `std::move` for sockets
4. **Thread count** - Usually `cores + 1` is optimal
5. **Zero-copy** - Use `file_body` for files

## Resources

- **Docs**: boost.org/doc/libs/release/doc/html/boost_asio/
- **Examples**: boost.org/doc/libs/release/libs/beast/example/
- **GitHub**: github.com/boostorg/beast
- **Book**: "C++ Network Programming with Boost.Asio"

## Getting Help

1. Check error codes and messages
2. Enable handler tracking (`#define BOOST_ASIO_ENABLE_HANDLER_TRACKING`)
3. Read the Beast examples
4. Ask on Stack Overflow with `[boost-asio]` tag
5. Join Boost Slack/Discord communities
