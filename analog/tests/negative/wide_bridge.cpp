#include <cpphdl_analog.h>
class Wide : public cpphdl::Module {
public:
    _PORT(cpphdl::logic<65>) Q;
};
void invalid(std::shared_ptr<Wide> module) {
    analog::sim_registered_output<65>(module, &Wide::Q);
}
