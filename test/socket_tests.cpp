//
// Created by Jesson on 2024/10/18.
//

#include <coro/io/io_service.h>
#include <coro/net/socket.h>
#include <coro/types/task.h>
#include <coro/when_all.h>
#include <coro/detail/sync_wait.h>
#include <coro/on_scope_exit.h>
#include <coro/cancellation/cancellation_source.h>
#include <coro/cancellation/cancellation_token.h>
#include <coro/async_scope.h>

#include "doctest/doctest.h"

using namespace coro;
using namespace coro::net;

TEST_SUITE_BEGIN("socket");

TEST_CASE("create TCP/IPv4") {
	io_service io_svc;
	auto socket = socket::create_tcpv4(io_svc);
}

TEST_CASE("create TCP/IPv6") {
	io_service io_svc;
	auto socket = socket::create_tcpv6(io_svc);
}

TEST_CASE("create UDP/IPv4") {
	io_service io_svc;
	auto socket = socket::create_udpv4(io_svc);
}

TEST_CASE("create UDP/IPv6") {
	io_service io_svc;
	auto socket = socket::create_udpv6(io_svc);
}

TEST_CASE("TCP/IPv4 connect/disconnect") {
	io_service io_svc;

    auto server_socket = socket::create_tcpv4(io_svc);
    server_socket.bind(ipv4_endpoint{ ipv4_address::loopback(), 0 });
    server_socket.listen(3);

	auto server = [&](net::socket listening_socket) -> task<int> {
		auto s = socket::create_tcpv4(io_svc);
		co_await listening_socket.accept(s);
		co_await s.disconnect();
		co_return 0;
	};

	auto client = [&]() -> task<int> {
		auto s = socket::create_tcpv4(io_svc);
		s.bind(ipv4_endpoint{ ipv4_address::loopback(), 0 });
		co_await s.connect(server_socket.local_endpoint());
		co_await s.disconnect();
		co_return 0;
	};

    task<int> server_task = server(std::move(server_socket));
	task<int> client_task = client();

	(void)sync_wait(when_all(
		[&]() -> task<int> {
			auto stop_on_exit = on_scope_exit([&] { io_svc.stop(); });
			(void)co_await when_all(std::move(server_task), std::move(client_task));
			co_return 0;
		}(),
		[&]() -> task<int> {
			io_svc.process_events();
			co_return 0;
		}()));
}

TEST_CASE("send/recv TCP/IPv4") {
    io_service io_svc;

    auto server_socket = socket::create_tcpv4(io_svc);
    server_socket.bind(ipv4_endpoint{ ipv4_address::loopback(), 0 });
    server_socket.listen(3);

    auto server = [&](net::socket listening_socket) -> task<int> {
        auto s = socket::create_tcpv4(io_svc);
        co_await listening_socket.accept(s);

        std::uint8_t buffer[64];
        std::size_t bytes_received;
        do {
            bytes_received = co_await s.recv(buffer, sizeof(buffer));
            if (bytes_received > 0) {
                std::size_t bytes_sent = 0;
                do {
                    bytes_sent += co_await s.send(buffer + bytes_sent, bytes_received - bytes_sent);
                } while (bytes_sent < bytes_received);
            }
        } while (bytes_received > 0);
        s.close_send();

        co_await s.disconnect();
        co_return 0;
    };

    auto client = [&]() -> task<int> {
        auto s = socket::create_tcpv4(io_svc);
        s.bind(ipv4_endpoint{ ipv4_address::loopback(), 0 });
        co_await s.connect(server_socket.local_endpoint());

        auto receive = [&]() -> task<int> {
            std::uint8_t buffer[100];
            std::uint64_t total_bytes_received = 0;
            std::size_t bytes_received;
            do {
                bytes_received = co_await s.recv(buffer, sizeof(buffer));
                for (std::size_t i = 0; i < bytes_received; ++i) {
                    std::uint64_t byte_index = total_bytes_received + i;
                    std::uint8_t expected_byte = 'a' + (byte_index % 26);
                    CHECK(buffer[i] == expected_byte);
                }
                total_bytes_received += bytes_received;
            } while (bytes_received > 0);
            CHECK(total_bytes_received == 1000);
            co_return 0;
        };

        auto send = [&]() -> task<int> {
            std::uint8_t buffer[100];
            for (std::uint64_t i = 0; i < 1000; i += sizeof(buffer)) {
                for (std::size_t j = 0; j < sizeof(buffer); ++j) {
                    buffer[j] = 'a' + ((i + j) % 26);
                }
                std::size_t bytes_sent = 0;
                do {
                    bytes_sent += co_await s.send(buffer + bytes_sent, sizeof(buffer) - bytes_sent);
                } while (bytes_sent < sizeof(buffer));
            }
            s.close_send();
            co_return 0;
        };
        co_await when_all(send(), receive());

        co_await s.disconnect();
        co_return 0;
    };

    task<int> server_task = server(std::move(server_socket));
    task<int> client_task = client();

    (void)sync_wait(when_all(
        [&]() -> task<int> {
            auto stop_on_exit = on_scope_exit([&] { io_svc.stop(); });
            (void)co_await when_all(std::move(server_task), std::move(client_task));
            co_return 0;
        }(),
        [&]() -> task<int> {
            io_svc.process_events();
            co_return 0;
        }()));
}

