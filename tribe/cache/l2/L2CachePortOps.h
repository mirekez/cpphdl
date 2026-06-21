#pragma once

#include "L2CacheTimeoutOps.h"

template<class GEOM>
struct L2CachePortChoice
{
    // True when any requester won arbitration; used by the controller to leave idle.
    bool valid;
    // True for coherent AXI slave maintenance traffic; gives those requests priority over CPU traffic.
    bool from_slave;
    // True for CPU data-side traffic; lets response logic distinguish D-cache from I-cache completion.
    bool data_port;
    // True when the selected request needs a cache read/lookup path.
    bool read;
    // True when the selected request needs a cache write/update path.
    bool write;
};

template<class GEOM>
class L2CachePortOps : public GEOM
{
public:
    // Re-export geometry constants for port-arbitration helpers; called by the final L2CacheOO stack.
    L2CACHE_GEOMETRY_CONSTANTS(GEOM);

    // Select the request source and operation kind for one cache cycle; called by future top-level arbitration.
    static L2CachePortChoice<GEOM> choose(bool slave_read, bool slave_write,
        bool d_read, bool d_write, bool i_read, bool i_write)
    {
        L2CachePortChoice<GEOM> choice;

        choice.valid = slave_read || slave_write || d_read || d_write || i_read || i_write;
        choice.from_slave = slave_read || slave_write;
        choice.data_port = !choice.from_slave && (d_read || d_write);
        choice.read = (choice.from_slave && !slave_write) || (!choice.from_slave && d_read) ||
            (!choice.from_slave && !d_read && !d_write && i_read);
        choice.write = (choice.from_slave && slave_write) || (!choice.from_slave && d_write) ||
            (!choice.from_slave && !d_read && !d_write && i_write);
        return choice;
    }

    // Decide whether the CPU instruction port must wait; called by future CPU response presentation logic.
    static bool cpu_i_wait(bool i_pending, bool d_pending, bool slave_pending,
        bool done_i_read, bool idle)
    {
        bool wait;

        wait = false;
        if (i_pending) {
            wait = !done_i_read;
        }
        if (!idle && !done_i_read) {
            wait = true;
        }
        if (d_pending || slave_pending) {
            wait = true;
        }
        return wait;
    }

    // Decide whether the CPU data port must wait; called by future CPU response presentation logic.
    static bool cpu_d_wait(bool d_pending, bool slave_pending, bool done_d, bool idle)
    {
        bool wait;

        wait = false;
        if (d_pending) {
            wait = !done_d;
        }
        if (!idle && !done_d) {
            wait = true;
        }
        if (slave_pending) {
            wait = true;
        }
        return wait;
    }
};
