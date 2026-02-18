//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: HTTP server, asynchronous
//
//------------------------------------------------------------------------------

#include <boost/asio.hpp>
#include <memory>

#include "session.h"

/**
 * @class listener
 * @brief The "receptionist" of your HTTP server - waits for new connections
 *        and hands them off to session objects
 *
 * @details
 * Think of the listener as the receptionist at a busy restaurant. Its job is
 * to:
 * 1. Open the door (bind to a network port)
 * 2. Wait for customers (listen for incoming TCP connections)
 * 3. Greet each customer and assign them to a waiter (create a session)
 * 4. Go back to waiting for more customers (loop forever)
 *
 * The listener runs asynchronously, which means it doesn't block your program
 * while waiting. Instead, it uses callbacks (like setting a "call me when
 * someone arrives" notification).
 *
 * @par Usage Example:
 * @code
 * // Create an io_context - this is the "event loop" that manages all async
 * operations boost::asio::io_context ioc;
 *
 * // Create a listener on port 8080, serving files from "/var/www"
 * auto server = std::make_shared<listener>(
 *     ioc,
 *     boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("0.0.0.0"),
 * 8080), std::make_shared<std::string>("/var/www")
 * );
 *
 * // Start accepting connections
 * server->run();
 *
 * // Run the event loop (this blocks until server shuts down)
 * ioc.run();
 * @endcode
 *
 * @par Thread Safety:
 * - The listener itself is NOT thread-safe
 * - Must be created and used from a single thread OR protected by a strand
 * - However, each session it creates IS thread-safe (has its own strand)
 *
 * @par Lifecycle:
 * 1. Construct with io_context, endpoint, and document root
 * 2. Call run() to start accepting connections
 * 3. The listener keeps itself alive using shared_ptr (enable_shared_from_this)
 * 4. Automatically creates new session objects for each connection
 * 5. Runs until the io_context is stopped or an error occurs
 *
 * @note
 * - The listener binds to the port immediately upon construction
 * - If the port is already in use, construction will fail silently (check logs)
 * - Uses SO_REUSEADDR option to allow quick restart after crash
 *
 * @see session
 */
class listener : public std::enable_shared_from_this<listener> {
  public:
    listener(boost::asio::io_context& ioc,
             boost::asio::ip::tcp::endpoint endpoint,
             std::shared_ptr<std::string const> const& doc_root);

    void run();

  private:
    void do_accept();

    void on_accept(boost::beast::error_code ec,
                   boost::asio::ip::tcp::socket socket);

    /// Reference to the io_context - the "event coordinator" for async
    /// operations
    boost::asio::io_context& ioc_;

    /// The TCP socket that listens for incoming connections
    /// Think of this as the "doorbell" - it rings when someone connects
    boost::asio::ip::tcp::acceptor acceptor_;

    /// Root directory where files are served from (shared across all sessions)
    /// This is like the restaurant's menu - shared with all waiters
    std::shared_ptr<std::string const> doc_root_;
};
