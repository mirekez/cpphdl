#pragma once
#include "SynchronousRam.h"
#include "math/FloatConvert.h"

namespace cpphdl::hls {
// Affine matrix copy through a 32x32 banked transpose buffer. Source can be
// FP16 or FP32; destination is FP32. Read credits and a held output packet
// decouple memory latency/backpressure from the one-beat-per-clock data path.
// Callers must supply disjoint source/destination views. No padding writes.
class MatrixTransfer:public cpphdl::Module {
public:
    using Beat=cpphdl::array<16,cpphdl::u32>;
    _PORT(bool) command_valid_in;_PORT(bool) command_ready_out;
    _PORT(uint32_t) source_in;_PORT(uint32_t) destination_in;
    _PORT(uint32_t) rows_in;_PORT(uint32_t) columns_in;
    _PORT(uint32_t) source_row_stride_in;_PORT(uint32_t) source_column_stride_in;
    _PORT(uint32_t) destination_row_stride_in;_PORT(uint32_t) destination_column_stride_in;
    _PORT(bool) half_in;
    _PORT(bool) response_valid_out;_PORT(bool) response_ready_in;_PORT(uint32_t) fault_out;
    _PORT(uint32_t) m_address_out;_PORT(Beat) m_writedata_out;_PORT(cpphdl::logic<64>) m_byteenable_out;
    _PORT(bool) m_read_out;_PORT(bool) m_write_out;
    _PORT(bool) m_waitrequest_in;_PORT(Beat) m_readdata_in;
    _PORT(bool) m_readdatavalid_in;_PORT(bool) m_readdataerror_in;
private:
    SynchronousRam<1,32> bank[32];
    cpphdl::reg<cpphdl::u<2>> state; // idle, load, store, response
    cpphdl::reg<cpphdl::u32> source,destination,rows,columns,sr,sc,dr,dc,error;
    cpphdl::reg<cpphdl::u1> half,load_rows,store_rows;
    cpphdl::reg<cpphdl::u32> tile_row,tile_col,tile_rows,tile_cols;
    cpphdl::reg<cpphdl::u32> cursor_row,cursor_col,head,tail,pending;
    cpphdl::reg<cpphdl::u1> requests_done,store_valid,read_held;
    cpphdl::reg<cpphdl::array<64,cpphdl::u32>> metadata;
    cpphdl::reg<cpphdl::u32> store_meta,store_address;
    cpphdl::reg<cpphdl::logic<64>> store_mask;
    uint32_t address_comb=0,count_comb=0,meta_comb=0;
    uint32_t response_meta_comb=0;
    bool banks_comb=false,fetch_comb=false,return_comb=false;
    cpphdl::array<32,cpphdl::u1> write_enable_comb{},read_enable_comb{};
    cpphdl::array<32,cpphdl::u32> write_address_comb{},read_address_comb{},write_data_comb{},read_data_comb{};
    Beat data_comb{};
    uint32_t minimum(uint32_t a,uint32_t b){return a<b?a:b;}
    uint32_t& address_comb_func() {
        uint32_t row_stride,col_stride,element,available,fast_left,fast_stride;
        bool row_fast;
        row_stride=state==1?uint32_t(sr):uint32_t(dr);col_stride=state==1?uint32_t(sc):uint32_t(dc);
        row_fast=state==1?bool(load_rows):bool(store_rows);element=state==1&&bool(half)?2:4;
        address_comb=(state==1?uint32_t(source):uint32_t(destination))+
            (uint32_t(tile_row)+uint32_t(cursor_row))*row_stride+(uint32_t(tile_col)+uint32_t(cursor_col))*col_stride;
        available=(64-(address_comb&63))/element;
        fast_left=row_fast?uint32_t(tile_rows)-uint32_t(cursor_row):uint32_t(tile_cols)-uint32_t(cursor_col);
        fast_stride=row_fast?row_stride:col_stride;
        count_comb=minimum(fast_left,fast_stride==element?available:1);
        meta_comb=uint32_t(cursor_row)|(uint32_t(cursor_col)<<5)|(count_comb<<10)|((address_comb&63)<<16);
        return address_comb;
    }
    bool& banks_comb_func() {
        uint32_t ignored,meta,r,c,count,offset,index,raw;
        r=0;c=0;index=0;raw=0;
        ignored=address_comb_func();
        banks_comb=true;
        return_comb=state==1&&m_readdatavalid_in()&&(pending!=0||(m_read_out()&&!m_waitrequest_in()));
        fetch_comb=state==2&&!bool(requests_done)&&(!bool(store_valid)||!m_waitrequest_in())&&error==0;
        response_meta_comb=pending!=0?uint32_t(metadata[head]):meta_comb;
        write_enable_comb=0;read_enable_comb=0;write_address_comb=0;read_address_comb=0;write_data_comb=0;
        meta=response_meta_comb;count=(meta>>10)&63;offset=(meta>>16)&63;
        for(uint32_t lane=0;lane<32;++lane)if(return_comb&&error==0&&!m_readdataerror_in()&&lane<count) {
            r=(meta&31)+(bool(load_rows)?lane:0);c=((meta>>5)&31)+(bool(load_rows)?0:lane);index=(r+c)&31;
            raw=uint32_t(m_readdata_in()[(offset+lane*(bool(half)?2:4))/4]);
            if(bool(half))raw=(raw>>(((offset/2+lane)&1)*16))&65535;
            write_enable_comb[index]=true;write_address_comb[index]=r;
            write_data_comb[index]=bool(half)?math::FloatConvertMethods::from_half(raw):raw;
        }
        for(uint32_t lane=0;lane<16;++lane)if(fetch_comb&&lane<count_comb) {
            r=uint32_t(cursor_row)+(bool(store_rows)?lane:0);c=uint32_t(cursor_col)+(bool(store_rows)?0:lane);index=(r+c)&31;
            read_enable_comb[index]=true;read_address_comb[index]=r;
        }
        return banks_comb;
    }
    Beat& data_comb_func() {
        uint32_t r,c,index,meta,count,offset;
        r=0;c=0;index=0;
        for(uint32_t i=0;i<32;++i)read_data_comb[i]=uint32_t(bank[i].data_out()[0]);
        data_comb=0;meta=uint32_t(store_meta);count=(meta>>10)&63;offset=(meta>>16)&63;
        for(uint32_t lane=0;lane<16;++lane)if(lane<count) {
            r=(meta&31)+(bool(store_rows)?lane:0);c=((meta>>5)&31)+(bool(store_rows)?0:lane);index=(r+c)&31;
            data_comb[offset/4+lane]=read_data_comb[index];
        }
        return data_comb;
    }
    void advance_cursor(bool row_fast,uint32_t count) {
        uint32_t next;
        next=(row_fast?uint32_t(cursor_row):uint32_t(cursor_col))+count;
        if(next==(row_fast?uint32_t(tile_rows):uint32_t(tile_cols))) {
            if(row_fast){cursor_row._next=0;cursor_col._next=uint32_t(cursor_col)+1;}
            else{cursor_col._next=0;cursor_row._next=uint32_t(cursor_row)+1;}
            if((row_fast?uint32_t(cursor_col)+1:uint32_t(cursor_row)+1)==(row_fast?uint32_t(tile_cols):uint32_t(tile_rows)))requests_done._next=true;
        } else {if(row_fast)cursor_row._next=next;else cursor_col._next=next;}
    }
public:
    void _assign() {
        command_ready_out=_ASSIGN(state==0);response_valid_out=_ASSIGN(state==3);fault_out=_ASSIGN(uint32_t(error));
        m_read_out=_ASSIGN(state==1&&(bool(read_held)||(!bool(requests_done)&&pending<64&&error==0)));
        m_write_out=_ASSIGN(state==2&&bool(store_valid)&&error==0);
        m_address_out=_ASSIGN(state==2?uint32_t(store_address):(address_comb_func()&0xffffffc0u));
        m_byteenable_out=_ASSIGN(store_mask);m_writedata_out=_ASSIGN_COMB(data_comb_func());
        for(uint32_t i=0;i<32;++i) {
            bank[i].read_in=_ASSIGN_I(bool(read_enable_comb[i]));bank[i].read_address_in=_ASSIGN_I(uint32_t(read_address_comb[i]));
            bank[i].write_in=_ASSIGN_I(bool(write_enable_comb[i]));bank[i].write_address_in=_ASSIGN_I(uint32_t(write_address_comb[i]));
            bank[i].data_in=_ASSIGN_I(cpphdl::array<1,cpphdl::u32>(write_data_comb[i]));bank[i]._assign();
        }
    }
    void _work(bool reset) {
        bool request,response,ignored;uint32_t outstanding,element;uint64_t src_end,dst_end,mask;
        if(!reset)ignored=banks_comb_func();
        for(uint32_t i=0;i<32;++i)bank[i]._work(reset);
        request=m_read_out()&&!m_waitrequest_in();response=return_comb;
        if(reset) {
            state.clr();source.clr();destination.clr();rows.clr();columns.clr();sr.clr();sc.clr();dr.clr();dc.clr();error.clr();
            half.clr();load_rows.clr();store_rows.clr();tile_row.clr();tile_col.clr();tile_rows.clr();tile_cols.clr();
            cursor_row.clr();cursor_col.clr();head.clr();tail.clr();pending.clr();requests_done.clr();store_valid.clr();read_held.clr();metadata.clr();
            store_meta.clr();store_address.clr();store_mask.clr();
        } else {
            read_held._next=m_read_out()&&m_waitrequest_in();
            if(state==0&&command_valid_in()) {
                source._next=source_in();destination._next=destination_in();rows._next=rows_in();columns._next=columns_in();
                sr._next=source_row_stride_in();sc._next=source_column_stride_in();dr._next=destination_row_stride_in();dc._next=destination_column_stride_in();
                half._next=half_in();element=half_in()?2:4;
                load_rows._next=source_row_stride_in()<source_column_stride_in();store_rows._next=destination_row_stride_in()<destination_column_stride_in();
                tile_row._next=0;tile_col._next=0;tile_rows._next=minimum(rows_in(),32);tile_cols._next=minimum(columns_in(),32);
                cursor_row._next=0;cursor_col._next=0;head._next=0;tail._next=0;pending._next=0;requests_done._next=false;store_valid._next=false;error._next=0;
                src_end=uint64_t(source_in())+uint64_t(rows_in()-1)*source_row_stride_in()+uint64_t(columns_in()-1)*source_column_stride_in()+element;
                dst_end=uint64_t(destination_in())+uint64_t(rows_in()-1)*destination_row_stride_in()+uint64_t(columns_in()-1)*destination_column_stride_in()+4;
                if(!rows_in()||!columns_in()||rows_in()>131072||columns_in()>131072||
                    ((source_in()|source_row_stride_in()|source_column_stride_in())&(element-1))||
                    ((destination_in()|destination_row_stride_in()|destination_column_stride_in())&3)||
                    !source_row_stride_in()||!source_column_stride_in()||!destination_row_stride_in()||!destination_column_stride_in()||
                    src_end>0x100000000ull||dst_end>0x100000000ull||
                    (uint64_t(source_in())<dst_end&&uint64_t(destination_in())<src_end)) {error._next=1;state._next=3;}
                else state._next=1;
            }
            if(state==1) {
                outstanding=uint32_t(pending)+(request?1:0)-(response?1:0);pending._next=outstanding;
                if(request){metadata._next[tail]=meta_comb;tail._next=(uint32_t(tail)+1)&63;advance_cursor(bool(load_rows),count_comb);}
                if(response){head._next=(uint32_t(head)+1)&63;if(m_readdataerror_in())error._next=2;}
                if((bool(requests_done)||error!=0)&&outstanding==0&&!m_read_out()) {
                    state._next=(error!=0||(response&&m_readdataerror_in()))?3:2;cursor_row._next=0;cursor_col._next=0;requests_done._next=false;
                }
            }
            if(state==2) {
                if(bool(store_valid)&&!m_waitrequest_in())store_valid._next=false;
                if(fetch_comb) {
                    store_meta._next=meta_comb;store_address._next=address_comb&0xffffffc0u;
                    mask=count_comb==16?~uint64_t(0):((uint64_t(1)<<(count_comb*4))-1)<<(address_comb&63);
                    store_mask._next=mask;store_valid._next=true;advance_cursor(bool(store_rows),count_comb);
                }
                if(bool(requests_done)&&(!bool(store_valid)||!m_waitrequest_in())) {
                    cursor_row._next=0;cursor_col._next=0;requests_done._next=false;
                    if(uint32_t(tile_col)+32<columns) {
                        tile_col._next=uint32_t(tile_col)+32;tile_cols._next=minimum(uint32_t(columns)-uint32_t(tile_col)-32,32);state._next=1;
                    } else if(uint32_t(tile_row)+32<rows) {
                        tile_row._next=uint32_t(tile_row)+32;tile_rows._next=minimum(uint32_t(rows)-uint32_t(tile_row)-32,32);
                        tile_col._next=0;tile_cols._next=minimum(uint32_t(columns),32);state._next=1;
                    } else state._next=3;
                }
            }
            if(state==3&&response_ready_in())state._next=0;
        }
    }
    void _strobe() {
        for(uint32_t i=0;i<32;++i)bank[i]._strobe();
        state.strobe();source.strobe();destination.strobe();rows.strobe();columns.strobe();sr.strobe();sc.strobe();dr.strobe();dc.strobe();error.strobe();
        half.strobe();load_rows.strobe();store_rows.strobe();tile_row.strobe();tile_col.strobe();tile_rows.strobe();tile_cols.strobe();
        cursor_row.strobe();cursor_col.strobe();head.strobe();tail.strobe();pending.strobe();requests_done.strobe();store_valid.strobe();read_held.strobe();metadata.strobe();
        store_meta.strobe();store_address.strobe();store_mask.strobe();
    }
};
}