TEST_CASE("send/recv TCP/IPv4 many connections") {
    io_service io_svc{};
	auto listening_socket = socket::create_tcpv4(io_svc);
	listening_socket.bind(ipv4_endpoint{ ipv4_address::loopback(), 0 });
	listening_socket.listen(20);

	cancellation_source canceller;

	auto handle_connection = [](net::socket s) -> task<void> {
		std::uint8_t buffer[64];
		std::size_t bytes_received;
		do {
			bytes_received = co_await s.recv(buffer, sizeof(buffer));
			if (bytes_received > 0) {
				std::size_t bytes_sent = 0;
				do {
					bytes_sent += co_await s.send(buffer + bytes_sent, bytes_received - bytes_sent);
				} while (bytes_sent < bytes_received);
			}
		} while (bytes_received > 0);
		s.close_send();
		co_await s.disconnect();
	};

	auto echo_server = [&](cancellation_token ct) -> task<> {
		async_scope connection_scope;

		std::exception_ptr ex;
		try {
			while (true) {
				auto accepting_socket = socket::create_tcpv4(io_svc);
				co_await listening_socket.accept(accepting_socket, ct);
				connection_scope.spawn(handle_connection(std::move(accepting_socket)));
			}
		}
		catch (const operation_cancelled&) {}
		catch (...) {
			ex = std::current_exception();
		}

		co_await connection_scope.join();

		if (ex) {
			std::rethrow_exception(ex);
		}
	};

	auto echo_client = [&]() -> task<> {
		auto connecting_socket = socket::create_tcpv4(io_svc);
		connecting_socket.bind(ipv4_endpoint{});
		co_await connecting_socket.connect(listening_socket.local_endpoint());

		auto receive = [&]() -> task<> {
			std::uint8_t buffer[100];
			std::uint64_t total_bytes_received = 0;
			std::size_t bytes_received;
			do {
				bytes_received = co_await connecting_socket.recv(buffer, sizeof(buffer));
				for (std::size_t i = 0; i < bytes_received; ++i) {
					std::uint64_t byte_index = total_bytes_received + i;
					std::uint8_t expected_byte = 'a' + (byte_index % 26);
					CHECK(buffer[i] == expected_byte);
				}
				total_bytes_received += bytes_received;
			} while (bytes_received > 0);
			CHECK(total_bytes_received == 1000);
		};

		auto send = [&]() -> task<> {
			std::uint8_t buffer[100];
			for (std::uint64_t i = 0; i < 1000; i += sizeof(buffer)) {
				for (std::size_t j = 0; j < sizeof(buffer); ++j) {
					buffer[j] = 'a' + ((i + j) % 26);
				}
				std::size_t bytes_sent = 0;
				do {
					bytes_sent += co_await connecting_socket.send(buffer + bytes_sent, sizeof(buffer) - bytes_sent);
				} while (bytes_sent < sizeof(buffer));
			}
			connecting_socket.close_send();
		};

		co_await when_all(send(), receive());
		co_await connecting_socket.disconnect();
	};

	auto many_echo_clients = [&](int count) -> task<void> {
		auto shutdown_server_on_exit = on_scope_exit([&] {
			canceller.request_cancellation();
		});

		std::vector<task<>> client_tasks;
		client_tasks.reserve(count);
		for (int i = 0; i < count; ++i) {
			client_tasks.emplace_back(echo_client());
		}

		co_await when_all(std::move(client_tasks));
	};

	(void)sync_wait(when_all(
		[&]() -> task<> {
			auto stop_on_exit = on_scope_exit([&] { io_svc.stop(); });
			(void)co_await when_all(many_echo_clients(20), echo_server(canceller.token()));
		}(),
		[&]() -> task<>
		{
			io_svc.process_events();
			co_return;
		}()));
}

