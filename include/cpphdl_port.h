#pragma once

#include <exception>
#include <functional>

extern long sys_clock;  // please declare sys_clock in main cpp

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

    long prev_call_sys_clock;
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
    using func1_type = std::function<A*()>;
    using func2_type = std::function<A()>;

    function_ref() = default;

//    function_ref(func1_type&& f) : func1_(std::move(f)) {}

//    function_ref& operator=(function_ref& other)
//    {
//        func1_ = other.func1_;
//        return *this;
//    }

//    template<typename F>
//    using result_t = std::remove_cvref_t<std::invoke_result_t<F&>>;

    template<typename F>
    requires std::is_pointer_v<std::invoke_result_t<F&>> //std::is_convertible_v<std::invoke_result_t<F>, A*>
    && (!std::same_as<std::remove_cvref_t<F&>, function_ref>)
    function_ref(F&& f)
    {
        func1_ = std::move(f);
    }

    template<typename F>
    requires (!std::is_pointer_v<std::invoke_result_t<F&>>) // std::is_convertible_v<std::invoke_result_t<F>, A>
    && (!std::same_as<std::remove_cvref_t<F&>, function_ref>)
    function_ref(F&& f)
    {
        func2_ = std::move(f);
        func1_ = [&]() -> A* {
            a_tmp = func2_();
            return &a_tmp;
        };
    }

    template<typename F>
//    requires std::same_as<result_t<F>, A*>
    requires std::is_pointer_v<std::invoke_result_t<F&>> //std::is_convertible_v<std::invoke_result_t<F>, A*>
    && (!std::same_as<std::remove_cvref_t<F&>, function_ref>)
    function_ref& operator=(const F& f)
    {
        func1_ = f;
        return *this;
    }

    template<typename F>
//    requires std::same_as<result_t<F>, A>
    requires (!std::is_pointer_v<std::invoke_result_t<F&>>) //std::is_convertible_v<std::invoke_result_t<F>, A>
    && (!std::same_as<std::remove_cvref_t<F&>, function_ref>)
    function_ref& operator=(const F& f)
    {
        func2_ = f;
        func1_ = [&]() -> A* {
            a_tmp = func2_();
            return &a_tmp;
        };
        return *this;
    }

    long prev_call_sys_clock = -1;
    A* cache = nullptr;
    A& operator()() {
        if (cache && prev_call_sys_clock == sys_clock) {  // already calculated
            return *cache;
        }
        prev_call_sys_clock = sys_clock;
        cache = func1_();
        return *cache;
    }

private:
    func1_type func1_;
    func2_type func2_;
    A a_tmp;
};

#endif


}
