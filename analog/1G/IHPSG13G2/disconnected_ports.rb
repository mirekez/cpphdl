# Keep explicitly unused public pins through KLayout's netlist simplification.
# Extraction must first prove that each named net exists and has no devices,
# child pins, or alias to another public port. Active connections are never
# removed or manufactured by this adapter.
class DisconnectedPorts
  def initialize(netlist, top_name, names, label)
    @netlist, @top_name, @names, @label = netlist, top_name, names, label
    circuits = netlist.each_circuit.select { |c| c.name.upcase == top_name.upcase }
    raise 'ambiguous or missing disconnected-port top' unless circuits.size == 1
    circuit = circuits.first
    @records = names.map do |name|
      nets = circuit.each_net.select { |n| n.name.upcase == name.upcase }
      raise "missing or aliased disconnected net: #{name}" unless nets.size == 1
      net = nets.first
      pins = net.each_pin.to_a
      unless net.each_terminal.to_a.empty? && net.each_subcircuit_pin.to_a.empty? &&
             pins.size <= 1 && pins.all? { |p| p.pin.name.upcase == name.upcase }
        raise "declared disconnected port has a connection: #{name}"
      end
      if label == 'schematic_netlist' && pins.size != 1
        raise "disconnected schematic net is not a public pin: #{name}"
      end
      { 'name' => net.name, 'device_terminals' => 0, 'subcircuit_pins' => 0,
        'public_pins_before_simplification' => pins.size }
    end
  end

  def restore
    circuit = @netlist.each_circuit.find { |c| c.name.upcase == @top_name.upcase }
    raise 'simplification removed the component' unless circuit
    @records.each do |record|
      name = record['name']
      net = circuit.net_by_name(name)
      raise "disconnected net unexpectedly survived simplification: #{name}" if net
      net = circuit.create_net(name)
      pin = circuit.create_pin(name)
      circuit.connect_pin(pin.id, net)
    end
    { 'top' => @top_name, 'netlist' => @label, 'preserved_ports' => @records }
  end
end
