#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <memory>
#include <tuple>
#include <utility>

#include "utils.h"

/**
 * @brief Processes an HTTP request and generates the appropriate response
 * @tparam Body The type of the request body (usually string_body for simple
 * requests)
 * @tparam Allocator Memory allocator for the request fields
 * @param doc_root Root directory where files are served from
 * @param req The HTTP request to process
 * @return An HTTP response message (type-erased as message_generator)
 *
 * @details
 * This function is the "kitchen" of the web server. It takes an order (HTTP
 * request) and prepares the meal (HTTP response). It handles:
 * - GET requests: Returns file contents
 * - HEAD requests: Returns file metadata without contents
 * - Error responses: 400 (bad request), 404 (not found), 500 (server error)
 *
 * @par Request Processing Flow:
 * 1. Validate HTTP method (only GET and HEAD allowed)
 * 2. Validate request path (must start with /, no ".." for security)
 * 3. Convert URL path to filesystem path
 * 4. If path ends with /, append "index.html"
 * 5. Open the file from doc_root
 * 6. Return appropriate response based on file status
 *
 * @par Security Features:
 * - Rejects paths containing ".." (prevents directory traversal attacks)
 * - Rejects paths not starting with "/"
 * - Only serves files under doc_root (chroot-like behavior)
 *
 * @note This is a template function, so it's defined in the header file
 */
