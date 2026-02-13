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

    template<typename T> using remove_cvref_t = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
    template<typename F> using invoke_result_t = typename std::invoke_result_t<F&>;

    template<typename F> using enable_if_pointer =
        typename std::enable_if<std::is_pointer<invoke_result_t<F>>::value && !std::is_same<remove_cvref_t<F>,function_ref>::value>::type;

    template<typename F> using enable_if_not_pointer =
        typename std::enable_if<!std::is_pointer<invoke_result_t<F>>::value && !std::is_same<remove_cvref_t<F>,function_ref>::value>::type;

    // we can accept pointers and objects

    template<typename F>
    function_ref(F&& f, enable_if_pointer<F>* = nullptr)
    {
        func1_ = std::move(f);
    }

    template<typename F>
    function_ref(F&& f, enable_if_not_pointer<F>* = nullptr)
    {
        func2_ = std::move(f);
        func1_ = [&]() -> A* {
            a_tmp = func2_();
            return &a_tmp;
        };
    }

    template<typename F, typename = enable_if_pointer<F>>
    function_ref& operator=(const F& f)  // we dont destroy source object
    {
        func1_ = f;
        return *this;
    }

    template<typename F, typename = enable_if_not_pointer<F>, typename = void>
    function_ref& operator=(const F& f)  // we dont destroy source object
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
