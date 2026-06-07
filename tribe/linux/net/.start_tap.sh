env ETHGIG_TAP_TRACE=1 ETHGIG_TAP_TRACE_FILE=/tmp/ethgig-tap.log     ../../../build/tribe_linux/net/ethgig_tap --tap tap-tribe --socket /tmp/tribe-ethgig.sock &
sleep 1
sudo ip addr add 192.168.76.1/24 dev tap-tribe 2>/dev/null || true
sudo ip link set tap-tribe up
sudo chmod a+rw /tmp/tribe-ethgig.sock

#in cpu:
#  ip link set eth0 up
#  ip addr add 192.168.76.2/24 dev eth0
#  ping 192.168.76.1
