#include "cpphdl_comb_proof.h"

constexpr bool ordered()
{
    cpphdl::comb_write_proof<3> proof;
    proof.write(2);
    proof.read(2);
    proof.write(1);
    proof.read(1);
    proof.write(0);
    return proof.complete();
}

constexpr bool oldValue()
{
    cpphdl::comb_write_proof<2> proof;
    proof.read(1);
    proof.write(0);
    proof.write(1);
    return proof.complete();
}

constexpr bool partial()
{
    cpphdl::comb_write_proof<2> proof;
    proof.write(0);
    return proof.complete();
}

constexpr bool outOfBounds()
{
    cpphdl::comb_write_proof<1> proof;
    proof.write(0);
    proof.write(1);
    return proof.complete();
}

static_assert(ordered());
static_assert(!oldValue());
static_assert(!partial());
static_assert(!outOfBounds());

int main() {}
