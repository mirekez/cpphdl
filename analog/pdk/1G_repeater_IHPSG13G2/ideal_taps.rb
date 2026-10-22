# Prepare the independent schematic for the PDK's documented ideal-tap mode.
# Geometry is never read here. All non-tap device parameters are preserved.
require 'digest'
require 'json'
require 'logger'

# Source preparation must not combine devices. The independent PDK LVS run
# subsequently applies its normal series/parallel reduction on both sides.
SERIES_RES = false
PARALLEL_RES = false

def logger
  @contact_logger ||= Logger.new($stdout)
end

def dbu
  0.001
end

$reader_inputs = {}
def expand_reader(path)
  path = File.realpath(path)
  $reader_inputs[path] = Digest::SHA256.file(path).hexdigest
  File.readlines(path).map do |line|
    match = line.match(/^\s*#\s*%include\s+(\S+)\s*$/)
    match ? expand_reader(File.join(File.dirname(path), match[1])) : line
  end.join
end

reader_file = File.join($pdk, 'ihp-sg13g2/libs.tech/klayout/tech/lvs/rule_decks/custom_classes.lvs')
eval(expand_reader(reader_file), TOPLEVEL_BINDING, reader_file)
class SourceReader < CustomReader
  def ordered_terminals(prefix, model, count)
    terminal_list_for_element(prefix, model, Array.new(count))
  end
end
class SourceWriter < CustomWriter
  private

  def primitive_prefix(device_class)
    pair = PREFIX_MAP.find { |name, _prefix| name.casecmp(device_class.name).zero? }
    raise "Unknown PDK primitive #{device_class.name}" unless pair
    pair[1]
  end

  def generate_device_prefix(device, device_class)
    "#{primitive_prefix(device_class)}#{device.expanded_name} "
  end

  def generate_device_terminals(device, device_class)
    # Use the reader's SPICE order, not the device-class declaration order.
    # In particular MOS classes may declare S,G,D,B while SPICE needs D,G,S,B.
    @source_reader ||= SourceReader.new
    names = @source_reader.ordered_terminals(primitive_prefix(device_class), device_class.name,
                                             device_class.terminal_definitions.length)
    names.map { |name| net_to_string(device.net_for_terminal(name)) }.join(' ') + ' '
  end
end

def terminal_graph(circuit, aliases)
  result = {}
  circuit.each_device do |device|
    type = device.device_class
    next if ['ptap1', 'ntap1'].include?(type.name.downcase)
    identity = [circuit.name.upcase, device.name.upcase, type.name.downcase]
    result[identity] = type.terminal_definitions.map do |terminal|
      net = device.net_for_terminal(terminal.id)
      name = net ? net.expanded_name.tr(',', '|') : '(unconnected)'
      [terminal.name.downcase, aliases.fetch(name, name).upcase]
    end.to_h
  end
  result
end
netlist = RBA::Netlist.new
netlist.read($input, RBA::NetlistSpiceReader.new(CustomReader.new))
netlist.flatten
selected = netlist.circuit_by_name($top.upcase)
raise 'Requested schematic top was not found' unless selected
original_ports = selected.each_pin.map { |pin| [pin.id, pin.name] }

def non_tap_parameters(netlist, selected)
  result = {}
  netlist.each_circuit do |circuit|
    next unless circuit == selected
    circuit.each_device do |device|
      type = device.device_class
      next if ['ptap1', 'ntap1'].include?(type.name.downcase)
      result[[circuit.name.upcase, device.name.upcase, type.name.downcase]] = type.parameter_definitions.map do |parameter|
        [parameter.name, device.parameter(parameter.id)]
      end.to_h
    end
  end
  result
end

before = non_tap_parameters(netlist, selected)
contacts = []
netlist.each_circuit do |circuit|
  next unless circuit == selected
  circuit.each_device.to_a.each do |device|
    type = device.device_class
    next unless ['ptap1', 'ntap1'].include?(type.name.downcase)
    tie, well = device.net_for_terminal('TIE'), device.net_for_terminal('WELL')
    raise 'Unconnected schematic tap terminal' unless tie && well
    contacts << {circuit: circuit.name, name: device.name, type: type.name,
                 tie: tie.expanded_name, well: well.expanded_name,
                 source_parameters: type.parameter_definitions.map { |parameter|
                   [parameter.name, device.parameter(parameter.id)] }.to_h}
    circuit.join_nets(tie, well) unless tie == well
    circuit.remove_device(device)
  end
end
raise 'Ideal-contact selection did not find any schematic taps' if contacts.empty?
raise 'Non-tap device parameters changed' unless before == non_tap_parameters(netlist, selected)

writer = RBA::NetlistSpiceWriter.new(SourceWriter.new)
writer.use_net_names = true
netlist.write($output, writer)
# A joined net can acquire an alias such as SUBSTRATE|VSS. Preserve each
# original external pin's name in the emitted flat source, without mutating
# KLayout's pin/net objects or changing any device terminal relationship.
aliases = {}
original_ports.each do |id, pin_name|
  # The standard SPICE writer separates a joined net's aliases with '|'.
  net = selected.net_for_pin(id)
  raise 'Ideal contacts short distinct external schematic ports' unless net
  name = net.expanded_name.tr(',', '|')
  raise 'Ideal contacts short distinct external schematic ports' if aliases.key?(name) && aliases[name] != pin_name
  aliases[name] = pin_name
end
active = false
flat = []
File.readlines($output).each do |line|
  fields = line.split
  if fields[0]&.upcase == '.SUBCKT'
    active = fields[1].casecmp($top).zero?
  end
  if active
    flat << (line.lstrip.start_with?('*') ? line : fields.map { |field| aliases.fetch(field, field) }.join(' ') + "\n")
  end
  active = false if fields[0]&.upcase == '.ENDS'
end
raise 'Flattened schematic contains no selected top' if flat.empty?
File.write($output, "* Independent source with ideal ohmic tap contacts\n" + flat.join)
verified = RBA::Netlist.new
verified.read($output, RBA::NetlistSpiceReader.new(CustomReader.new))
roundtrip = non_tap_parameters(verified, verified.circuit_by_name($top.upcase))
raise 'SPICE round trip changed device terminal connectivity' unless terminal_graph(selected, aliases) ==
  terminal_graph(verified.circuit_by_name($top.upcase), {})
raise 'SPICE round trip changed device identities' unless before.keys.sort == roundtrip.keys.sort
maximum_relative_error = 0.0
before.each do |identity, parameters|
  raise 'SPICE round trip changed parameter names' unless parameters.keys.sort == roundtrip[identity].keys.sort
  parameters.each do |name, value|
    actual = roundtrip[identity][name]
    error = (actual - value).abs / [1.0, value.abs].max
    maximum_relative_error = [maximum_relative_error, error].max
    raise 'SPICE round trip changed device parameters or units' if error > 1e-10
  end
end
audit = {contact_model: 'ideal ohmic tie-to-well/substrate connection',
         source_schematic: File.realpath($input), top: $top, source_sha256: Digest::SHA256.file($input).hexdigest,
         output_sha256: Digest::SHA256.file($output).hexdigest,
         pdk_reader_sha256: $reader_inputs, non_tap_device_count: before.length,
         port_aliases: aliases, original_ports: original_ports,
         non_tap_parameters_changed: false, taps: contacts,
         preparation_device_combination: false,
         roundtrip_maximum_relative_parameter_error: maximum_relative_error,
         roundtrip_terminal_connectivity_verified: true,
         scope: 'independent source-only model selection; requires PDK --disable_tap_extraction for geometry LVS'}
File.write($audit, JSON.pretty_generate(audit) + "\n")
puts "Prepared #{contacts.length} ideal schematic contacts; preserved #{before.length} other devices"
