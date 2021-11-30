<pre class='metadata'>
Title:  Zero-overhead exception stacktraces
Shortname: P2490
URL: https://wg21.link/P2490
Revision: 1
Audience: LEWG
Status: P
Group: WG21
Issue Tracking: GitHub https://github.com/ecatmur/stacktrace-from-exception/issues
!Source: <a href="https://github.com/ecatmur/stacktrace-from-exception/blob/main/paper.md">github.com/ecatmur/stacktrace-from-exception/blob/main/paper.md</a>
No Abstract: yes
Markup Shorthands: markdown yes
Markup Shorthands: biblio yes
Editor: Ed Catmur, ed@catmur.uk
</pre>
<pre class='biblio'>
{
    "p2370": {
        "title": "Stacktrace from exception",
        "href": "http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p2370r1.html"
    },
    "jeng": {
        "title": "The Visual C++ Exception Model",
        "href": "https://www.gamedev.net/tutorials/programming/general-and-gameplay-programming/the-visual-c-exception-model-r2488/"
    },
    "itanium": {
        "title": "C++ ABI for Itanium: Exception Handling",
        "href": "https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html"
    },
    "seh": {
        "title": "Structured Exception Handling (C/C++)",
        "href": "https://docs.microsoft.com/en-us/cpp/cpp/structured-exception-handling-c-cpp"
    },
    "p0709": {
        "title": "Zero-overhead deterministic exceptions: Throwing values",
        "href": "http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0709r4.pdf"
    },
    "try-except": {
        "title": "`try-except` statement",
        "href": "https://docs.microsoft.com/en-us/cpp/cpp/try-except-statement"
    },
    "poc": {
        "title": "Proof-of-concept implementation.",
        "href": "https://github.com/ecatmur/stacktrace-from-exception/blob/main/stacktrace-from-exception.cpp"
    },
    "python": {
        "title": "sys — System-specific parameters and functions",
        "href": "https://docs.python.org/3/library/sys.html#sys.exc_info"
    }
}
</pre>

# Overview

## Abstract

This paper identifies concerns with part of the **Stacktrace from exception** [[p2370]] proposal.  We suggest alternate approaches and offer implementation experience of the
techniques that could underly such alternatives.

## Background

The paper **Stacktrace from exception** [[p2370]] amply sets out why it is desired to be able to access a stacktrace from exception; that is, when *handling* an exception it should be
possible to retrieve a stacktrace from the (most recent) `throw` point of the exception, through the point of handling; and that this should be *transparent* to and not require
*cooperation by* or *modification of* throwing code.  That paper acknowledges that the cost of taking a stacktrace on *every* exception throw would be prohibitive and proposes a
mechanism to disable it via a standard library routine `std::this_thread::set_capture_stacktraces_at_throw` that will set a thread-local flag.

We argue that this mechanism still imposes a runtime cost and would not achieve the aims of the paper for the proposed facility.

### Runtime cost

Accessing a thread-local variable has a cost in instructions and memory access; at present this could be argued to be lost in the "noise" of the existing exception handling
machinery, particularly as this currently involves memory allocation, but in future if and when the **Zero-overhead deterministic exceptions: Throwing values** [[p0709]] proposal is adopted this will become relatively more
significant.

In any case even a *de minimis* runtime cost is not zero.

### Old third-party libraries

Under the proposed mechanism, throw sites would need recompilation and/or relinking to participate in the facility.  It is entirely possible that third-party library code is
shipped with its own implementations of the exception-raising mechanism, such that it would not participate in the facility until such time as the vendor recompiles and relinks
the library, which may not come for years or even decades.

### Conflicting requirements

Third-party library vendors who use exceptions for control flow may be expected to view the proposed facility negatively; if user code enables it via the proposed mechanism the
cost will be considerable even for exceptions that are caught and handled successfully entirely within the third-party library.  Thus they are likely to disable the facility at
API entry points, both negating the point of the facility for any exceptions that *do* leak out of the third-party library, and interfering with user code that expects it to
remain enabled.

## Alternatives

We note that C++ exception handling is typically built on top of a lower-level, language-agnostic facility.  On Windows this is structured exception handling [[seh]], 
while on the Itanium ABI (used by most Unix-style OSes on x64-64) it is the Level I Base ABI [[itanium]].  This lower-level facility uses *two-phase* exception handling; in 
the first, "search" phase the stack is walked from the throw point to identify a suitable handler, while in the second, "unwind" phase it is walked again from the 
throw point to the selected handler, this time invoking cleanup (i.e., destructors) along the way. Importantly,

* during the whole of the search phase the stack is still intact, and
* identifying a handler is *dynamic*, calling into a compiler- or library-generated match function.

This suggests a possible alternative mechanism; viz.:

