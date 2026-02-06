#pragma once

#include <exception>
#include <functional>

extern unsigned long sys_clock;  // please declare sys_clock in main cpp

namespace cpphdl {


#ifdef CPPHDL_STATIC

template <typename R>
class function_ref
{
public:
    using fn_t = R* (*)();

    fn_t fn_ = nullptr;

    function_ref() = default;

    function_ref(fn_t&& fn) noexcept : fn_(fn) {}

    template <typename F, typename = std::enable_if_t<std::is_invocable_r_v<R*, F>>>
    function_ref(F&& f) noexcept : fn_(reinterpret_cast<fn_t>(+f))
    {}

    function_ref& operator=(fn_t fn) noexcept
    {
        fn_ = fn;
        return *this;
    }

    unsigned long prev_call_sys_clock;
    R* cache = nullptr;
    R& operator()()
    {
        if (cache && prev_call_sys_clock == sys_clock) {  // already calculated
            return *cache;
        }
        prev_call_sys_clock = sys_clock;
        cache = fn_();
        return *cache;
    }

    explicit operator bool() const noexcept
    {
        return fn_ != nullptr;
    }

    void reset() noexcept
    {
        fn_ = nullptr;
    }
};

#else  // for non static version we capture this in comb functions

template <typename A>
class function_ref
{
public:
    using func_type = std::function<A*()>;

    function_ref() = default;

    function_ref(func_type f) : func_(std::move(f)) {}

    template<typename F>
    function_ref(F f) : func_(std::move(f)) {}

    unsigned long prev_call_sys_clock;
    A* cache = nullptr;
    A& operator()() {
        if (cache && prev_call_sys_clock == sys_clock) {  // already calculated
            return *cache;
        }
        prev_call_sys_clock = sys_clock;
        cache = func_();
        return *cache;
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(func_);
    }

private:
    func_type func_;
};

#endif


}
