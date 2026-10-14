// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal C++20 coroutine task. The interpreter is written as nested
// coroutines so that any opcode can suspend the whole script call chain
// until the next frame (needed for a browser main loop, and generally to
// keep rendering/input responsive without threads).

#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace gob {

template <typename T = void>
class Task;

namespace detail {

// When a task finishes, control transfers straight to whoever awaited it.
struct FinalAwaiter {
	bool await_ready() noexcept { return false; }
	template <typename P>
	std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) noexcept {
		auto cont = h.promise().continuation;
		return cont ? cont : std::noop_coroutine();
	}
	void await_resume() noexcept {}
};

struct PromiseBase {
	std::coroutine_handle<> continuation;
	std::exception_ptr error;

	std::suspend_always initial_suspend() noexcept { return {}; }
	FinalAwaiter final_suspend() noexcept { return {}; }
	void unhandled_exception() { error = std::current_exception(); }
};

} // namespace detail

template <typename T>
class Task {
public:
	struct promise_type : detail::PromiseBase {
		std::optional<T> value;
		Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
		void return_value(T v) { value = std::move(v); }
	};

	Task(Task &&o) noexcept : _h(std::exchange(o._h, {})) {}
	Task &operator=(Task &&o) noexcept { if (this != &o) { reset(); _h = std::exchange(o._h, {}); } return *this; }
	~Task() { reset(); }

	bool await_ready() const noexcept { return false; }
	std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
		_h.promise().continuation = awaiting;
		return _h;
	}
	T await_resume() {
		if (_h.promise().error)
			std::rethrow_exception(_h.promise().error);
		return std::move(*_h.promise().value);
	}

private:
	explicit Task(std::coroutine_handle<promise_type> h) : _h(h) {}
	void reset() { if (_h) _h.destroy(); _h = {}; }

	std::coroutine_handle<promise_type> _h;
};

template <>
class Task<void> {
public:
	struct promise_type : detail::PromiseBase {
		Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
		void return_void() {}
	};

	Task() = default;
	Task(Task &&o) noexcept : _h(std::exchange(o._h, {})) {}
	Task &operator=(Task &&o) noexcept { if (this != &o) { reset(); _h = std::exchange(o._h, {}); } return *this; }
	~Task() { reset(); }

	bool await_ready() const noexcept { return false; }
	std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
		_h.promise().continuation = awaiting;
		return _h;
	}
	void await_resume() {
		if (_h.promise().error)
			std::rethrow_exception(_h.promise().error);
	}

	// Root-task driving, used by the host loop.
	bool valid() const { return bool(_h); }
	bool done() const { return !_h || _h.done(); }
	void start() { _h.resume(); }
	void rethrowIfFailed() const {
		if (_h && _h.done() && _h.promise().error)
			std::rethrow_exception(_h.promise().error);
	}

private:
	explicit Task(std::coroutine_handle<promise_type> h) : _h(h) {}
	void reset() { if (_h) _h.destroy(); _h = {}; }

	std::coroutine_handle<promise_type> _h;
};

// Suspends the entire running coroutine chain; the scheduler resumes the
// stored handle on the next frame.
class FrameScheduler {
public:
	struct Awaiter {
		FrameScheduler &sched;
		bool await_ready() const noexcept { return false; }
		void await_suspend(std::coroutine_handle<> h) noexcept { sched._resume = h; }
		void await_resume() const noexcept {}
	};

	Awaiter nextFrame() { return Awaiter{*this}; }

	// Resumes the suspended chain; returns false if nothing was waiting.
	bool resume() {
		if (!_resume)
			return false;
		auto h = std::exchange(_resume, {});
		h.resume();
		return true;
	}

	bool waiting() const { return bool(_resume); }

private:
	std::coroutine_handle<> _resume;
};

} // namespace gob
