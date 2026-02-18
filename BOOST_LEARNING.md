# Boost Learning Guide: From This Project

> ℹ️ **Note:** The code examples in this guide are based on the [Boost.Beast HTTP server example](https://www.boost.org/doc/libs/release/libs/beast/example/http/server/async/http_server_async.cpp). The explanations are my own, but the underlying code and concepts come from the excellent Boost.Beast library by Vinnie Falco and the Boost team.

This guide breaks down Boost concepts used in the HTTP server, ordered by difficulty.

## Level 1: Basic Concepts

### What is `io_context`?

Think of `io_context` as a "to-do list" for your program:

```cpp
boost::asio::io_context ioc;

// Instead of:
// "Read from socket (wait until data arrives)"

// You say:
// "When data arrives, call this function"
// (Add task to to-do list)

ioc.run();  // Start processing the to-do list
```

**Key insight**: Your program doesn't wait. It sets up callbacks and continues.

### Synchronous vs Asynchronous

**Synchronous (blocking)**:
```cpp
// Program stops here until data arrives
size_t bytes = socket.read_some(buffer);
std::cout << "Got " << bytes << " bytes\n";
// Program continues
```

**Asynchronous (non-blocking)**:
```cpp
// Program continues immediately
socket.async_read_some(buffer, [](error_code ec, size_t bytes) {
    // This is called LATER, when data arrives
    std::cout << "Got " << bytes << " bytes\n";
});
// Program is already here, doing other work
```

### The Callback Pattern

Every async operation needs a callback:

```cpp
// Pattern: async_operation(args..., callback);
acceptor.async_accept(
    strand,           // Where to run the callback
    [](error_code ec, tcp::socket socket) {  // The callback
        if (!ec) {
            // Handle new connection
        }
    }
);
```

## Level 2: Object Lifetime

### The Problem

```cpp
void handle_connection() {
    session s(socket);  // Created on stack
    s.do_read();        // Starts async read
}  // s is destroyed here!
   // But async read is still pending!
   // CRASH when data arrives!
```

### The Solution: shared_ptr

```cpp
void handle_connection() {
    auto s = std::make_shared<session>(socket);  // Heap allocated
    s->do_read();  // Keeps s alive via shared_from_this()
}  // s stays alive because async op holds reference
```

### Understanding `enable_shared_from_this`

```cpp
class session : public std::enable_shared_from_this<session> {
    void do_read() {
        // WRONG: This creates a NEW shared_ptr
        // auto self = std::shared_ptr<session>(this);
        
        // CORRECT: Get shared_ptr to existing object
        auto self = shared_from_this();
        
        async_read(socket, buffer, [self](...) {
            // Callback holds reference to self
            // Object stays alive!
        });
    }
};
```

**Rule**: Never create a `shared_ptr` from `this` directly. Always use `shared_from_this()`.

## Level 3: Threading

### Single-Threaded io_context

```cpp
io_context ioc;

// All callbacks run on this thread
ioc.run();  // Blocks until all work done
```

**Pros**: No race conditions, simple
**Cons**: Can't use multiple CPU cores

### Multi-Threaded io_context

```cpp
io_context ioc;

// Run io_context on 4 threads
std::vector<std::thread> threads;
for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&ioc] { ioc.run(); });
}

for (auto& t : threads) t.join();
```

**Problem**: Multiple threads might access the same object!

### The Strand Solution

A strand is like a "personal assistant" that ensures thread safety:

```cpp
// Without strand (DANGEROUS):
// Thread 1: session.read()
// Thread 2: session.write()  // Race condition!

// With strand (SAFE):
Strand: session.read() → session.write()  // Serialized
```

**Code**:
```cpp
// Create strand
auto strand = boost::asio::make_strand(ioc);

// Everything posted to strand runs sequentially
boost::asio::post(strand, [] {
    // This runs exclusively
});

boost::asio::post(strand, [] {
    // This waits for above to finish
});
```

## Level 4: HTTP with Beast

### HTTP Message Structure

```cpp
// Request
http::request<http::string_body> req;
req.method(http::verb::get);           // GET
req.target("/index.html");             // URL
req.set(http::field::host, "localhost");
req.set(http::field::user_agent, "MyApp/1.0");

// Body (for POST requests)
req.body() = "name=value&foo=bar";
req.prepare_payload();  // Calculate Content-Length
```

### Reading HTTP

```cpp
// 1. Create buffer and request
beast::flat_buffer buffer;
http::request<http::string_body> req;

// 2. Read async
http::async_read(socket, buffer, req,
    [](error_code ec, size_t bytes) {
        // req now contains the parsed HTTP request
        std::cout << "Method: " << req.method() << "\n";
        std::cout << "Target: " << req.target() << "\n";
    }
);
```

### Writing HTTP

```cpp
// 1. Create response
http::response<http::string_body> res;
res.result(http::status::ok);           // 200 OK
res.set(http::field::content_type, "text/html");
res.body() = "<h1>Hello World</h1>";
res.prepare_payload();

// 2. Send async
http::async_write(socket, res,
    [](error_code ec, size_t bytes) {
        std::cout << "Sent " << bytes << " bytes\n";
    }
);
```

### File Serving

```cpp
// Beast can send files efficiently (zero-copy on some platforms)
http::response<http::file_body> res;
res.result(http::status::ok);

// Open file
http::file_body::value_type body;
body.open("/path/to/file", beast::file_mode::scan, ec);

// Move file into response
res.body() = std::move(body);
res.prepare_payload();
```

## Level 5: Advanced Patterns

### Composition: Chaining Operations

```cpp
void session::do_work() {
    do_read();  // Step 1
}

void session::on_read(ec, bytes) {
    process_data();  // Step 2
    do_write();      // Step 3
}

void session::on_write(ec, bytes) {
    do_read();  // Step 4 - loop back
}
```

**Visual**: `do_read → on_read → do_write → on_write → do_read → ...`

### Error Handling

Always check error codes:

```cpp
void on_read(error_code ec, size_t bytes) {
    if (ec == http::error::end_of_stream) {
        // Client closed connection normally
        do_close();
        return;
    }
    
    if (ec) {
        // Actual error
        std::cerr << "Error: " << ec.message() << "\n";
        do_close();
        return;
    }
    
    // Success - process data
    handle_request();
}
```

### Timeouts

```cpp
// Set deadline - close connection if no activity for 30 seconds
stream_.expires_after(std::chrono::seconds(30));

// Alternative: Set absolute time
stream_.expires_at(std::chrono::steady_clock::now() + std::chrono::seconds(30));
```

### Buffer Management

```cpp
// flat_buffer: Resizable buffer, good for unknown sizes
beast::flat_buffer buffer;

// static_buffer: Fixed size, stack-allocated
beast::static_buffer<1024> buffer;  // 1KB max

// multi_buffer: Multiple buffers, efficient for scattered I/O
beast::multi_buffer buffer;
```

## Common Mistakes

### 1. Capturing this in Lambdas

```cpp
// WRONG: session might be destroyed before callback
async_read(socket, buffer, [this](...) {
    // CRASH if session destroyed!
});

// CORRECT: Keep session alive
async_read(socket, buffer, 
    [self = shared_from_this()](...) {
        // Safe - self keeps session alive
    }
);
```

### 2. Not Checking error_code

```cpp
// WRONG
async_read(socket, buffer, [](error_code ec, size_t n) {
    // Always check ec!
    process(buffer);  // Buffer might be empty!
});

// CORRECT
async_read(socket, buffer, [](error_code ec, size_t n) {
    if (ec) {
        handle_error(ec);
        return;
    }
    process(buffer);
});
```

### 3. Blocking in Callbacks

```cpp
// WRONG - blocks the io_context thread
async_read(socket, buffer, [](...) {
    std::this_thread::sleep_for(10s);  // DON'T DO THIS!
    // Other connections can't be handled!
});

// CORRECT - use another thread pool
async_read(socket, buffer, [](...) {
    // Quick response
    post(thread_pool_executor, [] {
        // Heavy work here
    });
});
```

### 4. Forgetting to Call io_context::run()

```cpp
// WRONG - nothing happens!
io_context ioc;
listener server(ioc, ...);
server.run();
// Program exits immediately

// CORRECT
io_context ioc;
listener server(ioc, ...);
server.run();
ioc.run();  // This blocks and processes events
```

## Exercises

Try these to deepen your understanding:

### Exercise 1: Timer

Create a timer that prints "Hello" every second:

```cpp
void print(boost::system::error_code ec, 
           boost::asio::steady_timer& timer, 
           int& count) {
    if (count < 5) {
        std::cout << "Hello " << count << "\n";
        ++count;
        
        timer.expires_after(1s);
        timer.async_wait([&](error_code ec) {
            print(ec, timer, count);
        });
    }
}

int main() {
    io_context ioc;
    steady_timer timer(ioc);
    int count = 0;
    
    timer.expires_after(1s);
    timer.async_wait([&](error_code ec) {
        print(ec, timer, count);
    });
    
    ioc.run();
}
```

### Exercise 2: Simple Echo Server

Create a server that echoes back whatever the client sends:

```cpp
class echo_session : public std::enable_shared_from_this<echo_session> {
    tcp::socket socket_;
    std::array<char, 1024> buffer_;
    
public:
    echo_session(tcp::socket socket) : socket_(std::move(socket)) {}
    
    void start() {
        do_read();
    }
    
    void do_read() {
        socket_.async_read_some(
            boost::asio::buffer(buffer_),
            [self = shared_from_this()](error_code ec, size_t n) {
                if (!ec) {
                    self->do_write(n);
                }
            }
        );
    }
    
    void do_write(size_t n) {
        async_write(socket_, boost::asio::buffer(buffer_, n),
            [self = shared_from_this()](error_code ec, size_t) {
                if (!ec) {
                    self->do_read();  // Echo loop
                }
            }
        );
    }
};
```

### Exercise 3: HTTP Client

Create a simple HTTP client using Beast:

```cpp
int main() {
    io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    
    // Resolve hostname
    auto results = resolver.resolve("example.com", "80");
    
    // Connect
    stream.connect(results);
    
    // Send request
    http::request<http::string_body> req{http::verb::get, "/", 11};
    req.set(http::field::host, "example.com");
    http::write(stream, req);
    
    // Read response
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);
    
    std::cout << res.body() << "\n";
}
```

## Next Steps

After mastering these concepts:

1. **SSL/TLS** - Learn `boost::asio::ssl` for HTTPS
2. **WebSockets** - Real-time communication
3. **C++20 Coroutines** - Cleaner async code with `co_await`
4. **Boost.Asio Networking TS** - Standard C++ networking (coming in C++23/26)

## Resources

- **Books**:
  - "C++ Network Programming with Boost.Asio" by Wisnu Anggoro
  - "Hands-On Network Programming with C++" by Lewis Van Winkle

- **Websites**:
  - [Think Async](https://think-async.com/) - Asio author's blog
  - [Boost.Asio Examples](https://www.boost.org/doc/libs/release/doc/html/boost_asio/examples.html)

- **Videos**:
  - "Asynchronous IO with Boost.Asio" - CppCon talks
  - "Understanding Boost.Beast" - YouTube tutorials

---

**Remember**: Async programming is a paradigm shift. Don't worry if it feels weird at first - practice with small examples before diving into complex code.
