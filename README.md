# HTTP Server with Boost.Asio and Boost.Beast

> ⚠️ **IMPORTANT DISCLAIMER**
> 
> This repository is a **refactored version** of the official [Boost.Beast example: "HTTP server, asynchronous"](https://www.boost.org/doc/libs/release/libs/beast/example/http/server/async/http_server_async.cpp).
>
> **What I did:**
> - Took the original Boost.Beast example code (which is excellent!)
> - Restructured it into separate files with OOP-style class organization
> - Added extensive documentation to help beginners understand it
>
> **Why I did it:** Purely personal preference for code organization. I like having headers and implementation files separated.
>
> - This is not "better" than the original Boost example
> - This is not "more correct" than the original
> - This is not the "right way" to write Boost.Beast code
>
> The original example is perfectly fine and is the recommended starting point. This is just my personal take on organizing the same code. Use whichever style you prefer!
>
> **Credit:** All the actual code logic comes from the Boost.Beast library examples by Vinnie Falco and the Boost team. The Boost Software License applies.

---

A beginner-friendly example of building an asynchronous HTTP server using Boost libraries. This project demonstrates modern C++ networking with clear explanations for those new to Boost.

## What This Project Does

This is a simple file server that:
- Listens for HTTP connections on a specified port
- Serves files from a directory (like `/var/www` or a local folder)
- Handles multiple connections concurrently using async I/O
- Supports HTTP/1.1 keep-alive (multiple requests per connection)

**Example usage:**
```bash
./http-server-async 0.0.0.0 8080 ./public 4
# Server runs on port 8080, serves files from ./public, uses 4 threads
```

## Learning Goals

This project teaches you:
1. **Boost.Asio** - Asynchronous I/O and networking
2. **Boost.Beast** - HTTP protocol handling
3. **Modern C++ patterns** - shared_ptr, move semantics, lambdas
4. **Asynchronous programming** - callbacks, executors, strands

## Key Concepts Explained

### 1. Boost.Asio - The Event Loop

**io_context** is the heart of async operations:

```cpp
boost::asio::io_context ioc;
// io_context is like a task scheduler
// You tell it: "When X happens, call function Y"
// Then you run it: ioc.run() - this blocks and processes events
```

**Key idea**: Instead of waiting for operations to complete (blocking), you register callbacks and continue execution.

**Visual analogy**:
- Traditional (blocking): You call a restaurant, wait on hold until they answer
- Async (non-blocking): You call, leave your number, hang up and do other things. They call you back.

### 2. Boost.Beast - HTTP Made Easy

**Why Beast?** Raw HTTP is complex (headers, chunking, keep-alive, etc.). Beast handles it for you.

**Key types**:
- `http::request<Body>` - Incoming HTTP request
- `http::response<Body>` - Outgoing HTTP response
- `http::message_generator` - Type-erased response (can hold any response type)

### 3. The Big Picture - How It Works

```
┌─────────────────────────────────────────────────────────────┐
│                         MAIN THREAD                          │
│  ┌──────────────┐                                           │
│  │   listener   │◄──── Creates sessions for connections     │
│  └──────┬───────┘                                           │
│         │                                                    │
│         ▼                                                    │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐   │
│  │   session 1  │◄──►│   session 2  │◄──►│   session 3  │   │
│  │  (thread 1)  │    │  (thread 2)  │    │  (thread 3)  │   │
│  └──────────────┘    └──────────────┘    └──────────────┘   │
│                                                             │
│  All managed by boost::asio::io_context                      │
└─────────────────────────────────────────────────────────────┘
```

**Flow:**
1. `listener` waits for connections
2. When client connects, `listener` creates a `session`
3. `session` handles that one connection (read request → process → send response)
4. Multiple sessions run concurrently

### 4. Strands - Thread Safety Made Simple

**Problem**: Multiple threads accessing the same object = race conditions

**Solution**: Strands serialize operations

```cpp
// Without strand (unsafe):
Thread 1: session.read()  ──┐
Thread 2: session.write() ──┼──► Data corruption!
Thread 3: session.close() ──┘

// With strand (safe):
Strand:  read() → write() → close()  (serialized, one at a time)
```

**Code example**:
```cpp
acceptor_.async_accept(
    boost::asio::make_strand(ioc_),  // Each session gets its own strand
    callback
);
```

### 5. enable_shared_from_this - Self-Preservation

**Problem**: Async callbacks need to keep the object alive, but object might be destroyed while callback is pending

**Solution**: Use `shared_from_this()` in callbacks

```cpp
class session : public std::enable_shared_from_this<session> {
    void do_read() {
        http::async_read(stream_, buffer_, req_,
            boost::beast::bind_front_handler(
                &session::on_read, 
                shared_from_this()  // Keeps session alive until on_read is called
            ));
    }
};
```

**Analogy**: Like a restaurant reservation - you keep the table reserved until you arrive, even if you're running late.

## Code Walkthrough

### Main Function

```cpp
int main(int argc, char* argv[]) {
    // 1. Parse arguments
    auto address = boost::asio::ip::make_address(argv[1]);  // "0.0.0.0"
    auto port = static_cast<unsigned short>(std::atoi(argv[2]));  // 8080
    
    // 2. Create event loop
    boost::asio::io_context ioc{threads};  // {4} = 4 threads
    
    // 3. Create server
    auto server = std::make_shared<listener>(
        ioc,
        boost::asio::ip::tcp::endpoint{address, port},
        std::make_shared<std::string>(argv[3])  // doc root
    );
    
    // 4. Start accepting connections
    server->run();
    
    // 5. Run worker threads
    std::vector<std::thread> v;
    for (auto i = threads - 1; i > 0; --i)
        v.emplace_back([&ioc] { ioc.run(); });  // Each thread runs io_context
    
    // 6. Main thread also runs io_context (blocks here)
    ioc.run();
}
```

### Listener Class

```cpp
class listener : public std::enable_shared_from_this<listener> {
    void do_accept() {
        // "When someone connects, call on_accept()"
        acceptor_.async_accept(
            make_strand(ioc_),  // New connection gets own strand
            bind_front_handler(&listener::on_accept, shared_from_this())
        );
    }
    
    void on_accept(error_code ec, tcp::socket socket) {
        if (!ec) {
            // Create session for this connection
            std::make_shared<session>(std::move(socket), doc_root_)->run();
        }
        do_accept();  // Accept next connection
    }
};
```

### Session Class

```cpp
class session : public std::enable_shared_from_this<session> {
    void do_read() {
        // Reset request (clear previous data)
        req_ = {};
        
        // Set timeout - close connection if client is idle for 30 seconds
        stream_.expires_after(std::chrono::seconds(30));
        
        // Read HTTP request asynchronously
        http::async_read(stream_, buffer_, req_,
            bind_front_handler(&session::on_read, shared_from_this()));
    }
    
    void on_read(error_code ec, size_t bytes) {
        if (ec == http::error::end_of_stream) {
            // Client closed connection
            do_close();
            return;
        }
        
        // Process request and send response
        send_response(handle_request(*doc_root_, std::move(req_)));
    }
    
    void send_response(http::message_generator&& msg) {
        bool keep_alive = msg.keep_alive();
        
        http::async_write(stream_, std::move(msg),
            bind_front_handler(&session::on_write, shared_from_this(), keep_alive));
    }
    
    void on_write(bool keep_alive, error_code ec, size_t bytes) {
        if (!keep_alive) {
            do_close();  // Client said "Connection: close"
        } else {
            do_read();   // Keep connection open, read next request
        }
    }
};
```

## Building and Running

```bash
# Create build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build
make -j$(nproc)

# Run (serves files from current directory on port 8080)
./http-server-async 0.0.0.0 8080 . 1

# Test
curl http://localhost:8080/index.html
```

## Key Takeaways

1. **Async is not multi-threading** - Async = non-blocking I/O. You can have async on single thread.

2. **Strands are crucial** - They make thread-safety easy. Without them, you'd need manual locks.

3. **shared_ptr management** - Boost.Asio objects often outlive their creating scope due to async operations. Use `enable_shared_from_this`.

4. **RAII everywhere** - Boost uses RAII heavily. Objects clean up automatically (sockets close, memory freed).

5. **Error codes, not exceptions** - Boost.Asio uses `error_code` for async operations (exceptions don't work well across async boundaries).

## Further Learning

### Official Documentation
- [Boost.Asio Tutorial](https://www.boost.org/doc/libs/release/doc/html/boost_asio/tutorial.html)
- [Boost.Beast Documentation](https://www.boost.org/doc/libs/release/libs/beast/doc/html/index.html)

### Concepts to Learn Next
1. **Executors** - How tasks are scheduled (io_context is an executor)
2. **Completion tokens** - Different ways to receive async results (callbacks, futures, coroutines)
3. **SSL/TLS** - Using `boost::asio::ssl` for HTTPS
4. **WebSockets** - Real-time bidirectional communication with Beast
5. **C++20 Coroutines** - Modern way to write async code (`co_await`)

### Common Patterns
- **Acceptor pattern** - What we use here for handling connections
- **Proactor pattern** - Boost.Asio's underlying design
- **Half-sync/Half-async** - Queue work in one thread, process in another

## Troubleshooting

**"Address already in use"**
- Port is occupied by another process
- Solution: Use different port or kill the other process

**"No such file or directory"**
- The doc_root path doesn't exist
- Solution: Create the directory or use correct path

**Connection timeouts**
- Client is idle for more than 30 seconds
- This is expected behavior - see `expires_after()` in session.cpp

## Code Organization

```
.
├── CMakeLists.txt          # Build configuration
├── main.cpp                # Entry point, parses args
├── incl/
│   ├── listener.h          # Listens for connections (class declaration)
│   ├── session.h           # Handles one connection (class declaration)
│   └── utils.h             # Helper functions
└── src/
    ├── listener.cpp        # Listener implementation
    ├── session.cpp         # Session implementation
    └── utils.cpp           # Path handling, MIME types, error logging
```

## Contributing

This is a learning project. Feel free to:
- Add logging (Boost.Log)
- Add configuration file support
- Add SSL/TLS support
- Optimize file serving (add caching)
- Add support for POST requests

## License

Based on Boost.Beast examples. See Boost Software License.
