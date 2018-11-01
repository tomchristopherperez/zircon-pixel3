// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_BRIDGE_H_
#define LIB_FIT_BRIDGE_H_

#include "bridge_internal.h"

namespace fit {

// A bridge is a building block for asynchronous control flow that is formed
// by the association of two distinct participants: a completer and a consumer.
//
// - The completer is responsible for reporting completion of an asynchronous
//   task and providing its result.  See |completer()| and |fit::completer|.
// - The consumer is responsible for consuming the result of the asynchronous
//   task.  See |consumer()| and |fit::consumer|.
//
// This class is often used for binding a |fit::promise| to a callback,
// facilitating interoperation of promises with functions that asynchronously
// report their result via a callback function.  It can also be used more
// generally anytime it is necessary to decouple completion of an asynchronous
// task from consumption of its result (possibly on different threads).
//
// The completer and consumer each possesses a unique capability that can
// be exercised at most once during their association: the asynchronous
// task represented by a bridge can be completed at most once and its
// result can be consumed at most once.  This property is enforced by
// a single-ownership model for completers and consumers.
//
// The completion capability has a single owner represented by |fit::completer|.
// Its owner may exercise the capability to complete the task (provide its result),
// it may transfer the capability by moving it to another completer instance,
// or it may cause the asynchronous task to be "abandoned" by discarding the
// capability, implying that the task can never produce a result.
// See |fit::consumer::promise()| and |fit::consumer::promise_or_error()| for
// details on how abandonment of the task is handled by the consumer.
//
// The consumption capability has a single owner represented by |fit::consumer|.
// Its owner may exercise the capability to consume the task's result (as a
/// promise), it may transfer the capability by moving it to another consumer
// instance, or it may cause the asynchronous task to be "canceled" by
// discarding the capability, implying that the task's result can never be
// consumed.  When this occurs, the task's eventual result will be discarded.
//
// SYNOPSIS
//
// |V| is the type of value produced when the task completes successfully.
// Use |std::tuple<Args...>| if the task produces multiple values, such as
// when you intend to bind the task's completer to a callback with multiple
// arguments using |fit::completer::bind_tuple()|.
// Defaults to |void|.
//
// |E| is the type of error produced when the task completes with an error.
// Defaults to |void|.
//
// EXAMPLE
//
// Imagine a File I/O library offers a callback-based asynchronous reading
// function.  We suppose that the read handling code will invoke the
// callback upon completion.  The library's API might look a bit like this:
//
//     using read_callback = fit::function<void(size_t bytes_read)>;
//     void read_async(size_t num_bytes, uint8_t* buffer, read_callback cb);
//
// Here's how we can adapt the library's "read_async" function to a
// |fit::promise| by binding its callback to a bridge:
//
//     fit::promise<size_t> promise_read(uint8_t* buffer, size_t num_bytes) {
//         fit::bridge<size_t> bridge;
//         read_async(num_bytes, buffer, bridge.completer().bind());
//         return bridge.consumer().promise_or(::fit::error());
//     }
//
// Finally we can chain additional asynchronous tasks to be performed upon
// completion of the promised read:
//
//     uint8_t buffer[4096];
//     void my_program(fit::executor* executor) {
//         auto promise = promise_read(buffer, sizeof(buffer))
//             .and_then([] (size_t bytes_read) {
//                 // consume contents of buffer
//             })
//             .or_else() {
//                 // handle error case
//             });
//         executor->schedule_task(std::move(promise));
//     }
//
// Similarly, suppose the File I/O library offers a callback-based asynchronous
// writing function that can return a variety of errors encoded as negative
// sizes.  Here's how we might decode those errors uniformly into |fit::result|
// allowing them to be handled using combinators such as |or_else|.
//
//     using write_callback = fit::function<void(ssize_t bytes_written)>;
//     void write_async(size_t num_bytes, uint8_t* buffer, write_callback cb);
//
//     fit::promise<size_t, int> promise_write(uint8_t* buffer, size_t num_bytes) {
//         fit::bridge<size_t, int> bridge;
//         write_async(num_bytes, buffer,
//             [completer = std::move(bridge.completer())](ssize_t bytes_written) {
//             if (bytes_written < 0) {
//                 int error = bytes_written;
//                 completer.complete_error(error);
//                 return;
//             }
//             completer.complete_ok(bytes_written);
//         });
//         return bridge.consumer().promise_or(::fit::error(ERR_ABANDONED));
//     }
//
//     uint8_t buffer[4096];
//     void my_program(fit::executor* executor) {
//         auto promise = promise_write(buffer, sizeof(buffer))
//             .and_then([] (size_t bytes_written) {
//                 // consume contents of buffer
//             })
//             .or_else(int error) {
//                 // handle error case
//             });
//         executor->schedule_task(std::move(promise));
//     }
//
// See documentation of |fit::promise| for more information.
template <typename V, typename E>
class bridge final {
    using bridge_state = ::fit::internal::bridge_state<V, E>;

public:
    using value_type = V;
    using error_type = E;
    using result_type = result<value_type, error_type>;
    using completer_type = ::fit::completer<V, E>;
    using consumer_type = ::fit::consumer<V, E>;