* user code can mark a `catch` block as requiring a stacktrace, either via a special function or via new syntax; and
* on recognising `catch` blocks so marked, the compiler can emit suitable code or data such that *if and when* that `catch` block is selected as a handler for a thrown
  exception, it takes a stacktrace during search phase, immediately before nominating that `catch` block as a suitable handler; then
* the user code can retrieve that stored stacktrace during exception handling, after stack unwinding.

The advantages of this approach are twofold:

* *transparency*: there is no need whatsoever for throwing code to be modified, recompiled or relinked.  Indeed, since this mechanism relies solely on changes to the catch
  site, code using this mechanism may be introduced into existing (perhaps even running) programs without any need for those programs to be recompiled or relinked, as long as that new code has access
  to any necessary support libraries;
* *zero-cost*: if the search phase does not reach a `catch` block so marked (i.e., if the exception is caught and handled internally) then behavior is entirely unaffected.

## Acknowledgements

Thank you especially to Antony Peacock for getting this paper ready for initial submission, and to Mathias Gaunard for inspiration, review and feedback. Thank you also to Jonathan Wakely and to members of BSI IST/5/-/21 (C++) panel for review and feedback.

## Revision history

: R0
:: Initial revision; incorporated informal feedback.
: R1
:: Add `with_stacktrace` proposed syntax; add additional parameter (non-default) syntax. Extend discussion of rethrow. Add discussion of fallback implementation, coroutines, and allocators.

# Possible syntaxes

We now propose a number of possible syntaxes for user code to opt in to the mechanism and retrieve the stored stacktrace.

## Special function

Under this mechanism, a `catch` block that contains a potentially-evaluated call to `std::stacktrace::from_current_exception()` is marked as requiring a stacktrace to be taken
during search phase:

```c++
try {
    ...
} catch (std::exception& ex) {
    std::cout << ex.what() << "\n" << std::stacktrace::from_current_exception() << std::endl;
}
```

Drawback: if the call is moved out of the `catch` block, e.g. to a helper function, or even perhaps to a lambda within it, the facility will cease to work.
Possible workaround: make `std::stacktrace::from_current_exception()` ill-formed when called from anywhere other than a `catch` block - this could be ugly.

## Additional parameter

(Gašper Azman / Bronek Kozicki)

```c++
try {
    ...
} catch (std::exception& ex, std::stacktrace st) {
    std::cout << ex.what() << "\n" << st << std::endl;
}
```

This shows some similarity to Python [[python]], where `sys.exc_info` yields a tuple of exception and traceback.

Drawbacks: new syntax, could be understood as providing multiple types to be caught.

## Default parameter

```c++
try {
    ...
} catch (std::exception& ex, std::stacktrace st = std::stacktrace::from_current_exception()) {
    std::cout << ex.what() << "\n" << st << std::endl;
}
```

This can be understood by analogy to `std::source_location::current()` which similarly has special behavior in particular contexts.

Drawbacks: new syntax, could be understood as providing multiple types to be caught.

## "catch-with-init"

```c++
try {
    ...
} catch (auto st = std::stacktrace::current(); std::exception& ex) {
    std::cout << ex.what() << "\n" << st << std::endl;
}
```

Here we add an (optional) *init-statement* to the `catch` clause, to be executed before unwind (if the catch clause is selected).

Drawbacks: new syntax, may not be safe to run general user code during search phase.

## Expose search phase

```c++
try {
    ...
} catch (std::exception& ex) if (auto st = std::stacktrace::current(); true) {
    std::cout << ex.what() << "\n" << st << std::endl;
}
```

Drawbacks: new syntax, open to abuse, may not be safe to run general user code during search phase.

## Wrapper type

(Credit: Jonathan Wakely)

```c++
try {
    ...
} catch (std::with_stacktrace<std::exception&> e) {
    std::cout << e.get_exception().what() << "\n" << e.get_stacktrace() << std::endl;
}
```

