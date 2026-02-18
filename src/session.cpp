#include "session.h"

#include "utils.h"

/**
 * @brief Construct a new session
 * @param socket The connected TCP socket (moved into the session)
 * @param doc_root Shared pointer to the document root directory
 *
 * @details
 * Construction is quick and doesn't perform any I/O. The session is ready
 * to start handling the connection, but doesn't begin until run() is
 * called.
 *
 * @par What happens:
 * 1. Takes ownership of the socket (no copying, efficient move)
 * 2. Stores reference to doc_root (shared ownership)
 * 3. Initializes empty request object
 *
 * @warning The socket must already be connected to a client
 */
session::session(boost::asio::ip::tcp::socket&& socket,
                 std::shared_ptr<std::string const> const& doc_root)
    : stream_(std::move(socket)), doc_root_(doc_root)
{}

/**
 * @brief Start processing the connection
 *
 * @details
 * This is the entry point. It kicks off the async read operation and
 * returns immediately. The session will continue running in the background,
 * processing requests until the connection closes.
 *
 * @par Async Chain:
 * run() -> do_read() -> [wait] -> on_read() -> handle_request() ->
 * send_response() -> [wait] -> on_write() -> do_read() [or] do_close()
 *
 * @note
 * - Safe to call multiple times (idempotent, but only first call matters)
 * - Returns immediately - doesn't wait for completion
 * - Session keeps itself alive with shared_from_this()
 */
void session::run()
{
    boost::asio::dispatch(stream_.get_executor(),
                          boost::beast::bind_front_handler(&session::do_read,
                                                           shared_from_this()));
}

/**
 * @brief Initiates an asynchronous read operation
 *
 * @details
 * Tells the OS: "When data arrives from this client, wake me up."
 * This is non-blocking - it returns immediately and the program can do
 * other work while waiting.
 *
 * @par Settings:
 * - Clears any previous request data
 * - Sets a 30-second timeout (connection closed if no data arrives)
 * - Uses the session's strand for thread safety
 */
void session::do_read()
{
    req_ = {};
    stream_.expires_after(std::chrono::seconds(30));
    boost::beast::http::async_read(stream_, buffer_, req_,
                                   boost::beast::bind_front_handler(
                                       &session::on_read, shared_from_this()));
}

/**
 * @brief Callback invoked when data is received from the client
 * @param ec Error code - empty on success, contains error on failure
 * @param bytes_transferred Number of bytes read from the network
 *
 * @details
 * This is called when the client sends data (or disconnects). It:
 * 1. Checks for errors (disconnection, timeout, parse error)
 * 2. Calls handle_request() to process the HTTP request
 * 3. Initiates sending the response
 *
 * @par Error Handling:
 * - end_of_stream: Client closed connection gracefully - close our end
 * - Other errors: Log and close connection
 * - Success: Process the request
 */
void session::on_read(boost::beast::error_code ec,
                      std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if (ec == boost::beast::http::error::end_of_stream) return do_close();
    if (ec) return fail(ec, "read");

    send_response(handle_request(*doc_root_, std::move(req_)));
}

/**
 * @brief Sends an HTTP response to the client
 * @param msg The response message to send (moved, not copied)
 *
 * @details
 * This queues the response for transmission. Network I/O is async, so
 * this returns immediately and the actual sending happens in the
 * background.
 *
 * @par What happens:
 * 1. Checks if this is the final response (Connection: close)
 * 2. Starts async write operation
 * 3. on_write() will be called when complete
 */
void session::send_response(boost::beast::http::message_generator&& msg)
{
    bool keep_alive = msg.keep_alive();
    boost::beast::async_write(
        stream_, std::move(msg),
        boost::beast::bind_front_handler(&session::on_write, shared_from_this(),
                                         keep_alive));
}

/**
 * @brief Callback invoked when response has been sent
 * @param keep_alive Whether to keep the connection open for more requests
 * @param ec Error code - empty on success
 * @param bytes_transferred Number of bytes written to the network
 *
 * @details
 * Called after the response is fully transmitted. Decides what to do next:
 * - Success + keep-alive: Read the next request (HTTP persistent
 * connection)
 * - Success + close: Close the connection gracefully
 * - Error: Log and close connection
 */
void session::on_write(bool keep_alive, boost::beast::error_code ec,
                       std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if (ec) return fail(ec, "write");
    if (!keep_alive) return do_close();
    do_read();
}

/**
 * @brief Gracefully closes the TCP connection
 *
 * @details
 * Performs a "TCP half-close" - we stop sending but allow client to finish.
 * This is the polite way to end a conversation in TCP.
 *
 * @note After this, the session object will be destroyed automatically
 * when all shared_ptr references go out of scope.
 */
void session::do_close()
{
    boost::beast::error_code ec;
    stream_.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
}