template <class Body, class Allocator>
boost::beast::http::message_generator handle_request(
    boost::beast::string_view doc_root,
    boost::beast::http::request<
        Body, boost::beast::http::basic_fields<Allocator>>&& req)
{
    // Returns a bad request response
    auto const bad_request = [&req](boost::beast::string_view why) {
        boost::beast::http::response<boost::beast::http::string_body> res{
            boost::beast::http::status::bad_request, req.version()};
        res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(boost::beast::http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    // Returns a not found response
    auto const not_found = [&req](boost::beast::string_view target) {
        boost::beast::http::response<boost::beast::http::string_body> res{
            boost::beast::http::status::not_found, req.version()};
        res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(boost::beast::http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() =
            "The resource '" + std::string(target) + "' was not found.";
        res.prepare_payload();
        return res;
    };

    // Returns a server error response
    auto const server_error = [&req](boost::beast::string_view what) {
        boost::beast::http::response<boost::beast::http::string_body> res{
            boost::beast::http::status::internal_server_error, req.version()};
        res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(boost::beast::http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + std::string(what) + "'";
        res.prepare_payload();
        return res;
    };

    // Make sure we can handle the method
    if (req.method() != boost::beast::http::verb::get &&
        req.method() != boost::beast::http::verb::head)
        return bad_request("Unknown HTTP-method");

    // Request path must be absolute and not contain "..".
    if (req.target().empty() || req.target()[0] != '/' ||
        req.target().find("..") != boost::beast::string_view::npos)
        return bad_request("Illegal request-target");

    // Build the path to the requested file
    std::string path = path_cat(doc_root, req.target());
    if (req.target().back() == '/') path.append("index.html");

    // Attempt to open the file
    boost::beast::error_code ec;
    boost::beast::http::file_body::value_type body;
    body.open(path.c_str(), boost::beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if (ec == boost::beast::errc::no_such_file_or_directory)
        return not_found(req.target());

    // Handle an unknown error
    if (ec) return server_error(ec.message());

    // Cache the size since we need it after the move
    auto const size = body.size();

    // Respond to HEAD request
    if (req.method() == boost::beast::http::verb::head) {
        boost::beast::http::response<boost::beast::http::empty_body> res{
            boost::beast::http::status::ok, req.version()};
        res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(boost::beast::http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return res;
    }

    // Respond to GET request
    boost::beast::http::response<boost::beast::http::file_body> res{
        std::piecewise_construct, std::make_tuple(std::move(body)),
        std::make_tuple(boost::beast::http::status::ok, req.version())};
    res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(boost::beast::http::field::content_type, mime_type(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return res;
}

/**
 * @class session
 * @brief The "waiter" of your HTTP server - handles one client connection
 *        from start to finish
 *
 * @details
 * Think of a session as a restaurant waiter assigned to one table (client).
 * The waiter:
 * 1. Greets the customer (accepts connection)
 * 2. Takes their order (reads HTTP request)
 * 3. Brings the food (sends HTTP response)
 * 4. Asks if they want anything else (handles keep-alive)
 * 5. Says goodbye when they leave (closes connection)
 *
 * Each session handles exactly one TCP connection, but can process multiple
 * HTTP requests on that connection if the client uses HTTP keep-alive.
 *
 * @par Key Concepts:
 * - **Asynchronous**: Never blocks - uses callbacks for all I/O
 * - **Stateful**: Maintains connection state (buffer, request, etc.)
 * - **Self-managing**: Keeps itself alive with shared_ptr until connection
 * closes
 * - **Thread-safe**: Each session runs in its own strand (serialization
 * context)
 *
 * @par Lifecycle:
 * 1. **Construction**: Takes ownership of a connected socket
 * 2. **run()**: Starts the read cycle
 * 3. **do_read()**: Waits for client to send data
 * 4. **on_read()**: Processes the received HTTP request
 * 5. **handle_request()**: Generates response (from template function)
 * 6. **send_response()**: Sends response back to client
 * 7. **on_write()**: Checks if connection should continue or close
 * 8. **Loop**: Go back to step 3 if keep-alive, else...
 * 9. **do_close()**: Gracefully shuts down the TCP connection
 *
 * @par HTTP Keep-Alive:
 * HTTP/1.1 allows multiple requests on one connection. After sending a
 * response, the session checks the Connection header. If "keep-alive", it reads
 * the next request. If "close" or HTTP/1.0, it closes the connection.
 *
 * @par Error Handling:
 * - Network errors: Logged to stderr, connection closed
 * - Parse errors: Returns 400 Bad Request
 * - File not found: Returns 404 Not Found
 * - Server errors: Returns 500 Internal Server Error
 *
 * @par Thread Safety:
 * - Each session has its own strand (executor)
 * - All operations on a session are serialized through its strand
 * - Multiple sessions can run concurrently on different threads
 * - Safe to call from any thread - operations are automatically dispatched
 *
 * @par Memory Management:
 * - Sessions are created as shared_ptr by the listener
 * - Each session keeps itself alive with shared_from_this()
 * - Automatically destroyed when connection closes and all async ops complete
 *
 * @par Usage Example (Internal - you don't create sessions directly):
 * @code
 * // This is done by the listener - you don't write this code:
 * auto new_session = std::make_shared<session>(std::move(socket), doc_root);
 * new_session->run();  // Starts handling the connection
 * // Session manages its own lifetime now
 * @endcode
 *
 * @note
 * - Sessions timeout after 30 seconds of inactivity (configurable)
 * - Supports HTTP/1.1 and HTTP/1.0
 * - Supports GET and HEAD methods only
 * - File serving is limited to doc_root directory (security)
 *
 * @see listener
 * @see handle_request()
 */
class session : public std::enable_shared_from_this<session> {
  public:
    session(boost::asio::ip::tcp::socket&& socket,
            std::shared_ptr<std::string const> const& doc_root);

    void run();
    void do_read();
    void on_read(boost::beast::error_code ec, std::size_t bytes_transferred);
    void send_response(boost::beast::http::message_generator&& msg);
    void do_close();
    void on_write(bool keep_alive, boost::beast::error_code ec,
                  std::size_t bytes_transferred);

  private:
    /// The TCP connection to the client - like the "phone line" to the customer
    /// This is a stream that supports both reading and writing
    boost::beast::tcp_stream stream_;

    /// Buffer for reading data from the network - like a "notepad" for taking
    /// orders Dynamically grows as needed to hold incoming data
    boost::beast::flat_buffer buffer_;

    /// Root directory for serving files - shared with all sessions
    /// Shared_ptr ensures the directory path stays valid
    std::shared_ptr<std::string const> doc_root_;

    /// The current HTTP request being processed - like the "current order"
    /// Reused for each request in a keep-alive connection
    boost::beast::http::request<boost::beast::http::string_body> req_;
};
