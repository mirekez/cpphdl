#include "../common.hpp"

class Adder8 : public IC {
public:
    _PORT(bits<8>) A;
    _PORT(bits<8>) B;
    _PORT(bits<8>) SUM;

    void _assign() override {
        SUM = sim_add(A, B); // Modulo-256 value purpose.
    }
};

class Register8 : public IC {
public:
    _PORT(bits<8>) D;
    _PORT(bits<1>) CLK;
    _PORT(bits<8>) Q;

    void _assign() override {
        Q = sim_store(D, CLK); // Initially zero; sample on rising edges.
    }
};

class Pipeline : public IC {
    Adder8 math;
    Register8 first, second;

public:
    _PORT(bits<8>) A;
    _PORT(bits<8>) B;
    _PORT(bits<1>) CLK;
    _PORT(bits<8>) Q;
    _PORT(bits<8>) R;

    void _assign() override {
        math.A = A;
        math.B = B;
        math.name("pipeline.math");
        math.elaborate();

        first.D = math.SUM;
        first.CLK = CLK;
        first.name("pipeline.first");
        first.elaborate();

        second.D = first.Q;
        second.CLK = CLK;
        second.name("pipeline.second");
        second.elaborate();

        Q = first.Q;
        R = second.Q;
        // Declaration order does not change old-state/simultaneous-commit semantics.
    }
};

int main(int argc, char** argv) {
    return example_main(argc, argv, "examples/digital_pipeline/results", 2_us, 100_ns, [](simulation& sim) {
        bits<8> A, B;
        bits<1> CLK;
        sim.drive(A, 250);
        sim.drive(B, 10);
        sim.clock(CLK, 1_us);

        Pipeline pipe;
        pipe.A = A;
        pipe.B = B;
        pipe.CLK = CLK;
        pipe.name("pipeline");
        pipe.elaborate();

        sim.observe("Q", pipe.Q);
        sim.observe("R", pipe.R);
        sim.observe("CLK", CLK);
        sim.require_final("first register", pipe.Q, 4);
        sim.require_final("second register", pipe.R, 4);
    });
}
