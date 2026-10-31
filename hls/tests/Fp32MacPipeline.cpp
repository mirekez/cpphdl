#include "../Clocked.h"
#include "../math/Fp32MacPipeline.h"

class Fp32MacPipelineTop : public cpphdl::Module {
public:
    cpphdl::hls::ClockedPipeline<cpphdl::hls::math::Fp32MacMethods,3,uint32_t,uint32_t> worker;
    _PORT(bool) command_valid_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) index_in;
    _PORT(uint32_t) value_in;
    _PORT(bool) response_ready_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_valid_out;
    _PORT(uint32_t) result_out;
    _PORT(uint32_t) fault_out;
    void _assign() {
        worker.command_valid_in=_ASSIGN(command_valid_in());
        worker.operation_in=_ASSIGN(operation_in());
        worker.index_in=_ASSIGN(index_in());
        worker.value_in=_ASSIGN(value_in());
        worker.response_ready_in=_ASSIGN(response_ready_in());worker._assign();
        command_ready_out=_ASSIGN(worker.command_ready_out());
        response_valid_out=_ASSIGN(worker.response_valid_out());
        result_out=_ASSIGN(worker.result_out());fault_out=_ASSIGN(worker.fault_out());
    }
    void _work(bool reset){worker._work(reset);}
    void _strobe(){worker._strobe();}
};
