#!/usr/bin/env bash
# Resolve sampled executor callback addresses, including PIE/ASLR binaries.
set -euo pipefail
binary=${1:?usage: report_native_dispatch.sh BINARY PROFILE_LOG [--lanes]}
log=${2:?profile log required}
lanes=0
if [[ ${3:-} == --lanes ]]; then lanes=1; elif [[ $# != 2 ]]; then exit 2; fi
symbols=$(mktemp)
trap 'rm -f -- "$symbols"' EXIT
nm -n -C -- "$binary" > "$symbols"
awk -v lanes="$lanes" '
FNR==NR {
    if($1 ~ /^[[:xdigit:]]+$/ && NF>=3) {
        address=strtonum("0x"$1); name=$0;
        sub(/^[[:xdigit:]]+[[:space:]]+[[:alpha:]][[:space:]]+/,"",name);
        names[address]=name;
        if(name=="cpphdl::graph_runtime::DispatchProfile::now()")anchor=address;
    }
    next;
}
($1=="cpphdl_dispatch_profile" && !lanes) || ($1=="cpphdl_dispatch_lane" && lanes) {
    delete v;
    for(i=2;i<=NF;++i){split($i,p,"=");v[p[1]]=p[2];}
    if(!anchor || !("anchor" in v)) {
        print "Missing profiling anchor; use the matching instrumented binary and log" > "/dev/stderr";
        bad=1;exit 1;
    }
    callback=strtonum(v["callback"])-strtonum(v["anchor"])+anchor;
    if(!(callback in names)) {
        print "Unresolved callback; verify the binary matches this log" > "/dev/stderr";
        bad=1;exit 1;
    }
    if(lanes) {
        if(!rows++)print "workers\ttasks\tlane\tsamples\tstart_delay_us\tservice_us\titems\tcallback";
        s=v["samples"]+0;
        printf "%u\t%u\t%u\t%.0f\t%.3f\t%.3f\t%.2f\t%s\n",v["workers"],v["tasks"],v["lane"],s,
            v["start_delay_ns"]/s/1000,v["service_ns"]/s/1000,v["items"]/s,names[callback];
        next;
    }
    if(!rows++) print "workers\ttasks\tcalls\tsamples\tmean_us\tgap_before_us\twake_us\tcaller_us\tjoin_us\tmax_start_delay_us\tmin_service_us\tmax_service_us\tmin_items\tmax_items\tcallback";
    n=v["calls"]+0;s=v["samples"]+0;
    printf "%u\t%u\t%.0f\t%.0f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.2f\t%.2f\t%s\n",
        v["workers"],v["tasks"],n,s,v["elapsed_ns"]/n/1000,v["gap_before_ns"]/n/1000,
        v["wake_ns"]/s/1000,v["caller_ns"]/s/1000,v["join_ns"]/s/1000,
        v["max_start_delay_ns"]/s/1000,v["min_service_ns"]/s/1000,v["max_service_ns"]/s/1000,
        v["min_items"]/s,v["max_items"]/s,names[callback];
}
END {if(!rows && !bad){print "No dispatch profile records found" > "/dev/stderr";exit 1;}}
' "$symbols" "$log"
