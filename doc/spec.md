---
title: "C++HDL Specification"
author: "Mike Reznikov"
date: 2025-09-10
version: "v1.0"
---

# Project Specification

**Document Version:** v1.0  
**Author:** Mikhail Reznikov  
**Date:** 2025-09-10  

---

<div style="page-break-after: always;"></div>

# DRAFT

This document is currently in **draft** status.  
Content may change significantly before final approval.

---

# Table of Contents
- [Introduction](#introduction)
- [Requirements](#requirements)
- [Design](#design)
- [Appendix](#appendix)

---

# Introduction


&nbsp;&nbsp;&nbsp;&nbsp;C++HDL is an C++ language extension, designed for two purposes:

1. Building a full cycle of digital RTL development using C++ language
2. Allowing extra fast simulation of an Verilog-style written RTL

&nbsp;&nbsp;&nbsp;&nbsp;In all operations C++HDL works as a reflection of SystemVerilog model,
which means that, on all stages, 100% register-to-register copy of any C++HDL
code exists in SystemVerilog domain.
This allows C++HDL to SystemVerilog conversion for the purposes of

* Connection of C++ developers teams to classical verification and testing teams
* Final RTL delivery to fabrication processes and tools or third-party companies

&nbsp;&nbsp;&nbsp;&nbsp;The main idea of using C++ in RTL development is in replacing **simulation** with **execution**
to provide ~20 times faster compilation and running of a model during code writing.
The following properties of C++ language provide strong background for RTL development process:

* C++ provides ability to use any of plenty of IDEs/tools for development and debugging,
including a lot of professional tools, supporting all necessary stages for huge projects management
* C++ is a one of the most popular and powerful programming languages in the World, having extra wide community
and potentially all specific features of all compilers/tool described in minute details
* C++HDL is shifting hardware development to software development (at least as one of the stages of a process)
which makes a lot of software developers potentially accessible on the labor market
* C++ is extra fast. There is no another language with same abilities which can run RTL faster

RTL modelling using C++HDL should be made together with verification and testing, using all power and speed of
C++ language in modelling of digital signalling and digital systems interaction. It is also possible to involve
third-party SystemVerilog code in C++HDL development project using Verilator tool.
Therefore, C++HDL is intended to make Verification process much more simple, flexible, fast, and only containing programming
tasks, avoiding complex proprietary simulators requirements and allowing running of hundreds CPU cores with thousands of tests each day.

Same C++ testing and verification software then can be used to prove functionality in SV domain (despite it's 20-100 times slower than
C++HDL verification, it can be run once a week or a month just to prove SV generated files are still fully qualified).
Generated SystemVerilog files can be taken at any moment and used as the main source code for next stage of ASIC/FPGA/other production process.
Conversion tool provides creation of readable and structured SystemVerilog files containing all source comments and data structures.

# Limitations

* C++HDL supports only digital design components, written using blocking assignments
* Currently no multiclock or CDC is supported. Each clock domain should be developed separately
* Only clock-synchronous logic is supported currently

&nbsp;&nbsp;&nbsp;&nbsp;C++HDL is intended to bring ease and speed in development of various digital circuits like controllers,
multiplexors, cache and memory functions, mathematics functions, digital data processing, transmitting circuits, etc.

# Requirements

&nbsp;&nbsp;&nbsp;&nbsp;C++HDL is being delivered in two parts:

* C++ headers which contain some classes for C++HDL datatypes
* Conversion/checking tool which provides C++ to SystemVerilog conversion and syntax checking

C++HDL is actively using std::format during debug printing so GCC 14 or CLANG 18 is recommended, while it's still possible to
achieve execution using older compilators.

# C++HDL syntax

&nbsp;&nbsp;&nbsp;&nbsp;C++HDL source file can be written as .h C++ header or .cpp object file.
Each header file should describe a module or auxiliary information like datatypes, constants, inline functions definitions, etc.

## Module description format

&nbsp;&nbsp;&nbsp;&nbsp;Module definition is represented by C++ class and strictly splitted into 5 main sections and 1 optional:

1. Private: nested instances members list
2. Public: I/O ports definition and *connect()* function body
3. Private: full list of variables: registers, combinational values and all temporary variables
4. Public: combinational functions bodies
5. Public: *reset()*, *work()*, *strobe()* and *comb()* functions bodies (same order is strongly recommended)
6. Public: std::string name variable to handle instance name during hierarchy inferring

&nbsp;&nbsp;&nbsp;&nbsp;Simple FIFO module description using C++HDL is provided as example:

```cpp

#pragma once

#include "cpphdl.h"
#include "PrjConfig.h"
#include "Memory.h"

template<size_t FIFO_WIDTH_BYTES=BUS_WIDTH, size_t FIFO_DEPTH=FIFO_ROWS>
class Fifo
{
    Memory<FIFO_WIDTH_BYTES,FIFO_DEPTH> mem;
public:
    logic<FIFO_WIDTH_BYTES*8>* data_in = nullptr;
    bool* write_in = nullptr;
    logic<FIFO_WIDTH_BYTES*8>* data_out = mem.data_out;
    bool* read_in = nullptr;
    bool* empty_out = &empty_comb;
    bool* full_out = &full_comb;
    bool* clear_in = &ZERO;
    bool* afull_out = &afull_reg;

    void connect()
    {
        mem.data_in = data_in;
        mem.write_in = write_in;
        mem.read_in = read_in;
        mem.write_addr_in = &wp_reg;
        mem.read_addr_in = &rp_reg;
        mem.connect();
    }
private:

    u1 full_comb;
    u1 empty_comb;

    reg<u<clog2(FIFO_DEPTH)>> wp_reg;
    reg<u<clog2(FIFO_DEPTH)>> rp_reg;
    reg<u1> full_reg;

    reg<u1> afull_reg;

    size_t i;
public:

    bool full_comb_func()
    {
        full_comb = (wp_reg.next == rp_reg.next) && full_reg.next;
        return full_comb;
    }

    bool empty_comb_func()
    {
        empty_comb = (wp_reg.next == rp_reg.next) && !full_reg.next;
        return empty_comb;
    }

    void reset()
    {
        wp_reg.clr();
        rp_reg.clr();
        full_reg.clr();
        afull_reg.clr();

        mem.reset();
    }

    void work(int clk)
    {
        if (!clk) return;

        if (*read_in) {

            if (empty_comb_func()) {
                printf("%s: reading from an empty fifo\n", name.c_str());
                exit(1);
            }
            if (!empty_comb_func()) {
                rp_reg.next = rp_reg + 1;
            }
            if (!*write_in) {
                full_reg.next = 0;
            }
        }

        // rp_reg.next could be changed, lowering full_comb down

        if (*write_in) {

            if (full_comb_func()) {
                printf("%s: writing to a full fifo\n", name.c_str());
                exit(1);
            }
            if (!full_comb_func()) {
                wp_reg.next = wp_reg + 1;
            }
            if (wp_reg.next == rp_reg.next) {
                full_reg.next = 1;
            }
        }

        if (*clear_in) {
            wp_reg.next = 0;
            rp_reg.next = 0;
            full_reg.next = 0;
        }

        afull_reg.next = full_reg || (wp_reg >= rp_reg ? wp_reg - rp_reg : FIFO_DEPTH - rp_reg + wp_reg) >= FIFO_DEPTH/2;

        mem.work(clk);
    }

    void strobe()
    {
        wp_reg.strobe();
        rp_reg.strobe();
        full_reg.strobe();
        afull_reg.strobe();

        mem.strobe();
    }

    void comb()
    {
        mem.comb();
        mem.data_out_comb_func();
    }

    std::string name;
};

```


* Module class definition can use template parameters. (currently supported only default values)

* Only *work()* function body is recommended to be offloaded to .cpp object file, while all other bodies should be filled in .h file.

* Only positive edge clocking is supported now, but supporting of both edges requires minor changes.

* It is allowed to use native C++ types like bool, unsigned, unsigned long, etc

* Is is only allowed to use C++HDL types inside reg<> as register types

* Each combinational value should have _comb postfix and corresponding value_name_comb_func() method which calculates and returns combinatinal value (.next ??)

* Each of *connect()*, *reset()*, *work()*, *strobe()* and *comb()* functions should call corresponding functions of module's nested instances

* Registers have current and next values. Only reg_name.next value can be changed. Both current and .next register's value can be used on right side, in expressions.

* In reset() function methods .clr() and .set(val) are used to set up registers values.

* [f]printf() functions are converted to $write() during SV conversion, exit() becomes finish(), DEBUG_NAME macroses are converted to `DEBUG_NAME and {} are replaced with %x in text

## Input/output ports (bidir?)

&nbsp;&nbsp;&nbsp;&nbsp;Input and output ports are always pointers in C++HDL. It allows instant value update on change.
For combinational variables pointer to value is used, but value change require name_comb_func() call. This question will be discussed in corresponding chapter.

&nbsp;&nbsp;&nbsp;&nbsp;The strict naming convention is used for input/output ports:

* property_in or obj_property_in - generic input port name
* property_out or obj_property_out - generic output port name

It is strongly recommended to initialize all ports pointers right away in class description. All output ports should take address
of particular variables of the module or it's nested instances. All input ports should be assigned nullptr value to make possible uninitialization visible.

Since pointers are used to connect ports, any pair of connected ports must have similar variable types or variable sizes in bytes.

Any port size is multiple of 8 bit. There is no way to use less size of a variable in C++.
Size reduction happens after SystemVerilog conversion and uses one of the following ways:
* Standalone types of <8bit size (like reg<u1>) are translated to corresponding SystemVerilog types (like logic[0:0])
* Structs should be packed and can have integral-type fields of any bit size (not more than maximum possible size in C++)
* Composite types (structs with non-integral types and cat() busses) always align their subtypes size to 8 bit. Unused bits will be removed later after SV optimization

Therefore, when you use a joint point between C++HDL/SV module with third-party SV module, always use packed structs to achieve proper <8bit fields placing.

Function *connect()* is used to assign nested instances inputs to data sources in the module.

## Clock (and reset)

&nbsp;&nbsp;&nbsp;&nbsp;The only one clock is used currently, always named clk. Each *work()* function currently checks that only positive edge condition is called.

Work function can make any changes to variables. Definition of local variables is prohibited (same as in Verilog). Only .next value of registers should be changed.

Work function can call other functions to make code structured. Function with return value represents function on Verilog while function with void return represents
task in Verilog.

## Variables list

&nbsp;&nbsp;&nbsp;&nbsp;Variables are module class members and can be of 3 types:

* registers
* combinational values
* temporary variables

&nbsp;&nbsp;&nbsp;&nbsp;Registers are of type reg<TYPE> and always contain value, updated on last clock edge. To access next value of register reg_name.next property is used.
It is recommended to give register variables names a postfix _reg in cases when register is used as output port or in parent modules.

&nbsp;&nbsp;&nbsp;&nbsp;Combinational variable can be of any



## Work function

## Strobe function

## Comb function

## Data types

### logic<WIDTH>

### u1, u8, u16, u32, u64

    (s8, s16, s32, s64)

### u<WIDTH>

### reg<TYPE>

### array<SIZE>

### memory<TYPE,SIZE>

### cat(obj,obj,...)

# Appendix

(Any supporting info…)
