require_relative '../disconnected_ports'

def fixture
  netlist = RBA::Netlist.new
  circuit = RBA::Circuit.new
  circuit.name = 'TOP'
  netlist.add(circuit)
  kind = RBA::DeviceClassResistor.new
  kind.name = 'R'
  netlist.add(kind)
  resistor = circuit.create_device(kind, 'R1')
  %w[A B RESERVED].each_with_index do |name, index|
    net = circuit.create_net(name)
    pin = circuit.create_pin(name)
    circuit.connect_pin(pin.id, net)
    resistor.connect_terminal(index, net) if index < 2
  end
  [netlist, circuit, resistor]
end

def rejected
  begin
    yield
  rescue RuntimeError
    return
  end
  raise 'accepted an invalid disconnected-port declaration'
end

netlist, circuit, = fixture
keeper = DisconnectedPorts.new(netlist, 'TOP', ['RESERVED'], 'schematic_netlist')
netlist.simplify
raise 'fixture did not exercise deleted pin' unless circuit.pin_count == 2
record = keeper.restore
raise 'lost public interface' unless circuit.pin_count == 3
raise 'restoration introduced connectivity' unless circuit.net_by_name('RESERVED').each_terminal.to_a.empty?
raise 'missing audit' unless record['preserved_ports'].size == 1
rejected { keeper.restore }
rejected { DisconnectedPorts.new(netlist, 'TOP', ['MISSING'], 'layout_netlist') }
rejected { DisconnectedPorts.new(netlist, 'TOP', ['A'], 'layout_netlist') }

netlist, circuit, = fixture
pin = circuit.create_pin('ALIAS')
circuit.connect_pin(pin.id, circuit.net_by_name('RESERVED'))
rejected { DisconnectedPorts.new(netlist, 'TOP', ['RESERVED'], 'layout_netlist') }

netlist, circuit, = fixture
child = RBA::Circuit.new
child.name = 'CHILD'
netlist.add(child)
pin = child.create_pin('IN')
sub = circuit.create_subcircuit(child, 'LOAD')
sub.connect_pin(pin.id, circuit.net_by_name('RESERVED'))
rejected { DisconnectedPorts.new(netlist, 'TOP', ['RESERVED'], 'layout_netlist') }
puts 'PASS disconnected ports: restoration, missing net, device load, port alias, child load, repeated restoration'
