Here's a fairly simple program. Can you see the bug? (Aside from using functions with wide contracts, which is a matter of opinion.)

That's right, we failed to invoke any cleanup. That's because when the exception handling mechanism 
[cannot find a handler](https://timsong-cpp.github.io/cppwp/n4659/except.terminate#1.2) for a thrown exception, it's 
[implementation-specified](https://timsong-cpp.github.io/cppwp/n4659/except.terminate#2) whether the stack is unwound. So, if we want to
invoke destructors and shut down the program cleanly, we need to wrap `main()` in a try-catch block.

Oh dear. Now we've managed to invoke destructors and shutdown cleanly, we've lost a heap of useful information about the exception - 
where previously we had a backtrace and a core file, now all we've got is the type and the explanatory string (`what()`).

Can we have both? You might say, why not capture information in the exception object? And yes, Boost.Exception allows that, but you're
paying a heavy cost in case the exception is expected and will be caught and the information discarded. And that only works if everyone 
uses Boost.Exception to augment their throw sites; we want to be able to control this from the catch site, e.g. the catch-all block in
`main()`, or at the root of a handler for a client request in a server, allowing the server to return an error to that client and keep 
running. (Say, like other languages have no problem with.)

But maybe this is possible, with a lot of hacking and a little undefined (and platform-specific) behavior. I'll assume you're all
familiar with how [exception handling works in the Itanium ABI](http://refspecs.linuxbase.org/abi-eh-1.21.html#imp-catch), so let's 
just consider the challenge. More or less, we want pass 1 to succeed, so that `terminate` is not called and pass 2 proceeds to invoke 
destructors. However, we also want to execute a chunk of user-defined code before pass 2 begins. It turns out there's one (and only 
one, as far as I'm aware) hookable step:
https://github.com/gcc-mirror/gcc/blob/41d6b10e96a1de98e90a7c0378437c3255814b16/libstdc%2B%2B-v3/libsupc%2B%2B/eh_personality.cc#L228
Great! - the x86 EH personality calls a virtual method, `__do_catch`, on the catch specification's typeinfo to determine whether the 
catch specification can handle the exception. So, all we need is to create a type with a custom typeinfo -

Wait, what?

Well, under the Itanium ABI, typeinfo objects are polymorphic, which means they have a vtable pointer, which doesn't have to point to 
the `std::typeinfo` vtable, it can point to any vtable with the same layout. They live in `.rodata`, so in order to override the vtable 
pointer we have to suppress generation of the typeinfo and supply our own, in assembler. Typeinfo - being RTTI - gets emitted along 
with the vtable when the first virtual member function is defined, so we need to declare a dummy virtual member function and omit to 
define it. To recap:
* derive from `std::typeinfo`,
* override `__do_catch` to execute our code and return `true`,
* declare a dummy virtual member function and not define it,
* write assembler to output our own vtable and typeinfo with a custom vptr.

I'm not suggesting that you should use this - although I've used it successfully - but I think it does demonstrate a need for more 
information available - on request - at a catch site; and also that this is eminently implementable, at least on the Itanium ABI. 
(I would appreciate pointers on how to implement this within the Windows exception handling system.) Anyone feel like writing a paper?