This would require special-case handling by the unwind mechanism to match the wrapped exception type, but no changes to C++ grammar. Indeed, on many platforms it can be implemented [[#implementation-experience]] with only changes to the library.

Issue(ecatmur/stacktrace-from-exception#10): EC todo impl with_stacktrace

Drawbacks: greater verbosity compared to syntaxes where `ex` and `st` are separate variables.

* How to `catch (...)` - perhaps `catch (std::with_stacktrace<> e)` (empty template argument list), or `std::with_stacktrace<void>`?
* Can template parameter `E` be cvref qualified? - probably yes, to minimize code changes; this means that `get_exception()` should return by reference, but the returned reference should refer to the exception object when `E` is a reference type, and to a copy of the exception object otherwise.

Sketch:

```c++
template<class E, class Allocator = std::allocator<std::stacktrace_entry>, size_t max_depth = -1>
struct with_stacktrace {
    using exception_type = conditional_t<is_array_v<E>, remove_extent_t<E>*, conditional_t<is_function_v<E>, E*, E>>;
    using stacktrace_type = basic_stacktrace<Allocator>;
    exception_type& get_exception() const noexcept requires (not is_void_v<E>);
    stacktrace_type& get_stacktrace() const;
};
```

# Concerns

## Implementability

We do not know whether this mechanism is indeed implementible on all platforms.  We do know that (and, indeed, have practical experience [[#implementation-experience]] to show that) it is 
implementable on two major platforms (i.e. Windows on Intel, and Unix-like on x86-64) that between them cover a dominant proportion of the market.  We would welcome 
information regarding other platforms.

### Platform survey

Issue(ecatmur/stacktrace-from-exception#9): EC todo platform survey

### Fallback

Any platform with two-phase lookup and dynamic search phase is suitable for implementation of the proposed mechanism. For platforms that do not fall under this description, a suitable fallback could be to store a thread-local flag as in [[p2370]], that instructs the exception mechanism to take a stacktrace at the point of `throw`. This flag would be set to `true` on entry to a `try` block with accompanying `catch` block marked as requiring stacktrace, to `false` on entry to `noexcept` functions and to `try` blocks with accompanying `catch(...)` block not marked as requiring stacktrace, and restored to its previous value during stack unwinding.

This would still have the advantage over the suggested API in [[p2370]] that the `capture_stacktraces_at_throw` flag would be hidden and automatically set or restored to the appropriate value according to whether a stacktrace is desired in a particular (dynamic) scope.

Alternatively, vendors could choose to not provide support for the facility, or (under QOI) to provide a stub implementation.

## Secrecy

Third-party vendors who view secrecy as a virtue may be tempted to put `catch (...)` blocks at API entry points to prevent information on their library internals leaking 
out.  In practice they can achieve much the same end by stripping debug symbols and obfuscating object names, and are likely to do so; meanwhile the same information is 
available by attaching a debugger.

It has been suggested that the Standard may wish to provide an attribute for users to denote that a stack frame should be omitted from stack traces. We consider this out of scope for this proposal.

## Rethrow

For a *rethrown* exception (using `throw;`, `rethrow_exception`, `throw_with_nested`, etc.) the stacktrace will be truncated from the rethrow point.  We could provide mechanisms to alleviate this; for example, we could specify that `throw;` preserves stacktrace (specifically, that the accompanying stacktrace of a rethrown exception begins with the stacktrace captured for the use of its containing `catch`).

However, since `throw;` may be placed within a nested function invocation, the resulting chained stacktrace could either be non-contiguous, indeed self-overlapping (if it restarts from `throw;`) or, if it does not restart, omit important information (the stack from the catch clause to the rethrow point, showing how and why the exception was rethrown). At present, in the light of the complication and potential confusion arising, we choose not to pursue this.

An alternative approach could be to extend `throw_with_nested` to accept a stacktrace and store it in `nested_exception` or a derived class. Since this would be a pure library extension, we are not pursuing it in this paper but leave it open for future direction.

## Coroutines

The body of a coroutine is defined as-if containing a catch-all block:

```c++
try {
    co_await promise.initial_suspend();
    function-body
} catch (...) {
    if (!initial-await-resume-called)
        throw;
    promise.unhandled_exception();
}
```

This poses two problems: firstly, the stacktrace is truncated by the rethrow, and secondly it is not available to `unhandled_exception`. We tentatively suggest that the rethrow should be defined as preserving the stacktrace (this can be achieved by only installing the exception handler at the point of initial await-resume), and that `unhandled_exception` should be passed an appropriate stacktrace object where that call is well-formed.

## Allocators

Some syntaxes permit the user to supply an allocator; this is desirable for performance and/or latency.  In this case it would be preferable to also allow the user to supply a `max_depth` (but not `skip`) parameter as supplied to `basic_stacktrace::current()`, so that the stacktrace can (if desired) be constructed into a fixed-size buffer with no allocation overhead during unwinding.

On the other hand, allowing the user to supply an allocator opens the door to abuse (running arbitrary user code during unwinding).

# Implementation experience

The following implementations are provided solely to demonstrate implementability; we anticipate that any Standard implementation would be significantly less ugly in both
internals and in use.

## Windows

It is well known that the vendor-specific `__try` and `__except` keywords [[try-except]] (present in Visual Studio and compatible compilers) permit arbitrary code to be invoked during search 
phase, since the argument to the `__except` keyword is a *filter-expression* evaluated during search phase, to an enumeration indicating whether the consequent code block is to 
be selected as the handler.  We present a proof-of-concept implementation [[poc]] (32-bit and 64-bit) adapted from an article by Howard Jeng [[jeng]].

## Itanium

Although exception handling on Itanium is also two-phase, the handler selection mechanism is largely hidden from the user.  However, there is a workaround involving creating a
type whose run-time type information (that is, its `typeid`) refers to an instance of a user-defined subclass of `std::type_info`.  This technique is not particularly widely known, but has been used 
in several large proprietary code bases to good effect for some time.  We present a proof-of-concept implementation [[poc]].
