#include "listener.h"

/**
 * @brief Construct a new listener
 * @param ioc The io_context that manages all async operations
 * @param endpoint The IP address and port to listen on (e.g., 0.0.0.0:8080)
 * @param doc_root Path to the directory containing files to serve
 *
 * @par What happens during construction:
 * 1. Creates a TCP acceptor socket
 * 2. Opens the socket for the given protocol (IPv4 or IPv6)
 * 3. Sets SO_REUSEADDR option (allows quick restart after crash)
 * 4. Binds to the specified endpoint (IP:port)
 * 5. Starts listening for incoming connections
 *
 * @par Error Handling:
 * - If binding fails, the constructor prints an error and returns
 * - Check stderr for error messages ("open", "bind", "listen", etc.)
 *
 * @warning The doc_root pointer must remain valid for the lifetime of the
 * listener
 */
listener::listener(boost::asio::io_context& ioc,
                   boost::asio::ip::tcp::endpoint endpoint,
                   std::shared_ptr<std::string const> const& doc_root)
    : ioc_(ioc), acceptor_(boost::asio::make_strand(ioc)), doc_root_(doc_root)
{
    boost::beast::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        fail(ec, "open");
        return;
    }

    // Allow address reuse
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true), ec);
    if (ec) {
        fail(ec, "set_option");
        return;
    }

    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec) {
        fail(ec, "bind");
        return;
    }

    // Start listening for connections
    acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) {
        fail(ec, "listen");
        return;
    }
}

/**
 * @brief Start accepting incoming connections
 *
 * @details
 * This is the "main loop" of the listener. Once called, it:
 * 1. Kicks off an async accept operation
 * 2. Returns immediately (non-blocking)
 * 3. When a connection arrives, on_accept() is called
 * 4. on_accept() creates a session and starts accepting again
 *
 * @par When to call:
 * Call this AFTER constructing the listener but BEFORE running io_context.
 *
 * @code
 * auto server = std::make_shared<listener>(ioc, endpoint, doc_root);
 * server->run();  // Start accepting
 * ioc.run();      // Start event loop
 * @endcode
 */
void listener::run() { do_accept(); }

/**
 * @brief Initiates an asynchronous accept operation
 * @details Internal method called by run() and on_accept() to wait for the
 *          next connection. Uses a strand to ensure thread-safe accept.
 */
void listener::do_accept()
{
    // The new connection gets its own strand
    acceptor_.async_accept(boost::asio::make_strand(ioc_),
                           boost::beast::bind_front_handler(
                               &listener::on_accept, shared_from_this()));
}

/**
 * @brief Callback invoked when a new connection is accepted
 * @param ec Error code - empty if successful, contains error details if
 * failed
 * @param socket The newly connected TCP socket (represents the client
 * connection)
 *
 * @details
 * This is the "reception" callback. When someone knocks:
 * 1. If no error: Create a session object to handle this client
 * 2. If error: Log it and stop accepting (prevents infinite error loops)
 * 3. Immediately start accepting the next connection
 *
 * @par Session Creation:
 * The session is created as a shared_ptr and immediately started with
 * run(). The session keeps itself alive until the connection closes.
 */
void listener::on_accept(boost::beast::error_code ec,
                         boost::asio::ip::tcp::socket socket)
{
    if (ec) {
        fail(ec, "accept");
        return;  // To avoid infinite loop
    } else {
        // Create the session and run it
        std::make_shared<session>(std::move(socket), doc_root_)->run();
    }

    // Accept another connection
    do_accept();
}