TEST_CASE("udp send_to/recv_from") {
    io_service io_svc{};

	auto server = [&](net::socket server_socket) -> task<int> {
		std::uint8_t buffer[100];

		auto [bytes_received, remote_end_point] = co_await server_socket.recv_from(buffer, 100);
		CHECK(bytes_received == 50);

		// Send an ACK response.
		{
			constexpr std::uint8_t response[1] = { 0 };
			co_await server_socket.send_to(remote_end_point, &response, 1);
		}

		// Second message received won't fit within buffer.
		try {
			std::tie(bytes_received, remote_end_point) = co_await server_socket.recv_from(buffer, 100);
			FAIL("Should have thrown");
		}
		catch (const std::system_error& e) {
		    CHECK(e.code().value() == EMSGSIZE);
		    CHECK(e.code().category() == std::generic_category());
		}

		// Send an NACK response.
		{
			std::uint8_t response[1] = { 1 };
			co_await server_socket.send_to(remote_end_point, response, 1);
		}

		co_return 0;
	};

	ip_endpoint server_address;
	task<int> server_task;
	{
		auto server_socket = socket::create_udpv4(io_svc);
		server_socket.bind(ipv4_endpoint{ ipv4_address::loopback(), 0 });
		server_address = server_socket.local_endpoint();
		server_task = server(std::move(server_socket));
	}

	auto client = [&]() -> task<int> {
		auto socket = socket::create_udpv4(io_svc);

		// don't need to bind(), should be implicitly bound on first send_to().

		// Send first message of 50 bytes
		{
			std::uint8_t buffer[50] = { 0 };
			co_await socket.send_to(server_address, buffer, 50);
		}

		// Receive ACK message
		{
			std::uint8_t buffer[1];
			auto [bytes_received, ack_address] = co_await socket.recv_from(buffer, 1);
			CHECK(bytes_received == 1);
			CHECK(buffer[0] == 0);
			CHECK(ack_address == server_address);
		}

		// Send second message of 128 bytes
		{
			std::uint8_t buffer[128] = { 0 };
			co_await socket.send_to(server_address, buffer, 128);
		}

		// Receive NACK message
		{
			std::uint8_t buffer[1];
			auto [bytes_received, ack_address] = co_await socket.recv_from(buffer, 1);
			CHECK(bytes_received == 1);
			CHECK(buffer[0] == 1);
			CHECK(ack_address == server_address);
		}

		co_return 0;
	};

	(void)sync_wait(when_all(
		[&]() -> task<int> {
			auto stop_on_exit = on_scope_exit([&] { io_svc.stop(); });
			(void)co_await when_all(std::move(server_task), client());
			co_return 0;
		}(),
		[&]() -> task<int> {
			io_svc.process_events();
			co_return 0;
		}()));
}

TEST_SUITE_END();