    // Creates a bridge representing a new asynchronous task formed by the
    // association of a completer and consumer.
    bridge() {
        bridge_state::create(&completer_.completion_ref_,
                             &consumer_.consumption_ref_);
    }
    bridge(bridge&& other) = default;
    ~bridge() = default;

    bridge& operator=(bridge&& other) = default;

    // Gets a reference to the bridge's completer capability.
    // The completer can be moved out of the bridge, if desired.
    completer_type& completer() { return completer_; }
    const completer_type& completer() const { return completer_; }

    // Gets a reference to the bridge's consumer capability.
    // The consumer can be moved out of the bridge, if desired.
    consumer_type& consumer() { return consumer_; }
    const consumer_type& consumer() const { return consumer_; }

    bridge(const bridge& other) = delete;
    bridge& operator=(const bridge& other) = delete;

private:
    completer_type completer_;
    consumer_type consumer_;
};

// Provides a result upon completion of an asynchronous task.
//
// Instances of this class have single-ownership of a unique capability for
// completing the task.  This capability can be exercised at most once.
// Ownership of the capability is implicitly transferred away when the
// completer is abandoned, completed, or bound to a callback.
//
// See also |fit::bridge|.
// See documentation of |fit::promise| for more information.
//
// SYNOPSIS
//
// |V| is the type of value produced when the task completes successfully.
// Use |std::tuple<Args...>| if the task produces multiple values, such as
// when you intend to bind the task's completer to a callback with multiple
// arguments using |fit::completer::bind_tuple()|.
// Defaults to |void|.
//
// |E| is the type of error produced when the task completes with an error.
// Defaults to |void|.
template <typename V, typename E>
class completer final {
    using bridge_state = ::fit::internal::bridge_state<V, E>;
    using completion_ref = typename bridge_state::completion_ref;

public:
    using value_type = V;
    using error_type = E;
    using result_type = ::fit::result<V, E>;

    completer() = default;
    completer(completer&& other) = default;
    ~completer() = default;

    completer& operator=(completer&& other) = default;

    // Returns true if this instance currently owns the unique capability for
    // reporting completion of the task.
    explicit operator bool() const { return !!completion_ref_; }

    // Explicitly abandons the task, meaning that it will never be completed.
    // See |fit::bridge| for details about abandonment.
    void abandon() {
        assert(completion_ref_);
        completion_ref_ = completion_ref();
    }

    // Reports that the task has completed successfully.
    // This method takes no arguments if |value_type| is void, otherwise it
    // takes one argument which must be assignable to |value_type|.
    template <typename VV = value_type,
              typename = std::enable_if_t<std::is_void<VV>::value>>
    void complete_ok() {
        assert(completion_ref_);
        bridge_state* state = completion_ref_.get();
        state->complete_or_abandon(std::move(completion_ref_),
                                   ::fit::ok());
    }
    template <typename VV = value_type,
              typename = std::enable_if_t<!std::is_void<VV>::value>>
    void complete_ok(VV value) {
        assert(completion_ref_);
        bridge_state* state = completion_ref_.get();
        state->complete_or_abandon(std::move(completion_ref_),
                                   ::fit::ok<value_type>(std::forward<VV>(value)));
    }

    // Reports that the task has completed with an error.
    // This method takes no arguments if |error_type| is void, otherwise it
    // takes one argument which must be assignable to |error_type|.
    template <typename EE = error_type,
              typename = std::enable_if_t<std::is_void<EE>::value>>
    void complete_error() {
        assert(completion_ref_);
        bridge_state* state = completion_ref_.get();
        state->complete_or_abandon(std::move(completion_ref_),
                                   ::fit::error());
    }
    template <typename EE = error_type,
              typename = std::enable_if_t<!std::is_void<EE>::value>>
    void complete_error(EE error) {
        assert(completion_ref_);
        bridge_state* state = completion_ref_.get();
        state->complete_or_abandon(std::move(completion_ref_),
                                   ::fit::error<error_type>(std::forward<EE>(error)));
    }

    // Reports that the task has completed or been abandoned.
    // See |fit::bridge| for details about abandonment.
    //
    // The result state determines the task's final disposition.
    // - |fit::result_state::ok|: The task completed successfully.
    // - |fit::result_state::error|: The task completed with an error.
    // - |fit::result_state::pending|: The task was abandoned.
    void complete_or_abandon(result_type result) {
        assert(completion_ref_);
        bridge_state* state = completion_ref_.get();
        state->complete_or_abandon(std::move(completion_ref_),
                                   std::move(result));
    }

