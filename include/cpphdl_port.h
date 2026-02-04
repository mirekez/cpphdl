#pragma once

#include <exception>
#include <functional>

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

    R operator()() const
    {
        return *fn_();
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

    A& operator()() const {
        A* ptr = func_();
        if (!ptr) {
            throw std::runtime_error("function_ref: function returned null");
        }
        return *ptr;
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(func_);
    }

private:
    func_type func_;
};

#endif


}