    // Returns a callback that reports completion of the asynchronous task along
    // with its result when invoked.  This method is typically used to bind
    // completion of a task to a callback that has zero or one argument.
    //
    // If |value_type| is void, the returned callback's signature is: void(void)
    // Otherwise, the returned callback's signature is: void(value_type).
    //
    // The returned callback is thread-safe and move-only.
    ::fit::internal::bridge_bind_callback<V, E> bind() {
        assert(completion_ref_);
        return ::fit::internal::bridge_bind_callback<V, E>(
            std::move(completion_ref_));
    }

    // A variant of |bind()| that can be used to bind a completion of a task
    // to a callback that has zero or more arguments by wrapping the callback's
    // arguments into a tuple when producing the task's result.
    //
    // The |value_type| must be a tuple type.
    // Given a |value_type| of std::tuple<Args...>, the returned callback's
    // signature is: void(Args...).  Note that the tuple's fields are
    // unpacked as individual arguments of the callback.
    //
    // The returned callback is thread-safe and move-only.
    ::fit::internal::bridge_bind_tuple_callback<V, E> bind_tuple() {
        assert(completion_ref_);
        return ::fit::internal::bridge_bind_tuple_callback<V, E>(
            std::move(completion_ref_));
    }

    completer(const completer& other) = delete;
    completer& operator=(const completer& other) = delete;

private:
    friend class bridge<V, E>;

    completion_ref completion_ref_;
};

// Consumes the result of an asynchronous task.
//
// Instances of this class have single-ownership of a unique capability for
// consuming the task's result.  This capability can be exercised at most once.
// Ownership of the capability is implicitly transferred away when the
// task is canceled or converted to a promise.
//
// See also |fit::bridge|.
// See documentation of |fit::promise| for more information.
//
// SYNOPSIS
//
// |V| is the type of value produced when the task completes successfully.
// Use |std::tuple<Args...>| if the task produces multiple values, such as
// when you intend to bind the task's completer to a callback with multiple
// arguments using |fit::completer::bind_tuple()|.
// Defaults to |void|.
//
// |E| is the type of error produced when the task completes with an error.
// Defaults to |void|.
template <typename V, typename E>
class consumer final {
    using bridge_state = ::fit::internal::bridge_state<V, E>;
    using consumption_ref = typename bridge_state::consumption_ref;

public:
    using value_type = V;
    using error_type = E;
    using result_type = ::fit::result<V, E>;

    consumer() = default;
    consumer(consumer&& other) = default;
    ~consumer() = default;

    consumer& operator=(consumer&& other) = default;

    // Returns true if this instance currently owns the unique capability for
    // consuming the result of the task upon its completion.
    explicit operator bool() const { return !!consumption_ref_; }

    // Explicitly cancels the task, meaning that its result will never be consumed.
    // See |fit::bridge| for details about cancelation.
    void cancel() {
        assert(consumption_ref_);
        consumption_ref_ = consumption_ref();
    }

    // Returns an unboxed promise which resumes execution once this task has
    // completed.  If the task is abandoned by its completer, the promise
    // will not produce a result, thereby causing subsequent tasks associated
    // with the promise to also be abandoned and eventually destroyed if
    // they cannot make progress without the promised result.
    promise_impl<typename bridge_state::promise_continuation>
    promise() {
        return promise_or(::fit::pending());
    }

    // A variant of |promise()| that allows a default result to be provided when
    // the task is abandoned by its completer.  Typically this is used to cause
    // the promise to return an error when the task is abandoned instead of
    // causing subsequent tasks associated with the promise to also be abandoned.
    //
    // The state of |result_if_abandoned| determines the promise's behavior
    // in case of abandonment.
    //
    // - |fit::result_state::ok|: Reports a successful result.
    // - |fit::result_state::error|: Reports a failure result.
    // - |fit::result_state::pending|: Does not report a result, thereby
    //   causing subsequent tasks associated with the promise to also be
    //   abandoned and eventually destroyed if they cannot make progress
    //   without the promised result.
    promise_impl<typename bridge_state::promise_continuation>
    promise_or(result_type result_if_abandoned) {
        assert(consumption_ref_);
        bridge_state* state = consumption_ref_.get();
        return make_promise_with_continuation(
            state->promise_or(std::move(consumption_ref_),
                              std::move(result_if_abandoned)));
    }

    consumer(const consumer& other) = delete;
    consumer& operator=(const consumer& other) = delete;

private:
    friend class bridge<V, E>;

    consumption_ref consumption_ref_;
};

} // namespace fit

#endif // LIB_FIT_BRIDGE_H_
